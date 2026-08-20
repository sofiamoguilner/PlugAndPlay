#include "serializacion.h"

#include <utils/sockets.h>

/*
 * =============================================================================
 * serializacion.c
 * =============================================================================
 *
 * Implementa la capa de transporte uniforme. Ver serializacion.h para el
 * protocolo completo.
 *
 * =============================================================================
 */

/* ============================================================================
 * Ciclo de vida
 * ============================================================================ */

void crear_buffer(t_paquete *paquete)
{
    paquete->buffer         = malloc(sizeof(t_buffer));
    paquete->buffer->size   = 0;
    paquete->buffer->stream = NULL;
}

t_paquete *crear_paquete(op_code codigo_operacion)
{
    t_paquete *paquete = malloc(sizeof(t_paquete));
    paquete->codigo_operacion = codigo_operacion;
    crear_buffer(paquete);
    return paquete;
}

void eliminar_paquete(t_paquete *paquete)
{
    free(paquete->buffer->stream);
    free(paquete->buffer);
    free(paquete);
}

/* ============================================================================
 * Escritura en el paquete
 * ============================================================================ */

/// @brief Agrega `tamanio` bytes de `valor` al final del stream del paquete.
void agregar_a_paquete(t_paquete *paquete, void *valor, int tamanio)
{
    paquete->buffer->stream = realloc(paquete->buffer->stream,
                                      paquete->buffer->size + tamanio);
    memcpy(paquete->buffer->stream + paquete->buffer->size, valor, tamanio);
    paquete->buffer->size += tamanio;
}

/// @brief Agrega una cadena como [ longitud : int ][ chars : longitud bytes ].
///        La longitud incluye el '\0' terminal.
void agregar_cadena(t_paquete *paquete, char *cadena)
{
    int longitud = 0;
    while (cadena[longitud++] != '\0') { }           /* longitud con '\0' */
    agregar_a_paquete(paquete, &longitud, sizeof(longitud));
    agregar_a_paquete(paquete, cadena, sizeof(char) * longitud);
}

/* ============================================================================
 * Envío
 * ============================================================================ */

static void *serializar_paquete(t_paquete *paquete, int bytes)
{
    void *stream  = malloc(bytes);
    int   offset  = 0;

    memcpy(stream + offset, &paquete->codigo_operacion,
           sizeof(paquete->codigo_operacion));
    offset += sizeof(paquete->codigo_operacion);

    memcpy(stream + offset, &paquete->buffer->size,
           sizeof(paquete->buffer->size));
    offset += sizeof(paquete->buffer->size);

    if (paquete->buffer->size > 0)
        memcpy(stream + offset, paquete->buffer->stream,
               paquete->buffer->size);

    return stream;
}

/// @brief Serializa y envía el paquete completo:
///        [ op_code ][ size ][ stream ]
void enviar_paquete(t_paquete *paquete, int socket_cliente)
{
    int   bytes     = sizeof(paquete->codigo_operacion)
                    + sizeof(paquete->buffer->size)
                    + paquete->buffer->size;
    void *a_enviar  = serializar_paquete(paquete, bytes);

    if (send_all(socket_cliente, a_enviar, (size_t)bytes) != 0) {
        fprintf(stderr, "[serializacion] Error enviando paquete completo\n");
    }
    free(a_enviar);
}

/* ============================================================================
 * Recepción (capa baja)
 * ============================================================================ */

/// @brief Lee el op_code del próximo mensaje.
///        DEBE llamarse antes de cualquier recibir_X().
/// @return Opcode leído o (op_code)-1 si hubo error/desconexión.
op_code recibir_codigo_operacion(int socket)
{
    op_code codigo_operacion;
    int     leidos = recv(socket, &codigo_operacion,
                          sizeof(codigo_operacion), MSG_WAITALL);

    if (leidos <= 0 || leidos != (int)sizeof(codigo_operacion)) {
        return (op_code)-1;
    }

    if (getenv("UTILS_TRACE_WIRE") != NULL) {
        const unsigned char *b = (const unsigned char *)&codigo_operacion;
        fprintf(stderr, "[wire-recv-opcode fd=%d] %02x %02x %02x %02x = %d\n",
                socket, b[0], b[1], b[2], b[3], (int)codigo_operacion);
    }

    return codigo_operacion;
}

/// @brief Valida que el buffer entrante es legible en su totalidad.
///        DEBE ser el primer llamado dentro de cada recibir_X().
void validar_buffer(int socket)
{
    size_t tamanio_esperado;
    recv(socket, &tamanio_esperado, sizeof(tamanio_esperado), MSG_WAITALL);

    void  *buffer      = malloc(tamanio_esperado);
    size_t tamanio_real = recv(socket, buffer, tamanio_esperado,
                                MSG_WAITALL | MSG_PEEK);
    free(buffer);

    if (tamanio_real == (size_t)-1 || tamanio_real != tamanio_esperado) {
        fprintf(stderr,
            "[serializacion] Buffer inválido. Esperado: %zu, Real: %zu.\n"
            "¿Olvidaste llamar recibir_codigo_operacion() antes?\n",
            tamanio_esperado, tamanio_real);
        abort();
    }
}

/// @brief Lee [ longitud : int ][ chars : longitud bytes ] del socket.
///        El caller es responsable de liberar la cadena con free().
char *recibir_cadena(int socket)
{
    int longitud;
    recv(socket, &longitud, sizeof(longitud), MSG_WAITALL);
    char *cadena = malloc(sizeof(char) * longitud);
    recv(socket, cadena, sizeof(char) * longitud, MSG_WAITALL);
    return cadena;
}

/* ============================================================================
 * ACKs genéricos
 * ============================================================================ */

/// @brief Envía un paquete de confirmación (payload: un int centinela = -1).
void enviar_ok(op_code codigo_operacion, int socket)
{
    t_paquete *paquete = crear_paquete(codigo_operacion);
    int centinela = -1;
    agregar_a_paquete(paquete, &centinela, sizeof(centinela));
    enviar_paquete(paquete, socket);
    eliminar_paquete(paquete);
}

/// @brief Recibe y descarta el paquete de confirmación.
void recibir_ok(int socket)
{
    validar_buffer(socket);
    int centinela;
    recv(socket, &centinela, sizeof(centinela), MSG_WAITALL);
}