#ifndef PLANIFICADOR_MEDIANO_PLAZO_H_
#define PLANIFICADOR_MEDIANO_PLAZO_H_

#include "globalesKernelScheduler.h"
#include <commons/collections/list.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

/* ======================================================================
 * Planificador de Mediano Plazo (PMP)
 * ======================================================================
 *
 * Responsabilidades:
 *  1. BLOCK → SUSP_BLOCK: por cada proceso que entra a BLOCK, arranca un
 *     timer de SUSPENSION_TIMEOUT ms.  Si al vencer el proceso sigue en
 *     BLOCK, lo suspende (segmentos RAM → SWAP) y pasa al estado SUSP_BLOCK.
 *
 *  2. SUSP_READY → READY: cuando se libera memoria (eliminación de segmento,
 *     finalización de proceso, nuevo Memory Stick o compactación), recorre
 *     todos los procesos en SUSP_READY ordenados por prioridad dinámica (asc)
 *     y luego por tiempo de suspensión (oldest-first), des-suspendiendo los
 *     que tienen espacio suficiente sin disparar compactación.
 *
 * Triggers externos que deben llamar notificar_reevaluar_suspendidos():
 *   - handle_memoria_liberada()       en memory_protocol.c
 *   - handle_nueva_memoria_disponible() en memory_protocol.c
 *   - manejar_compactacion_fin()      en utils_kernel_scheduler.c
 *   - desbloquear_proceso_por_io()    en colaIO.c (cuando → SUSP_READY)
 *
 * Triggers externos que deben llamar notificar_proceso_en_block():
 *   - encolar_io_request()            en colaIO.c
 *   - manejar_syscall_mutex_lock()    en utils_kernel_scheduler.c
 * ====================================================================== */

typedef struct {
    t_pcb*  pcb;
    time_t  tiempo_suspension;  // para desempate: el más viejo se des-suspende primero
} t_proceso_suspendido;

/* Lista global de procesos suspendidos (SUSP_BLOCK y SUSP_READY) */
extern t_list*          lista_suspendidos;
extern pthread_mutex_t  mutex_suspendidos;

/* Semáforo que el loop principal espera; se postea cuando hay memoria libre */
extern sem_t            sem_reevaluar_suspendidos;

/* --- API pública --------------------------------------------------- */

/* Inicializa estructuras y lanza el thread del PMP */
void iniciar_planificador_mediano_plazo(void);

/* Loop del PMP (corre en su propio thread) */
void planificadorMedianoPlazo(void);
void* _planificador_mediano_plazo_thread(void* arg);

/* Llamar cuando un proceso entra a BLOCK: lanza el timer de suspensión */
void notificar_proceso_en_block(t_pcb* pcb, uint32_t epoch);

/* Llamar cuando se libera/amplía memoria para re-evaluar SUSP_READY */
void notificar_reevaluar_suspendidos(void);

/* Gestión de la lista de suspendidos */
void agregar_a_lista_suspendidos(t_pcb* pcb);
void remover_de_lista_suspendidos(int pid);

#endif /* PLANIFICADOR_MEDIANO_PLAZO_H_ */