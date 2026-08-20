#ifndef MENSAJES_H_
#define MENSAJES_H_

/*
 * =============================================================================
 * mensajes.h  —  API de mensajes de alto nivel
 * =============================================================================
 *
 * Cada par enviar_X / recibir_X corresponde a un mensaje del protocolo.
 *
 * Patrón de uso en el EMISOR:
 *   enviar_X(datos..., socket);
 *
 * Patrón de uso en el RECEPTOR:
 *   op_code cod = recibir_codigo_operacion(socket);
 *   switch (cod) {
 *       case MI_OPCODE: recibir_X(socket); break;
 *       ...
 *   }
 *
 * Layout de cada mensaje en el wire (documentado junto a cada función):
 *   [ op_code ][ size ][ campo_1 ][ campo_2 ]...
 *   Los strings van como: [ longitud : int ][ chars : longitud bytes ]
 *
 * =============================================================================
 */

#include "serializacion.h"
#include <commons/collections/list.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- Recepción del código de operación (siempre primero) ---- */
op_code recibir_codigo_operacion(int socket);

/* ===========================================================================
 * Instrucción
 * wire (enviar): [ pid : int ][ pc : uint32_t ]
 * wire (recibir): [ instruccion : cadena ]
 * =========================================================================== */
void  enviar_obtener_instruccion(int memory_socket, int pid, uint32_t pc);
char *recibir_instruccion(int socket);

/* ===========================================================================
 * Contexto
 * wire (solicitar): [ pid : int ]
 * wire (recibir contexto): [ registros : sizeof(t_registros) bytes ]
 *                          [ cantidad_segmentos : int ]
 *                          [ seg_i : sizeof(t_segmento) bytes ] x cantidad
 * wire (actualizar): [ pid : int ][ registros : bytes ]
 *                    [ cantidad : int ][ seg_i : bytes ] x cantidad
 * wire (ACK): recibir_ok(socket)
 * =========================================================================== */
void        enviar_solicitar_contexto(int memory_socket, int pid);
t_contexto *recibir_contexto(int socket);

void enviar_actualizar_contexto(int memory_socket, t_contexto *contexto);
/* recibir ACK: recibir_ok(socket) */

/* ===========================================================================
 * Proceso
 * wire (crear): [ pid : int ][ path : cadena ]
 * wire (ACK): recibir_ok(socket)
 * wire (ejecutar/pid): [ pid : int ]
 * =========================================================================== */
void enviar_crear_proceso(int memory_socket, int pid, const char *path);
void recibir_crear_proceso(int socket, int *pid_out, char **path_out);

/* ===========================================================================
 * Suspensión / Des-suspensión
 * wire (request): [ pid : int ]
 * wire (ACK):     recibir_ok(socket)
 * =========================================================================== */
void enviar_suspender_proceso(int socket, int pid);
int  recibir_suspender_proceso(int socket);

void enviar_des_suspender_proceso(int socket, int pid);
int  recibir_des_suspender_proceso(int socket);

void enviar_pid(int socket, int pid);
int  recibir_pid(int socket);

/* ===========================================================================
 * Interrupción (Kernel Scheduler → CPU)
 * wire: [ pid : int ][ motivo : int ]
 * El motivo (t_motivo_devolucion) indica por qué se interrumpe: fin de quantum
 * (MOTIVO_QUANTUM) o desalojo por compactación (MOTIVO_DESALOJO). La CPU lo
 * reenvía en el DEVOLVER_PROCESO resultante.
 * =========================================================================== */
void enviar_interrupcion(int cpu_socket, int pid, int motivo);
void recibir_interrupcion(int socket, int *pid_out, int *motivo_out);

/* ==========================================================================
 * Devolver proceso (CPU -> Scheduler)
 * wire obligatorio: [ pid : int ][ motivo : int ]
 * wire opcional (solo syscall): [ nombre_syscall : cadena ]
 *                               [ cant_params : int ]
 *                               [ param_i : cadena ] x cant_params
 * ========================================================================== */
void enviar_devolver_proceso(int socket, int pid, t_motivo_devolucion motivo,
                             const char* nombre_syscall, char** params, int cant_params);
void recibir_devolver_proceso(int socket, int* pid_out, t_motivo_devolucion* motivo_out,
                              char** nombre_syscall_out, char*** params_out, int* cant_params_out);

/* ===========================================================================
 * Espacio libre (mock)
 * wire (solicitar): sin payload
 * wire (respuesta): [ espacio_libre : uint32_t ]
 * =========================================================================== */
void     enviar_obtener_espacio_libre(int memory_socket);
uint32_t recibir_espacio_libre(int socket);

/* ===========================================================================
 * Memoria: lectura
 * wire (solicitar): [ pid : int ][ dir_logica : uint32_t ][ tamanio : uint32_t ]
 * wire (respuesta): [ data : tamanio bytes ]
 * =========================================================================== */
void  enviar_lectura_memoria(int memory_socket, int pid,
                              uint32_t dir_logica, uint32_t tamanio);
void *recibir_datos_memoria(int socket, uint32_t tamanio);

/* ===========================================================================
 * Memoria: escritura
 * wire (solicitar): [ pid : int ][ dir_logica : uint32_t ]
 *                   [ tamanio : uint32_t ][ data : tamanio bytes ]
 * wire (ACK): recibir_ok(socket)
 * =========================================================================== */
void enviar_escritura_memoria(int memory_socket, int pid,
                               uint32_t dir_logica,
                               void *datos, uint32_t tamanio);
/* recibir ACK: recibir_ok(socket) */

/* ===========================================================================
 * Handshakes
 * Todos siguen el mismo wire format.
 * Sin payload útil: usan enviar_ok / recibir_ok internamente.
 *
 * HANDSHAKE_SCHEDULER_A_MEMORY : sin payload
 * HANDSHAKE_CPU                : [ cpu_id : int ]
 * HANDSHAKE_SWAP               : [ block_size : uint32_t ][ total_size : uint32_t ]
 * HANDSHAKE_MEMORY_STICK       : [ memory_size : uint32_t ][ ip : cadena ][ puerto : cadena ]
 * =========================================================================== */
void enviar_handshake_scheduler_a_memory(int memory_socket);
/* recibir ACK: recibir_ok(socket) */

void enviar_handshake_cpu(int socket, int cpu_id);
int  recibir_handshake_cpu(int socket);

/* HANDSHAKE_IO : [ nombre : cadena ]  (módulo IO → Kernel Scheduler) */
void  enviar_handshake_io(int socket, const char *nombre);
char *recibir_handshake_io(int socket);  /* el caller hace free() del nombre */

void enviar_handshake_swap(int socket, uint32_t block_size, uint32_t total_size);
void recibir_handshake_swap(int socket, uint32_t *block_size_out, uint32_t *total_size_out);

void enviar_handshake_memory_stick(int memory_socket, uint32_t memory_size,
                                   const char *ip, const char *puerto);
void recibir_handshake_memory_stick(int socket, uint32_t *memory_size_out,
                                    char **ip_out, char **puerto_out);

/* ===========================================================================
 * Memory Stick: acceso físico  (CPU → Memory Stick)
 * wire lectura  (request) : [ pid : int32_t ][ dir_fisica : uint32_t ][ tamanio : uint32_t ]
 *               (response): [ data : tamanio bytes ]
 * wire escritura (request): [ pid : int32_t ][ dir_fisica : uint32_t ][ tamanio : uint32_t ]
 *                           [ data : tamanio bytes ]
 *               (response): ACK  (recibir_ok)
 * =========================================================================== */
void  enviar_lectura_fisica(int stick_socket, int pid,
                             uint32_t dir_fisica, uint32_t tamanio);
void  enviar_escritura_fisica(int stick_socket, int pid, uint32_t dir_fisica,
                               void *datos, uint32_t tamanio);

/* ===========================================================================
 * Nueva memoria disponible  (Kernel Memory → CPU / Scheduler)
 * wire: [ tamanio : uint32_t ][ base_fisica : uint32_t ][ ip : cadena ][ puerto : cadena ]
 * =========================================================================== */
void enviar_nueva_memoria_disponible(int cpu_socket, uint32_t tamanio, uint32_t base_fisica,
                                     const char *ip, const char *puerto);
void recibir_nueva_memoria_disponible(int socket, uint32_t *tamanio_out, uint32_t *base_fisica_out,
                                      char **ip_out, char **puerto_out);

/* ===========================================================================
 * Config de la MMU  (Kernel Memory → CPU, sincrónico tras el HANDSHAKE_CPU)
 * Le informa a la CPU el SEGMENT_MAX_SIZE para que su MMU coincida con memoria.
 * wire: op_code CONFIG_MMU_CPU + [ segment_max_size : uint32_t ]
 * =========================================================================== */
void     enviar_config_mmu_cpu(int cpu_socket, uint32_t segment_max_size);
uint32_t recibir_config_mmu_cpu(int socket);

/* ===========================================================================
 * BSOD
 * wire: sin payload (enviar_ok / recibir_ok)
 * =========================================================================== */
void enviar_memoria_corrupta(int scheduler_socket);
bool recibir_memoria_corrupta_no_bloqueante(int socket);

/* ===========================================================================
 * Memoria liberada  (Kernel Memory → Kernel Scheduler)
 * Notifica que se liberó memoria (eliminación de segmento o finalización de
 * proceso) para que el Scheduler re-evalúe procesos suspendidos.
 * wire: sin payload (enviar_ok / recibir_ok)
 * =========================================================================== */
void enviar_memoria_liberada(int scheduler_socket);

/* ===========================================================================
 * Swap: escritura de bloque
 * wire (solicitar): [ bloque : uint32_t ][ data : size bytes ]
 * wire (respuesta): [ status : int ]
 * =========================================================================== */
void enviar_escritura_bloque_swap(int socket, uint32_t bloque,
                                  void *data, uint32_t size);
void recibir_escritura_bloque_swap(int socket, uint32_t *bloque_out,
                                   void **data_out, uint32_t size);
void enviar_resultado_swap(int socket, int status);
int  recibir_resultado_swap(int socket);

/* ===========================================================================
 * Swap: lectura de bloque
 * wire (solicitar): [ bloque : uint32_t ]
 * wire (respuesta): [ status : int ][ data : size bytes ]  (solo si status == 0)
 * =========================================================================== */
void     enviar_lectura_bloque_swap(int socket, uint32_t bloque);
uint32_t recibir_lectura_bloque_swap(int socket);

void  enviar_bloque_swap(int socket, int status, void *data, uint32_t size);
void *recibir_bloque_swap(int socket, int *status_out, uint32_t size);

/* ===========================================================================
 * IO
 * Pedidos: Kernel Scheduler → módulo IO
 *   wire IO_SLEEP:  [ pid : int ][ tiempo_ms : int ]
 *   wire IO_STDIN:  [ pid : int ][ size : int ]        (cantidad de bytes a leer)
 *   wire IO_STDOUT: [ pid : int ][ datos : size bytes ] (bytes crudos, sin prefijo)
 *
 * Respuestas: módulo IO → Kernel Scheduler (op_code IO_FINALIZADA)
 *   wire fin simple:   [ pid : int ]
 *   wire con datos:    [ pid : int ][ datos : size bytes ]  (resultado de STDIN)
 * =========================================================================== */
void enviar_io_sleep(int io_socket, int pid, int tiempo_ms);
void enviar_io_stdin(int io_socket, int pid, int size);
void enviar_io_stdout(int io_socket, int pid, void *datos, int size);

/* IO_FINALIZADA sin payload de datos (SLEEP / STDOUT). */
void enviar_fin_io(int scheduler_socket, int pid);
int  recibir_fin_io(int socket);

/* IO_FINALIZADA con los bytes leídos por STDIN. */
void  enviar_datos_stdin(int scheduler_socket, int pid, void *datos, int size);
/* Devuelve el buffer de `size` bytes (el caller hace free); deja el pid en *pid_out. */
void *recibir_datos_stdin(int socket, int *pid_out, int size);

/* ===========================================================================
 * Devolver proceso  (CPU → Scheduler)
 * wire: [ pid : int ][ motivo : int ]
 *       si motivo == MOTIVO_SYSCALL:
 *         [ nombre_syscall : cadena ][ cant_params : int ]
 *         [ param_i : cadena ] × cant_params
 * =========================================================================== */
void enviar_devolver_proceso(int socket, int pid,
                              t_motivo_devolucion motivo,
                              const char* nombre_syscall,
                              char** params, int cant_params);

void recibir_devolver_proceso(int socket,
                               int* pid_out,
                               t_motivo_devolucion* motivo_out,
                               char** nombre_out,
                               char*** params_out,
                               int* cant_params_out);

/* ===========================================================================
 * Segmentos (Scheduler → Kernel Memory)
 *
 * SOLICITAR_SEGMENTO:
 *   wire (solicitar): [ pid : int ][ id_segmento : uint32_t ][ tamanio : uint32_t ]
 *   wire (respuesta): [ id_segmento : uint32_t ][ base : uint32_t ][ limite : uint32_t ]
 *   o SEGMENTATION_FAULT si no hay espacio
 *
 * ELIMINAR_SEGMENTO:
 *   wire (solicitar): [ pid : int ][ id_segmento : uint32_t ]
 *   wire (ACK): recibir_ok(socket)
 *
 * FINALIZAR_PROCESO:
 *   wire (solicitar): [ pid : int ]
 *   wire (ACK): recibir_ok(socket)
 * =========================================================================== */
void enviar_solicitar_segmento(int memory_socket, int pid,
                                uint32_t id_segmento, uint32_t tamanio);
void recibir_respuesta_segmento(int socket,
                                 uint32_t* id_out, uint32_t* base_out,
                                 uint32_t* limite_out);

void enviar_eliminar_segmento(int memory_socket, int pid, uint32_t id_segmento);

void enviar_finalizar_proceso_memory(int memory_socket, int pid);

#endif /* MENSAJES_H_ */