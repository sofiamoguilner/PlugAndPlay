
#include "planificadorLargoPlazo.h"
#include "globalesKernelScheduler.h"
#include "scheduler_conexiones.h"
#include "scheduler_config.h"
#include "utils_kernel_scheduler.h"
#include <commons/collections/queue.h>
#include <utils/utils.h>
#include <utils/sockets.h>
#include <utils/mensajes.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <semaphore.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>


// monitor_bsod_thread eliminado: la detección de MEMORIA_CORRUPTA_BSOD ahora
// se hace inline desde memory_protocol.c::recibir_respuesta_de_memoria,
// drenando notificaciones bajo mutex_memory_socket sin race contra los
// otros lectores del socket.


void planificadorLargoPlazo(void) {
    log_debug(logger, "Planificador a Largo Plazo iniciado");

    while (!sistema_corrupto) {
        // Esperar a que haya procesos en NEW
       sem_wait(&sem_new);

        pthread_mutex_lock(&mutex_new);
        t_pcb* proceso = (t_pcb*) queue_pop(cola_procesos_new);
        pthread_mutex_unlock(&mutex_new);

        if (proceso == NULL) continue;

        proceso->estado = READY;
        proceso->tiempo_llegada = time(NULL);

        // Si hay colas multinivel, encolarlo en la cola de su prioridad
        if (cantidad_niveles_prioridad > 0) {
            log_info(logger, "## (%d) Pasa del estado NEW al estado READY", proceso->pid);
            encolar_en_ready_por_prioridad(proceso);
        } else {
            // Fallback a cola única
            pthread_mutex_lock(&mutex_ready);
            queue_push(cola_procesos_ready, proceso);
            pthread_mutex_unlock(&mutex_ready);
            log_info(logger, "## (%d) Pasa del estado NEW al estado READY", proceso->pid);
            sem_post(&sem_ready);
        }
    }

    log_debug(logger, "Planificador a Largo Plazo terminado");
    
}

void* _planificador_largo_plazo_thread(void* arg) {
    planificadorLargoPlazo();
    return NULL;
}

void iniciar_planificador_largo_plazo(void) {
    // Crear thread del planificador a largo plazo
    pthread_t thread_plp;
    if (pthread_create(&thread_plp, NULL, _planificador_largo_plazo_thread, NULL) != 0) {
        log_error(logger, "Error al crear thread del Planificador a Largo Plazo");
        return;
    }
    pthread_detach(thread_plp);
    log_debug(logger, "Thread Planificador a Largo Plazo creado");

    // BSOD: pendiente de canal dedicado con Kernel Memory.
    // No spawnear monitor sobre memory_socket: comparte stream con request/response y
    // consume bytes destinados a otras operaciones, desincronizando el protocolo.
}


void manejar_bsod(const char* motivo) {
    pthread_mutex_lock(&mutex_bsod);
    
    if (sistema_corrupto) {
        pthread_mutex_unlock(&mutex_bsod);
        return; // Ya se está manejando BSOD
    }
    
    sistema_corrupto = 1;
    pthread_mutex_unlock(&mutex_bsod);
    
    log_error(logger, "╔════════════════════════════════════════╗");
    log_error(logger, "║      BLUE SCREEN OF DEATH - BSOD      ║");
    log_error(logger, "║   CORRUPCIÓN DE MEMORIA DETECTADA     ║");
    log_error(logger, "╚════════════════════════════════════════╝");
    log_error(logger, "Motivo: %s", motivo);
    log_error(logger, "Finalizando todo el sistema...");

    // Marcar todos los procesos vivos como EXIT antes de morir. Sin esto, los
    // PCBs que estaban en READY/EXEC/BLOCK/SUSP cuando llegó el BSOD nunca
    // quedan registrados en EXIT y el validador los marca como huérfanos.
    pthread_mutex_lock(&mutex_todos_los_procesos);
    if (todos_los_procesos != NULL) {
        for (int i = 0; i < list_size(todos_los_procesos); i++) {
            t_pcb* p = list_get(todos_los_procesos, i);
            if (p != NULL && p->estado != EXIT_STATE) {
                log_info(logger, "## (%d) Pasa del estado %s al estado EXIT por BSOD",
                         p->pid, estado_a_string(p->estado));
                log_info(logger, "## (%d) finalizó su ejecución con motivo de BSOD", p->pid);
                p->estado = EXIT_STATE;
            }
        }
    }
    pthread_mutex_unlock(&mutex_todos_los_procesos);

    // Liberar memoria de colas
    pthread_mutex_lock(&mutex_new);
    if (cola_procesos_new != NULL) {
        queue_destroy(cola_procesos_new);
        cola_procesos_new = NULL;
    }
    pthread_mutex_unlock(&mutex_new);

    pthread_mutex_lock(&mutex_ready);
    if (cola_procesos_ready != NULL) {
        queue_destroy(cola_procesos_ready);
        cola_procesos_ready = NULL;
    }
    pthread_mutex_unlock(&mutex_ready);

    // Destruir colas multinivel
    pthread_mutex_lock(&mutex_colas_prioridad);
    for (int i = 0; i < cantidad_niveles_prioridad; i++) {
        if (colas_ready_por_prioridad[i] != NULL) {
            queue_destroy(colas_ready_por_prioridad[i]);
            colas_ready_por_prioridad[i] = NULL;
        }
    }
    cantidad_niveles_prioridad = 0;
    pthread_mutex_unlock(&mutex_colas_prioridad);
    
    // Destruir semáforo de procesos en NEW
    if (sem_destroy(&sem_new) != 0) {
        log_error(logger, "Error al destruir semáforo sem_new durante BSOD");
    }
    
    log_debug(logger, "Recursos liberados. Scheduler terminando...");
    
    // Terminar el programa
    exit(EXIT_FAILURE);
    }
