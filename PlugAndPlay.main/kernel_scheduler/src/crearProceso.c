#include "crearProceso.h"
#include "scheduler_conexiones.h"
#include "memory_protocol.h"

#include <utils/sockets.h>
#include <utils/mensajes.h>
#include <commons/collections/list.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


int _contador_pids = 0;
pthread_mutex_t _mutex_pids = PTHREAD_MUTEX_INITIALIZER;


// este lo moveria tambien a utils_kernel_scheduler.c porque es una función de conexión a memory, no de creación de procesos
int generar_pid(void) {
    pthread_mutex_lock(&_mutex_pids);
    // El proceso inicial debe ser el PID 0 del sistema (enunciado, Kernel
    // Scheduler): el primer PID entregado es 0 y luego se incrementa.
    int nuevo_pid = _contador_pids;
    _contador_pids++;
    pthread_mutex_unlock(&_mutex_pids);
    return nuevo_pid;
}

// ESTE LO MOVERIA A UTILS KERNEL SCHEDULER, PORQUE ES UNA FUNCION DE CONEXION A MEMORY, NO DE CREACION DE PROCESOS
bool enviar_creacion_proceso_a_memory(int pid, char* path) {
    extern int memory_socket;

    if (memory_socket == -1) {
        log_error(logger, "Socket de Memory no disponible para crear proceso");
        return false;
    }

    pthread_mutex_lock(&mutex_memory_socket);

    // Enviar solicitución de creación de proceso sin esperar respuesta
    // para evitar deadlock. El scheduler no necesita bloquearse en memory.
    enviar_crear_proceso(memory_socket, pid, path);

    op_code codigo = recibir_respuesta_de_memoria(memory_socket);
    if ((int)codigo == -1) {
        pthread_mutex_unlock(&mutex_memory_socket);
        log_error(logger, "Kernel Memory desconectado esperando ACK de CREAR_PROCESO PID:%d", pid);
        return false;
    }

    if (codigo != CREAR_PROCESO) {
        pthread_mutex_unlock(&mutex_memory_socket);
        log_error(logger,
            "ACK inesperado de Kernel Memory para CREAR_PROCESO PID:%d: opcode=%d",
            pid,
            codigo);
        return false;
    }

    recibir_ok(memory_socket);

    pthread_mutex_unlock(&mutex_memory_socket);

    log_debug(logger, "Notificada creación de proceso PID:%d a Memory", pid);
    return true;
}

t_pcb* crear_proceso(char* path, int prioridad) {
    if (path == NULL || path[0] == '\0') {
        log_error(logger, "No se puede crear proceso: path inválido");
        return NULL;
    }

    t_pcb* pcb = malloc(sizeof(t_pcb));
    if (!pcb) {
        log_error(logger, "Error al reservar memoria para PCB");
        return NULL;
    }


    pcb->pid = generar_pid();

   
    pcb->prioridad = prioridad;
    pcb->prioridad_dinamica = prioridad;
    pcb->estado = NEW;

    
    pcb->tiempo_llegada = time(NULL);
    pcb->tiempo_inicio_ejecucion = 0;

    pcb->quantum_restante = 0;
    pcb->tiempo_total_cpu = 0;
    pcb->conteo_context_switch = 0;
    pcb->mutex_esperando = NULL;
    pcb->suspendido_en_swap = false;
    pcb->block_epoch = 0;

    /* Campos del Planificador de Mediano Plazo */
    pcb->total_memoria   = 0;
    pcb->segmentos_info  = list_create();

    pcb->stdin_pendiente      = NULL;
    pcb->stdin_pendiente_dir  = 0;
    pcb->stdin_pendiente_size = 0;

    registrar_pcb(pcb);

    log_info(logger, "## (%d) Se crea el proceso - Estado: NEW", pcb->pid);
    log_debug(logger, "## (%d) Script asociado: %s", pcb->pid, path);

    // Avisar a Memory de la creación del proceso PRIMERO.
    // Si falla, no se descarta el PCB: el planificador ya puede avanzar.
    if (!enviar_creacion_proceso_a_memory(pcb->pid, path)) {
        log_error(logger, "Fallo al notificar creación de proceso PID:%d a Memory", pcb->pid);
    }

    // Encolar en NEW y notificar al planificador (no bloquear por Memory)
    pthread_mutex_lock(&mutex_new);
    if (cola_procesos_new != NULL) {
        queue_push(cola_procesos_new, pcb);
        log_debug(logger, "Proceso PID:%d encolado en NEW", pcb->pid);
        pthread_mutex_unlock(&mutex_new);

        // Avisar al planificador de que hay un nuevo proceso en NEW
        sem_post(&sem_new);
    } else {
        log_error(logger, "Cola NEW no inicializada. Proceso PID:%d se pierde", pcb->pid);
        pthread_mutex_unlock(&mutex_new);
        free(pcb);
        return NULL;
    }

    return pcb;
}

t_pcb* crear_proceso_inicial(char* path) {
    if (path == NULL || path[0] == '\0') {
        log_error(logger, "No se puede crear proceso inicial: path inválido");
        return NULL;
    }

    log_debug(logger, "=== CREANDO PROCESO INICIAL ===");
    log_debug(logger, "Path: %s", path);

    // Crear con máxima prioridad (0)
    t_pcb* proceso_inicial = crear_proceso(path, 0);

    if (proceso_inicial != NULL) {
        log_debug(logger, "Proceso inicial creado exitosamente: PID=%d", proceso_inicial->pid);
    } else {
        log_error(logger, "Falló la creación del proceso inicial");
    }

    return proceso_inicial;
}

