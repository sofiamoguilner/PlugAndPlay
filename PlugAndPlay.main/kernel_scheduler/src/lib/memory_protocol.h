#ifndef MEMORY_PROTOCOL_H_
#define MEMORY_PROTOCOL_H_

#include <utils/utils.h>

/* ============================================================================
 * Demultiplexador del protocolo Kernel Scheduler ↔ Kernel Memory
 * ============================================================================
 *
 * # Problema que resuelve
 *
 * Scheduler y Memory comparten un único socket TCP por el que viajan DOS clases
 * de mensajes:
 *
 *   (A) Request/response sincrónico iniciado por el scheduler:
 *       CREAR_PROCESO, FINALIZAR_PROCESO, SOLICITAR_SEGMENTO, ELIMINAR_SEGMENTO,
 *       LECTURA_MEMORIA, ESCRITURA_MEMORIA, SUSPENDER_PROCESO, DES_SUSPENDER_PROCESO.
 *
 *   (B) Notificaciones push que memory empuja sin que el scheduler las haya pedido:
 *       MEMORIA_LIBERADA, MEMORIA_CORRUPTA_BSOD, INICIAR_COMPACTACION,
 *       FINALIZAR_COMPACTACION, NUEVA_MEMORIA_DISPONIBLE.
 *
 * Si el scheduler usa `recibir_codigo_operacion(memory_socket)` directamente,
 * una notificación que llega entremedio de un request/response queda
 * buffereada en el socket y el próximo recv() la lee como si fuera la
 * respuesta esperada. Resultado: cascada de "ACK inesperado", corrupción del
 * stream y caídas de conexión.
 *
 * # Cómo usarlo
 *
 * En cualquier sección crítica del scheduler que mande algo a memory y espere
 * respuesta, usar `recibir_respuesta_de_memoria(memory_socket)` en lugar de
 * `recibir_codigo_operacion(memory_socket)`. Después se leen los campos del
 * payload como antes (recibir_ok, recibir_respuesta_segmento, recibir_paquete,
 * etc.). Ejemplo:
 *
 *     pthread_mutex_lock(&mutex_memory_socket);
 *     enviar_solicitar_segmento(memory_socket, pid, id_seg, tamanio);
 *     op_code ack = recibir_respuesta_de_memoria(memory_socket);
 *     if (ack == SEGMENTATION_FAULT) {
 *         recibir_ok(memory_socket);
 *         pthread_mutex_unlock(&mutex_memory_socket);
 *         // ... manejar segfault
 *     }
 *     uint32_t id, base, limite;
 *     recibir_respuesta_segmento(memory_socket, &id, &base, &limite);
 *     pthread_mutex_unlock(&mutex_memory_socket);
 *
 * # Precondición
 *
 * El caller DEBE tener tomado `mutex_memory_socket` cuando invoca esta función.
 * El mutex garantiza que solo un thread del scheduler está leyendo del socket
 * a la vez; el demux drena las notificaciones que aparezcan ANTES de la
 * respuesta real para ese caller.
 *
 * # Cómo agregar un nuevo opcode de notificación
 *
 * 1. Agregar el `case` en `despachar_si_es_notificacion()` en memory_protocol.c.
 * 2. Implementar el handler `handle_<nombre>(int socket)` que:
 *      - Drena exactamente el payload del wire format declarado por el sender
 *        en utils/src/utils/mensajes.c (sino `validar_buffer` aborta).
 *      - Hace el trabajo síncrono que corresponda.
 *      - Si el protocolo requiere ack, envía `enviar_ok(...)` por el mismo socket.
 *
 * REGLA DE ORO de los handlers: corren con `mutex_memory_socket` tomado por el
 * caller original. NO deben re-tomarlo ni bloquearse esperando algo que
 * indirectamente lo necesite (ej. respuesta de una CPU que a su vez le pida
 * algo a memory).
 *
 * # Estado actual de los handlers
 *
 *   MEMORIA_CORRUPTA_BSOD      → llama manejar_bsod() (termina el proceso).
 *   INICIAR_COMPACTACION       → suspende el timeout del socket, desaloja CPUs
 *                                y responde ok a memory.
 *   FINALIZAR_COMPACTACION     → restaura el timeout y reanuda el planificador.
 *   MEMORIA_LIBERADA           → notificar_reevaluar_suspendidos(): despierta al
 *                                planificador de mediano plazo para reintentar
 *                                los procesos en SUSP_READY (enunciado pág. 10).
 *   NUEVA_MEMORIA_DISPONIBLE   → loguea el nuevo Memory Stick y también dispara
 *                                notificar_reevaluar_suspendidos().
 *
 * Ambas re-evaluaciones se hacen con un sem_post, nunca esperando al mediano
 * plazo: cumplir la REGLA DE ORO es lo que hace que eso sea seguro.
 *
 * # Retorno
 *
 * Devuelve el opcode de RESPUESTA (no notificación) leído del socket, o
 * (op_code)-1 si el socket fue cerrado o hubo error de lectura. El caller debe
 * propagar ese error como antes.
 * ============================================================================ */
op_code recibir_respuesta_de_memoria(int socket);

#endif // MEMORY_PROTOCOL_H_
