#ifndef SERIALIZACION_H_
#define SERIALIZACION_H_

/*
 * =============================================================================
 * PROTOCOLO DE SERIALIZACIÓN
 * =============================================================================
 *
 * Todo mensaje en el wire sigue SIEMPRE este formato:
 *
 *   [ op_code : sizeof(op_code) bytes ]
 *   [ size    : sizeof(size_t) bytes  ]   <- tamaño del payload
 *   [ stream  : size bytes            ]   <- payload campo a campo, sin padding
 *
 * Para enviar un mensaje:
 *   1. t_paquete *p = crear_paquete(MI_OPCODE);
 *   2. agregar_a_paquete(p, &campo, sizeof(campo));   <- un llamado por campo
 *      agregar_cadena(p, mi_string);                  <- para strings
 *   3. enviar_paquete(p, socket);
 *   4. eliminar_paquete(p);
 *
 * Para recibir un mensaje:
 *   1. op_code cod = recibir_codigo_operacion(socket); <- lee solo el opcode
 *   2. switch(cod) { case MI_OPCODE: recibir_X(socket); }
 *      La función recibir_X llama validar_buffer() internamente y luego
 *      hace recv() campo por campo en el mismo orden que se serializó.
 *
 * Strings: se serializan como [ longitud : int ][ chars : longitud bytes ]
 *          usando agregar_cadena() / recibir_cadena().
 *
 * NUNCA se envía un struct directamente con send() para evitar problemas
 * de padding y diferencias de arquitectura entre módulos.
 *
 * =============================================================================
 */

#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Tipos compartidos del protocolo (op_code, t_contexto, etc.)
#include "utils.h"

/* ---- TADs de serialización ---- */

typedef struct {
    size_t size;    /* tamaño del payload en bytes  */
    void  *stream;  /* bloque contiguo de bytes     */
} t_buffer;

typedef struct {
    op_code   codigo_operacion;
    t_buffer *buffer;
} t_paquete;

/* ---- Ciclo de vida del paquete ---- */

void      crear_buffer(t_paquete *paquete);
t_paquete *crear_paquete(op_code codigo_operacion);
void      eliminar_paquete(t_paquete *paquete);

/* ---- Escritura en el paquete ---- */

/// @brief Agrega `tamanio` bytes de `valor` al stream del paquete.
void agregar_a_paquete(t_paquete *paquete, void *valor, int tamanio);

/// @brief Agrega una cadena al paquete como [ longitud : int ][ chars ].
/// @note  La longitud incluye el '\0' terminal.
void agregar_cadena(t_paquete *paquete, char *cadena);

/* ---- Envío ---- */

/// @brief Serializa y envía el paquete completo por el socket.
void enviar_paquete(t_paquete *paquete, int socket_cliente);

/* ---- Recepción (capa baja) ---- */

/// @brief Lee el op_code del próximo mensaje. Llamar ANTES de cualquier recibir_X().
op_code recibir_codigo_operacion(int socket);

/// @brief Valida que el buffer entrante tiene el tamaño esperado.
///        Llamar al inicio de cada función recibir_X().
void validar_buffer(int socket);

/// @brief Lee una cadena serializada por agregar_cadena(). El caller hace free().
char *recibir_cadena(int socket);

/* ---- ACKs genéricos ---- */

/// @brief Envía un paquete de confirmación con el opcode indicado (sin payload útil).
void enviar_ok(op_code codigo_operacion, int socket);

/// @brief Recibe y descarta un paquete de confirmación.
void recibir_ok(int socket);

#endif /* SERIALIZACION_H_ */