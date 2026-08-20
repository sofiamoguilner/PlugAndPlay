#include "mensajes.h"

/*
 * =============================================================================
 * mensajes.c  —  Implementación de mensajes de alto nivel
 * =============================================================================
 *
 * REGLA: este archivo solo usa agregar_a_paquete(), agregar_cadena(),
 *        enviar_paquete(), validar_buffer(), recv() y recibir_cadena().
 *        Nunca llama a send() directamente.
 *
 * Patrón enviar:
 *   t_paquete *p = crear_paquete(OPCODE);
 *   agregar_a_paquete(p, &campo, sizeof(campo));
 *   agregar_cadena(p, mi_string);          <- para strings
 *   enviar_paquete(p, socket);
 *   eliminar_paquete(p);
 *
 * Patrón recibir:
 *   validar_buffer(socket);                <- SIEMPRE primero
 *   recv(socket, &campo, sizeof(campo), MSG_WAITALL);
 *   char *s = recibir_cadena(socket);      <- para strings
 *
 * =============================================================================
 */

/* ============================================================================
 * Helpers internos: agregar/recibir estructuras compuestas
 * ============================================================================ */

/// @brief Agrega los campos de t_registros al paquete.
static void agregar_registros(t_paquete *paquete, t_registros *regs)
{
    agregar_a_paquete(paquete, regs, sizeof(t_registros));
}

/// @brief Recibe t_registros desde el socket.
/// @warning El caller debe liberar la estructura retornada.
static t_registros *recibir_registros(int socket)
{
    t_registros *regs = malloc(sizeof(t_registros));
    recv(socket, regs, sizeof(t_registros), MSG_WAITALL);
    return regs;
}

/// @brief Agrega los campos de t_segmento al paquete.
static void agregar_segmento(t_paquete *paquete, t_segmento *seg)
{
    agregar_a_paquete(paquete, seg, sizeof(t_segmento));
}

/// @brief Recibe un t_segmento desde el socket.
/// @warning El caller debe liberar la estructura retornada.
static t_segmento *recibir_segmento(int socket)
{
    t_segmento *seg = malloc(sizeof(t_segmento));
    recv(socket, seg, sizeof(t_segmento), MSG_WAITALL);
    return seg;
}

/* ============================================================================
 * Instrucción
 * ============================================================================
 *
 * wire (enviar): [ pid : int ][ pc : uint32_t ]
 * wire (recibir): [ instruccion : cadena ]
 */

void enviar_obtener_instruccion(int memory_socket, int pid, uint32_t pc)
{
    t_paquete *paquete = crear_paquete(OBTENER_INSTRUCCION);
    agregar_a_paquete(paquete, &pid, sizeof(pid));
    agregar_a_paquete(paquete, &pc,  sizeof(pc));
    enviar_paquete(paquete, memory_socket);
    eliminar_paquete(paquete);
}

/// @brief Recibe la instruccion respondida por memoria.
/// @warning El caller debe liberar la cadena retornada con free().
char *recibir_instruccion(int socket)
{
    validar_buffer(socket);
    return recibir_cadena(socket);
}

/* ============================================================================
 * Contexto
 * ============================================================================
 *
 * wire (solicitar): [ pid : int ]
 *
 * wire (recibir contexto):
 *   [ registros : sizeof(t_registros) bytes ]
 *   [ cantidad_segmentos : int ]
 *   [ seg_i : sizeof(t_segmento) bytes ] × cantidad
 *
 * wire (actualizar):
 *   [ pid      : int ]
 *   [ registros: sizeof(t_registros) bytes ]
 *   [ cantidad : int ]
 *   [ seg_i    : sizeof(t_segmento) bytes ] × cantidad
 * wire (ACK): recibir_ok(socket)
 */

void enviar_solicitar_contexto(int memory_socket, int pid)
{
    t_paquete *paquete = crear_paquete(OBTENER_CONTEXTO);
    agregar_a_paquete(paquete, &pid, sizeof(pid));
    enviar_paquete(paquete, memory_socket);
    eliminar_paquete(paquete);
}

/// @brief Recibe el contexto completo desde memoria.
/// @warning El caller debe liberar ctx->tabla_segmentos y sus elementos, y ctx.
t_contexto *recibir_contexto(int socket)
{
    validar_buffer(socket);

    t_contexto *ctx = malloc(sizeof(t_contexto));

    t_registros *regs = recibir_registros(socket);
    ctx->registros = *regs;
    free(regs);

    int cantidad;
    recv(socket, &cantidad, sizeof(cantidad), MSG_WAITALL);
    ctx->tabla_segmentos = list_create();
    for (int i = 0; i < cantidad; i++) {
        t_segmento *seg = recibir_segmento(socket);
        list_add(ctx->tabla_segmentos, seg);
    }

    return ctx;
}

void enviar_actualizar_contexto(int memory_socket, t_contexto *contexto)
{
    t_paquete *paquete = crear_paquete(ACTUALIZAR_CONTEXTO);
    agregar_a_paquete(paquete, &contexto->pid, sizeof(contexto->pid));
    agregar_registros(paquete, &contexto->registros);
    int cantidad = list_size(contexto->tabla_segmentos);
    agregar_a_paquete(paquete, &cantidad, sizeof(cantidad));
    for (int i = 0; i < cantidad; i++) {
        t_segmento *seg = list_get(contexto->tabla_segmentos, i);
        agregar_segmento(paquete, seg);
    }
    enviar_paquete(paquete, memory_socket);
    eliminar_paquete(paquete);
    /* ACK: recibir_ok(memory_socket) en el caller */
}

/* ============================================================================
 * Proceso
 * ============================================================================
 *
 * wire (crear): [ pid : int ][ path : cadena ]
 * wire (ACK): recibir_ok(socket)
 * wire (ejecutar/pid): [ pid : int ]
 */

void enviar_crear_proceso(int memory_socket, int pid, const char *path)
{
    t_paquete *paquete = crear_paquete(CREAR_PROCESO);
    agregar_a_paquete(paquete, &pid, sizeof(pid));
    agregar_cadena(paquete, (char *)path);
    enviar_paquete(paquete, memory_socket);
    eliminar_paquete(paquete);
    /* ACK: recibir_ok(memory_socket) en el caller */
}

/// @brief Recibe pid y path de un mensaje CREAR_PROCESO.
/// @warning El caller debe liberar *path_out con free().
void recibir_crear_proceso(int socket, int *pid_out, char **path_out)
{
    validar_buffer(socket);
    recv(socket, pid_out, sizeof(*pid_out), MSG_WAITALL);
    *path_out = recibir_cadena(socket);
}

void enviar_pid(int socket, int pid)
{
    t_paquete *paquete = crear_paquete(EJECUTAR_PROCESO);
    agregar_a_paquete(paquete, &pid, sizeof(pid));
    enviar_paquete(paquete, socket);
    eliminar_paquete(paquete);
}

void enviar_interrupcion(int cpu_socket, int pid, int motivo)
{
    t_paquete *paquete = crear_paquete(INTERRUPCION);
    agregar_a_paquete(paquete, &pid,    sizeof(pid));
    agregar_a_paquete(paquete, &motivo, sizeof(motivo));
    enviar_paquete(paquete, cpu_socket);
    eliminar_paquete(paquete);
}

void recibir_interrupcion(int socket, int *pid_out, int *motivo_out)
{
    validar_buffer(socket);
    recv(socket, pid_out,    sizeof(*pid_out),    MSG_WAITALL);
    recv(socket, motivo_out, sizeof(*motivo_out), MSG_WAITALL);
}

int recibir_pid(int socket)
{
    validar_buffer(socket);
    int pid;
    recv(socket, &pid, sizeof(pid), MSG_WAITALL);
    return pid;
}

void enviar_devolver_proceso(int socket, int pid, t_motivo_devolucion motivo,
                             const char* nombre_syscall, char** params, int cant_params)
{
    t_paquete *paquete = crear_paquete(DEVOLVER_PROCESO);
    int motivo_i = (int)motivo;

    agregar_a_paquete(paquete, &pid, sizeof(pid));
    agregar_a_paquete(paquete, &motivo_i, sizeof(motivo_i));

    if (nombre_syscall != NULL) {
        agregar_cadena(paquete, (char *)nombre_syscall);

        if (cant_params < 0) {
            cant_params = 0;
        }

        agregar_a_paquete(paquete, &cant_params, sizeof(cant_params));
        for (int i = 0; i < cant_params; i++) {
            agregar_cadena(paquete, params[i]);
        }
    }

    enviar_paquete(paquete, socket);
    eliminar_paquete(paquete);
}

void recibir_devolver_proceso(int socket, int* pid_out, t_motivo_devolucion* motivo_out,
                              char** nombre_syscall_out, char*** params_out, int* cant_params_out)
{
    if (nombre_syscall_out != NULL) *nombre_syscall_out = NULL;
    if (params_out != NULL)         *params_out = NULL;
    if (cant_params_out != NULL)    *cant_params_out = 0;

    validar_buffer(socket);

    int pid = -1;
    recv(socket, &pid, sizeof(pid), MSG_WAITALL);
    if (pid_out != NULL) *pid_out = pid;

    int motivo_i = 0;
    recv(socket, &motivo_i, sizeof(motivo_i), MSG_WAITALL);
    if (motivo_out != NULL) *motivo_out = (t_motivo_devolucion)motivo_i;

    if ((t_motivo_devolucion)motivo_i != MOTIVO_SYSCALL) {
        return;
    }

    /* Para MOTIVO_SYSCALL el wire sigue con: nombre + cant_params + N x param. */
    char *nombre = recibir_cadena(socket);
    if (nombre_syscall_out != NULL) {
        *nombre_syscall_out = nombre;
    } else {
        free(nombre);
    }

    int cant = 0;
    recv(socket, &cant, sizeof(cant), MSG_WAITALL);
    if (cant < 0) cant = 0;
    if (cant_params_out != NULL) *cant_params_out = cant;

    if (cant == 0) return;

    char **params = (params_out != NULL) ? calloc((size_t)cant, sizeof(char*)) : NULL;
    for (int i = 0; i < cant; i++) {
        char *p = recibir_cadena(socket);
        if (params != NULL) params[i] = p;
        else free(p);
    }
    if (params_out != NULL) *params_out = params;
}

/* ============================================================================
 * Memoria: lectura
 * ============================================================================
 *
 * wire (solicitar): [ pid : int ][ dir_logica : uint32_t ][ tamanio : uint32_t ]
 * wire (respuesta): [ data : tamanio bytes ]
 */

void enviar_lectura_memoria(int memory_socket, int pid,
                             uint32_t dir_logica, uint32_t tamanio)
{
    t_paquete *paquete = crear_paquete(LECTURA_MEMORIA);
    agregar_a_paquete(paquete, &pid,        sizeof(pid));
    agregar_a_paquete(paquete, &dir_logica, sizeof(dir_logica));
    agregar_a_paquete(paquete, &tamanio,    sizeof(tamanio));
    enviar_paquete(paquete, memory_socket);
    eliminar_paquete(paquete);
}

/// @brief Recibe `tamanio` bytes de datos de memoria.
/// @warning El caller debe liberar el puntero retornado con free().
void *recibir_datos_memoria(int socket, uint32_t tamanio)
{
    validar_buffer(socket);
    void *datos = malloc(tamanio);
    recv(socket, datos, tamanio, MSG_WAITALL);
    return datos;
}

/* ============================================================================
 * Memoria: escritura
 * ============================================================================
 *
 * wire (solicitar): [ pid : int ][ dir_logica : uint32_t ]
 *                   [ tamanio : uint32_t ][ data : tamanio bytes ]
 * wire (ACK): recibir_ok(socket)
 */

void enviar_escritura_memoria(int memory_socket, int pid,
                               uint32_t dir_logica,
                               void *datos, uint32_t tamanio)
{
    t_paquete *paquete = crear_paquete(ESCRITURA_MEMORIA);
    agregar_a_paquete(paquete, &pid,        sizeof(pid));
    agregar_a_paquete(paquete, &dir_logica, sizeof(dir_logica));
    agregar_a_paquete(paquete, &tamanio,    sizeof(tamanio));
    agregar_a_paquete(paquete, datos,       (int)tamanio);
    enviar_paquete(paquete, memory_socket);
    eliminar_paquete(paquete);
    /* ACK: recibir_ok(memory_socket) en el caller */
}

/* ============================================================================
 * Espacio libre (mock)
 * ============================================================================
 *
 * wire (solicitar): sin payload
 * wire (respuesta): [ espacio_libre : uint32_t ]
 */

void enviar_obtener_espacio_libre(int memory_socket)
{
    t_paquete *p = crear_paquete(OBTENER_ESPACIO_LIBRE);
    enviar_paquete(p, memory_socket);
    eliminar_paquete(p);
}

uint32_t recibir_espacio_libre(int socket)
{
    validar_buffer(socket);
    uint32_t espacio = 0;
    recv(socket, &espacio, sizeof(espacio), MSG_WAITALL);
    return espacio;
}

/* ============================================================================
 * Handshakes
 * ============================================================================
 *
 * Todos usan el mismo wire format [ op_code ][ size ][ payload ].
 * Los que no tienen datos útiles usan enviar_ok / recibir_ok.
 *
 * HANDSHAKE_SCHEDULER_A_MEMORY : sin payload
 * HANDSHAKE_CPU                : [ cpu_id : int ]
 * HANDSHAKE_SWAP               : [ block_size : uint32_t ][ total_size : uint32_t ]
 * HANDSHAKE_MEMORY_STICK       : [ memory_size : uint32_t ][ ip : cadena ][ puerto : cadena ]
 */

void enviar_handshake_scheduler_a_memory(int memory_socket)
{
    enviar_ok(HANDSHAKE_SCHEDULER_A_MEMORY, memory_socket);
    /* ACK: recibir_ok(memory_socket) en el caller */
}

void enviar_handshake_cpu(int socket, int cpu_id)
{
    t_paquete *paquete = crear_paquete(HANDSHAKE_CPU);
    agregar_a_paquete(paquete, &cpu_id, sizeof(cpu_id));
    enviar_paquete(paquete, socket);
    eliminar_paquete(paquete);
}

int recibir_handshake_cpu(int socket)
{
    validar_buffer(socket);
    int cpu_id;
    recv(socket, &cpu_id, sizeof(cpu_id), MSG_WAITALL);
    return cpu_id;
}

void enviar_handshake_io(int socket, const char *nombre)
{
    t_paquete *paquete = crear_paquete(HANDSHAKE_IO);
    agregar_cadena(paquete, (char *)nombre);
    enviar_paquete(paquete, socket);
    eliminar_paquete(paquete);
}

/// @warning El caller debe liberar el nombre retornado con free().
char *recibir_handshake_io(int socket)
{
    validar_buffer(socket);
    return recibir_cadena(socket);
}

void enviar_handshake_swap(int socket, uint32_t block_size, uint32_t total_size)
{
    t_paquete *paquete = crear_paquete(HANDSHAKE_SWAP);
    agregar_a_paquete(paquete, &block_size,  sizeof(block_size));
    agregar_a_paquete(paquete, &total_size,  sizeof(total_size));
    enviar_paquete(paquete, socket);
    eliminar_paquete(paquete);
}

void recibir_handshake_swap(int socket, uint32_t *block_size_out, uint32_t *total_size_out)
{
    validar_buffer(socket);
    recv(socket, block_size_out, sizeof(*block_size_out), MSG_WAITALL);
    recv(socket, total_size_out, sizeof(*total_size_out), MSG_WAITALL);
}

void enviar_handshake_memory_stick(int memory_socket, uint32_t memory_size,
                                   const char *ip, const char *puerto)
{
    t_paquete *paquete = crear_paquete(HANDSHAKE_MEMORY_STICK);
    agregar_a_paquete(paquete, &memory_size, sizeof(memory_size));
    agregar_cadena(paquete, (char *)ip);
    agregar_cadena(paquete, (char *)puerto);
    enviar_paquete(paquete, memory_socket);
    eliminar_paquete(paquete);
}

/// @warning El caller debe liberar *ip_out y *puerto_out con free().
void recibir_handshake_memory_stick(int socket, uint32_t *memory_size_out,
                                    char **ip_out, char **puerto_out)
{
    validar_buffer(socket);
    recv(socket, memory_size_out, sizeof(*memory_size_out), MSG_WAITALL);
    *ip_out     = recibir_cadena(socket);
    *puerto_out = recibir_cadena(socket);
}

/* ============================================================================
 * Memory Stick: acceso físico  (CPU → Memory Stick)
 * ============================================================================
 *
 * wire lectura  (request) : [ pid : int32_t ][ dir_fisica : uint32_t ][ tamanio : uint32_t ]
 *               (response): [ data : tamanio bytes ]
 * wire escritura (request): [ pid : int32_t ][ dir_fisica : uint32_t ][ tamanio : uint32_t ]
 *                           [ data : tamanio bytes ]
 *               (response): ACK  (recibir_ok)
 */

void enviar_lectura_fisica(int stick_socket, int pid,
                            uint32_t dir_fisica, uint32_t tamanio)
{
    t_paquete *paquete = crear_paquete(LECTURA_MEMORIA);
    agregar_a_paquete(paquete, &pid,        sizeof(pid));
    agregar_a_paquete(paquete, &dir_fisica, sizeof(dir_fisica));
    agregar_a_paquete(paquete, &tamanio,    sizeof(tamanio));
    enviar_paquete(paquete, stick_socket);
    eliminar_paquete(paquete);
}

void enviar_escritura_fisica(int stick_socket, int pid, uint32_t dir_fisica,
                              void *datos, uint32_t tamanio)
{
    t_paquete *paquete = crear_paquete(ESCRITURA_MEMORIA);
    agregar_a_paquete(paquete, &pid,        sizeof(pid));
    agregar_a_paquete(paquete, &dir_fisica, sizeof(dir_fisica));
    agregar_a_paquete(paquete, &tamanio,    sizeof(tamanio));
    agregar_a_paquete(paquete, datos,       (int)tamanio);
    enviar_paquete(paquete, stick_socket);
    eliminar_paquete(paquete);
    /* ACK: recibir_ok(stick_socket) en el caller */
}

/* ============================================================================
 * Nueva memoria disponible  (Kernel Memory → CPU / Scheduler)
 * ============================================================================
 *
 * wire: [ tamanio : uint32_t ][ base_fisica : uint32_t ][ ip : cadena ][ puerto : cadena ]
 */

void enviar_nueva_memoria_disponible(int cpu_socket, uint32_t tamanio, uint32_t base_fisica,
                                     const char *ip, const char *puerto)
{
    t_paquete *paquete = crear_paquete(NUEVA_MEMORIA_DISPONIBLE);
    agregar_a_paquete(paquete, &tamanio,     sizeof(tamanio));
    agregar_a_paquete(paquete, &base_fisica, sizeof(base_fisica));
    agregar_cadena(paquete, (char *)ip);
    agregar_cadena(paquete, (char *)puerto);
    enviar_paquete(paquete, cpu_socket);
    eliminar_paquete(paquete);
}

/// @warning El caller debe liberar *ip_out y *puerto_out con free().
void recibir_nueva_memoria_disponible(int socket, uint32_t *tamanio_out, uint32_t *base_fisica_out,
                                      char **ip_out, char **puerto_out)
{
    validar_buffer(socket);
    recv(socket, tamanio_out,     sizeof(*tamanio_out),     MSG_WAITALL);
    recv(socket, base_fisica_out, sizeof(*base_fisica_out), MSG_WAITALL);
    *ip_out     = recibir_cadena(socket);
    *puerto_out = recibir_cadena(socket);
}

/* ============================================================================
 * Config de la MMU  (Kernel Memory → CPU)
 * ============================================================================
 *
 * Sincrónico tras el HANDSHAKE_CPU: memoria le informa a la CPU el
 * SEGMENT_MAX_SIZE para que la MMU de la CPU coincida con la de memoria.
 *
 * wire: [ segment_max_size : uint32_t ]
 */

void enviar_config_mmu_cpu(int cpu_socket, uint32_t segment_max_size)
{
    t_paquete *paquete = crear_paquete(CONFIG_MMU_CPU);
    agregar_a_paquete(paquete, &segment_max_size, sizeof(segment_max_size));
    enviar_paquete(paquete, cpu_socket);
    eliminar_paquete(paquete);
}

uint32_t recibir_config_mmu_cpu(int socket)
{
    validar_buffer(socket);
    uint32_t segment_max_size = 0;
    recv(socket, &segment_max_size, sizeof(segment_max_size), MSG_WAITALL);
    return segment_max_size;
}

/* ============================================================================
 * BSOD
 * ============================================================================
 *
 * wire: sin payload (enviar_ok / recibir_ok)
 */

void enviar_memoria_corrupta(int scheduler_socket)
{
    enviar_ok(MEMORIA_CORRUPTA_BSOD, scheduler_socket);
}

void enviar_memoria_liberada(int scheduler_socket)
{
    enviar_ok(MEMORIA_LIBERADA, scheduler_socket);
}

bool recibir_memoria_corrupta_no_bloqueante(int socket)
{
    op_code codigo;
    int bytes = recv(socket, &codigo, sizeof(op_code), MSG_DONTWAIT);
    if (bytes <= 0) return false;

    /* Drenar el payload (size + bytes) SIEMPRE, así op_codes desconocidos
       (p.ej. MEMORIA_LIBERADA) no corrompen el siguiente recv. */
    size_t tamanio;
    recv(socket, &tamanio, sizeof(tamanio), MSG_WAITALL);
    if (tamanio > 0) {
        void *basura = malloc(tamanio);
        recv(socket, basura, tamanio, MSG_WAITALL);
        free(basura);
    }

    return codigo == MEMORIA_CORRUPTA_BSOD;
}

/* ============================================================================
 * Swap: escritura de bloque
 * ============================================================================
 *
 * wire (solicitar): [ bloque : uint32_t ][ data : size bytes ]
 * wire (respuesta): [ status : int ]
 */

void enviar_escritura_bloque_swap(int socket, uint32_t bloque,
                                  void *data, uint32_t size)
{
    t_paquete *paquete = crear_paquete(ESCRITURA_BLOQUE_SWAP);
    agregar_a_paquete(paquete, &bloque, sizeof(bloque));
    agregar_a_paquete(paquete, data,    (int)size);
    enviar_paquete(paquete, socket);
    eliminar_paquete(paquete);
}

/// @warning El caller debe liberar *data_out con free().
void recibir_escritura_bloque_swap(int socket, uint32_t *bloque_out,
                                   void **data_out, uint32_t size)
{
    validar_buffer(socket);
    recv(socket, bloque_out, sizeof(*bloque_out), MSG_WAITALL);
    *data_out = malloc(size);
    recv(socket, *data_out, size, MSG_WAITALL);
}

void enviar_resultado_swap(int socket, int status)
{
    t_paquete *paquete = crear_paquete(RESPUESTA_SWAP);
    agregar_a_paquete(paquete, &status, sizeof(status));
    enviar_paquete(paquete, socket);
    eliminar_paquete(paquete);
}

int recibir_resultado_swap(int socket)
{
    recibir_codigo_operacion(socket);
    validar_buffer(socket);
    int status;
    recv(socket, &status, sizeof(status), MSG_WAITALL);
    return status;
}

/* ============================================================================
 * Swap: lectura de bloque
 * ============================================================================
 *
 * wire (solicitar): [ bloque : uint32_t ]
 * wire (respuesta): [ status : int ][ data : size bytes ]  (solo si status == 0)
 */

void enviar_lectura_bloque_swap(int socket, uint32_t bloque)
{
    t_paquete *paquete = crear_paquete(LECTURA_BLOQUE_SWAP);
    agregar_a_paquete(paquete, &bloque, sizeof(bloque));
    enviar_paquete(paquete, socket);
    eliminar_paquete(paquete);
}

uint32_t recibir_lectura_bloque_swap(int socket)
{
    validar_buffer(socket);
    uint32_t bloque;
    recv(socket, &bloque, sizeof(bloque), MSG_WAITALL);
    return bloque;
}

void enviar_bloque_swap(int socket, int status, void *data, uint32_t size)
{
    t_paquete *paquete = crear_paquete(RESPUESTA_SWAP);
    agregar_a_paquete(paquete, &status, sizeof(status));
    if (status == 0 && data != NULL && size > 0)
        agregar_a_paquete(paquete, data, (int)size);
    enviar_paquete(paquete, socket);
    eliminar_paquete(paquete);
}

/// @brief Recibe la respuesta a una lectura de bloque.
/// @warning Si status == 0, el caller debe liberar el puntero retornado con free().
/// @return puntero a los datos, o NULL si status != 0
void *recibir_bloque_swap(int socket, int *status_out, uint32_t size)
{
    recibir_codigo_operacion(socket);
    validar_buffer(socket);
    recv(socket, status_out, sizeof(*status_out), MSG_WAITALL);
    if (*status_out != 0) return NULL;
    void *data = malloc(size);
    recv(socket, data, size, MSG_WAITALL);
    return data;
}

/* ============================================================================
 * IO
 * ============================================================================
 *
 * Pedidos (Scheduler → IO):
 *   wire IO_SLEEP:  [ pid : int ][ tiempo_ms : int ]
 *   wire IO_STDIN:  [ pid : int ][ size : int ]
 *   wire IO_STDOUT: [ pid : int ][ datos : size bytes ]
 *
 * Respuestas (IO → Scheduler, op_code IO_FINALIZADA):
 *   wire fin simple: [ pid : int ]
 *   wire con datos:  [ pid : int ][ datos : size bytes ]
 */

void enviar_io_sleep(int io_socket, int pid, int tiempo_ms)
{
    t_paquete *paquete = crear_paquete(IO_SLEEP);
    agregar_a_paquete(paquete, &pid,       sizeof(pid));
    agregar_a_paquete(paquete, &tiempo_ms, sizeof(tiempo_ms));
    enviar_paquete(paquete, io_socket);
    eliminar_paquete(paquete);
}

void enviar_io_stdin(int io_socket, int pid, int size)
{
    t_paquete *paquete = crear_paquete(IO_STDIN);
    agregar_a_paquete(paquete, &pid,  sizeof(pid));
    agregar_a_paquete(paquete, &size, sizeof(size));
    enviar_paquete(paquete, io_socket);
    eliminar_paquete(paquete);
}

void enviar_io_stdout(int io_socket, int pid, void *datos, int size)
{
    t_paquete *paquete = crear_paquete(IO_STDOUT);
    agregar_a_paquete(paquete, &pid,  sizeof(pid));
    agregar_a_paquete(paquete, datos, size);
    enviar_paquete(paquete, io_socket);
    eliminar_paquete(paquete);
}

void enviar_fin_io(int scheduler_socket, int pid)
{
    t_paquete *paquete = crear_paquete(IO_FINALIZADA);
    agregar_a_paquete(paquete, &pid, sizeof(pid));
    enviar_paquete(paquete, scheduler_socket);
    eliminar_paquete(paquete);
}

int recibir_fin_io(int socket)
{
    validar_buffer(socket);
    int pid;
    recv(socket, &pid, sizeof(pid), MSG_WAITALL);
    return pid;
}

void enviar_datos_stdin(int scheduler_socket, int pid, void *datos, int size)
{
    t_paquete *paquete = crear_paquete(IO_FINALIZADA);
    agregar_a_paquete(paquete, &pid,  sizeof(pid));
    agregar_a_paquete(paquete, datos, size);
    enviar_paquete(paquete, scheduler_socket);
    eliminar_paquete(paquete);
}

/// @warning El caller debe liberar el buffer retornado con free().
void *recibir_datos_stdin(int socket, int *pid_out, int size)
{
    validar_buffer(socket);
    recv(socket, pid_out, sizeof(*pid_out), MSG_WAITALL);
    void *datos = malloc(size);
    recv(socket, datos, size, MSG_WAITALL);
    return datos;
}

/* ============================================================================
 * Suspensión / Des-suspensión de procesos
 * ============================================================================ */

void enviar_suspender_proceso(int socket, int pid)
{
    t_paquete *p = crear_paquete(SUSPENDER_PROCESO);
    agregar_a_paquete(p, &pid, sizeof(pid));
    enviar_paquete(p, socket);
    eliminar_paquete(p);
}

int recibir_suspender_proceso(int socket)
{
    validar_buffer(socket);
    int pid;
    recv(socket, &pid, sizeof(pid), MSG_WAITALL);
    return pid;
}

void enviar_des_suspender_proceso(int socket, int pid)
{
    t_paquete *p = crear_paquete(DES_SUSPENDER_PROCESO);
    agregar_a_paquete(p, &pid, sizeof(pid));
    enviar_paquete(p, socket);
    eliminar_paquete(p);
}

int recibir_des_suspender_proceso(int socket)
{
    validar_buffer(socket);
    int pid;
    recv(socket, &pid, sizeof(pid), MSG_WAITALL);
    return pid;
}

/* ===========================================================================
 * Segmentos
 * =========================================================================== */

void enviar_solicitar_segmento(int memory_socket, int pid,
                                uint32_t id_segmento, uint32_t tamanio)
{
    t_paquete* p = crear_paquete(SOLICITAR_SEGMENTO);
    agregar_a_paquete(p, &pid,         sizeof(pid));
    agregar_a_paquete(p, &id_segmento, sizeof(id_segmento));
    agregar_a_paquete(p, &tamanio,     sizeof(tamanio));
    enviar_paquete(p, memory_socket);
    eliminar_paquete(p);
}

void recibir_respuesta_segmento(int socket,
                                 uint32_t* id_out, uint32_t* base_out,
                                 uint32_t* limite_out)
{
    validar_buffer(socket);
    recv(socket, id_out,    sizeof(*id_out),    MSG_WAITALL);
    recv(socket, base_out,  sizeof(*base_out),  MSG_WAITALL);
    recv(socket, limite_out, sizeof(*limite_out), MSG_WAITALL);
}

void enviar_eliminar_segmento(int memory_socket, int pid, uint32_t id_segmento)
{
    t_paquete* p = crear_paquete(ELIMINAR_SEGMENTO);
    agregar_a_paquete(p, &pid,         sizeof(pid));
    agregar_a_paquete(p, &id_segmento, sizeof(id_segmento));
    enviar_paquete(p, memory_socket);
    eliminar_paquete(p);
}

void enviar_finalizar_proceso_memory(int memory_socket, int pid)
{
    t_paquete* p = crear_paquete(FINALIZAR_PROCESO);
    agregar_a_paquete(p, &pid, sizeof(pid));
    enviar_paquete(p, memory_socket);
    eliminar_paquete(p);
}
