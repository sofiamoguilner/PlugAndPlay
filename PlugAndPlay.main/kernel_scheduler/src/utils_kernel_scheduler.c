#include "utils_kernel_scheduler.h"
#include "globalesKernelScheduler.h"
#include "crearProceso.h"
#include "memory_protocol.h"
#include "planificadorMedianoPlazo.h"
#include <utils/sockets.h>
#include <utils/mensajes.h>
#include <utils/serializacion.h>
#include <semaphore.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "colaIO.h"

// Imports de globales (definidos en scheduler_conexiones.c o main.c)
extern t_log* logger;
extern int memory_socket;
extern int server_socket;

// Variable global de conexiones activas
t_conexiones_activas scheduler_connections = {
    .cpu_count = 0,
    .io_count = 0
};

// Struct de argumento para thread CPU
typedef struct {
    int socket;
    int cpu_id;
} t_cpu_thread_arg;

static void liberar_parametros_syscall(char** params, int cant_params)
{
    if (params == NULL) {
        return;
    }

    for (int i = 0; i < cant_params; i++) {
        free(params[i]);
    }
    free(params);
}

static t_pcb* retirar_proceso_de_cpu(int cpu_id, int pid)
{
    t_pcb* proceso = NULL;

    pthread_mutex_lock(&scheduler_connections.lock);
    for (int i = 0; i < scheduler_connections.cpu_count; i++) {
        if (scheduler_connections.cpus[i].cpu_id == cpu_id) {
            proceso = scheduler_connections.cpus[i].proceso_actual;
            scheduler_connections.cpus[i].proceso_actual = NULL;
            scheduler_connections.cpus[i].timer_activo = false;
            scheduler_connections.cpus[i].disponible = true;
            break;
        }
    }
    pthread_mutex_unlock(&scheduler_connections.lock);

    sem_post(&sem_cpu_disponible);

    if (proceso != NULL && proceso->pid != pid) {
        log_warning(logger,
            "CPU %d devolvio PID %d pero se esperaba PID %d",
            cpu_id,
            pid,
            proceso->pid);
    }

    return proceso;
}

/* Variante para syscalls de memoria (enunciado v1.1: MEM_ALLOC/MEM_FREE deben
 * volver a la CPU que hizo la llamada): retira el proceso pero deja la CPU
 * RESERVADA (disponible = false, sin sem_post). El PCP no puede despacharle
 * otro proceso, y tanto el chequeo de QUEUE_PREEMPTION como el desalojo por
 * compactación la saltean porque proceso_actual queda en NULL. */
static t_pcb* retirar_proceso_de_cpu_reservando(int cpu_id, int pid)
{
    t_pcb* proceso = NULL;

    pthread_mutex_lock(&scheduler_connections.lock);
    for (int i = 0; i < scheduler_connections.cpu_count; i++) {
        if (scheduler_connections.cpus[i].cpu_id == cpu_id) {
            proceso = scheduler_connections.cpus[i].proceso_actual;
            scheduler_connections.cpus[i].proceso_actual = NULL;
            scheduler_connections.cpus[i].timer_activo = false;
            break;
        }
    }
    pthread_mutex_unlock(&scheduler_connections.lock);

    if (proceso != NULL && proceso->pid != pid) {
        log_warning(logger,
            "CPU %d devolvio PID %d pero se esperaba PID %d",
            cpu_id,
            pid,
            proceso->pid);
    }

    return proceso;
}

/* Libera una CPU reservada por retirar_proceso_de_cpu_reservando cuando el
 * proceso NO vuelve a ella (la syscall falló y el proceso finalizó). */
static void liberar_cpu_reservada(int cpu_id)
{
    pthread_mutex_lock(&scheduler_connections.lock);
    for (int i = 0; i < scheduler_connections.cpu_count; i++) {
        if (scheduler_connections.cpus[i].cpu_id == cpu_id) {
            scheduler_connections.cpus[i].disponible = true;
            break;
        }
    }
    pthread_mutex_unlock(&scheduler_connections.lock);
    sem_post(&sem_cpu_disponible);
}

/* Devuelve el proceso a la CPU reservada que hizo la syscall de memoria.
 * El proceso nunca salió de EXEC, así que no hay logs de cambio de estado.
 * Si corre bajo RR/CMN-RR (quantum_restante > 0, seteado por el PCP en el
 * dispatch) el quantum se reinicia completo: el retorno cuenta como un nuevo
 * dispatch. Devuelve false si la CPU se desconectó durante la syscall. */
static bool devolver_proceso_a_misma_cpu(int cpu_id, t_pcb* proceso)
{
    int cpu_socket = -1;

    pthread_mutex_lock(&scheduler_connections.lock);
    for (int i = 0; i < scheduler_connections.cpu_count; i++) {
        if (scheduler_connections.cpus[i].cpu_id == cpu_id) {
            if (scheduler_connections.cpus[i].socket != -1) {
                scheduler_connections.cpus[i].proceso_actual = proceso;
                cpu_socket = scheduler_connections.cpus[i].socket;
            }
            break;
        }
    }
    pthread_mutex_unlock(&scheduler_connections.lock);

    if (cpu_socket == -1) {
        return false;
    }

    enviar_pid(cpu_socket, proceso->pid);
    if (proceso->quantum_restante > 0) {
        iniciar_quantum_timer(cpu_id, proceso->pid, proceso->quantum_restante);
    }
    log_debug(logger,
        "Proceso PID:%d devuelto a CPU %d tras syscall de memoria",
        proceso->pid, cpu_id);
    return true;
}

void encolar_en_ready_y_notificar(t_pcb* proceso)
{
    if (proceso == NULL) {
        return;
    }

    proceso->estado = READY;

    // Si hay colas multinivel, usar la cola por prioridad
    if (cantidad_niveles_prioridad > 0) {
        reencolar_proceso_ready_final(proceso, proceso->prioridad);
    } else {
        // Fallback a cola única
        pthread_mutex_lock(&mutex_ready);
        if (cola_procesos_ready != NULL) {
            queue_push(cola_procesos_ready, proceso);
            sem_post(&sem_ready);
        }
        pthread_mutex_unlock(&mutex_ready);
    }
}

static void manejar_syscall_init_proc(int pid_origen, int cant_params, char** params)
{
    if (cant_params < 2 || params == NULL) {
        log_warning(logger,
            "PID %d devolvio INIT_PROC con parametros invalidos (cant=%d)",
            pid_origen,
            cant_params);
        return;
    }

    char* path = params[0];
    int prioridad = atoi(params[1]);

    t_pcb* nuevo = crear_proceso(path, prioridad);
    if (nuevo == NULL) {
        log_error(logger,
            "PID %d: fallo al crear proceso por INIT_PROC (path=%s, prioridad=%d)",
            pid_origen,
            path,
            prioridad);
        return;
    }

    log_debug(logger,
        "PID %d solicito INIT_PROC -> nuevo PID %d (path=%s, prioridad=%d)",
        pid_origen,
        nuevo->pid,
        path,
        prioridad);
}

void manejar_syscall_mutex_create(t_pcb* proceso, char** params, int cant_params);
bool manejar_syscall_mutex_lock(t_pcb* proceso, char** params, int cant_params);
void manejar_syscall_mutex_unlock(t_pcb* proceso, char** params, int cant_params);

// Notifica a Kernel Memory el fin de un proceso para liberar contexto/instrucciones/segmentos.
// Sigue el patrón send→recv_opcode→recv_ok bajo mutex_memory_socket.
void notificar_fin_a_memoria(int pid)
{
    if (memory_socket < 0) return;
    pthread_mutex_lock(&mutex_memory_socket);
    enviar_finalizar_proceso_memory(memory_socket, pid);
    op_code ack = recibir_respuesta_de_memoria(memory_socket);
    if ((int)ack != -1) recibir_ok(memory_socket);
    pthread_mutex_unlock(&mutex_memory_socket);
}

/* cpu_origen: CPU que hizo la syscall. Solo se usa en MEM_ALLOC/MEM_FREE, que
 * reservaron la CPU y deben devolverle el mismo proceso (enunciado v1.1) o
 * liberarla si el proceso finaliza por error. */
static void manejar_syscall_io(t_pcb* proceso, const char* nombre_syscall, char** params, int cant_params, int cpu_origen)
{
    if (proceso == NULL || nombre_syscall == NULL) return;
 
    int pid = proceso->pid;
 
    log_info(logger, "## (%d) - Solicitó syscall: %s", pid, nombre_syscall);
 
    // SLEEP <tiempo_ms>  (script form: single param, no interface name)
    if (strcmp(nombre_syscall, "SLEEP") == 0) {
        if (cant_params < 1) {
            log_error(logger, "PID %d: SLEEP sin parámetros", pid);
            encolar_en_ready_y_notificar(proceso);
            return;
        }

        int tiempo_ms = atoi(params[0]);

        t_interfaz_io* io = buscar_interfaz_io_por_tipo(IO_TYPE_SLEEP);
        if (io == NULL) {
            log_error(logger, "PID %d: SLEEP sin interfaz IO tipo SLEEP conectada", pid);
            encolar_en_ready_y_notificar(proceso);
            return;
        }

        t_io_request* req = crear_io_request_sleep(proceso, tiempo_ms);
        encolar_io_request(io, req);
        return;
    }

    // IO_SLEEP: params[0] = nombre_io, params[1] = tiempo_ms
    if (strcmp(nombre_syscall, "IO_SLEEP") == 0) {
        if (cant_params < 2) {
            log_error(logger, "PID %d: IO_SLEEP sin parámetros suficientes", pid);
            encolar_en_ready_y_notificar(proceso);
            return;
        }
 
        char* nombre_io = params[0];
        int   tiempo_ms = atoi(params[1]);
 
        t_interfaz_io* io = buscar_interfaz_io(nombre_io);
        if (io == NULL) {
            log_error(logger,
                "PID %d: IO_SLEEP requiere interfaz '%s' pero no está conectada",
                pid, nombre_io);
            encolar_en_ready_y_notificar(proceso);
            return;
        }
 
        t_io_request* req = crear_io_request_sleep(proceso, tiempo_ms);
        encolar_io_request(io, req);
        // El proc queda en BLOCK, el worker de la IO lo desbloquea al terminar
        return;
    }
 
    // STDIN: params[0] = dir_logica, params[1] = size (register values resolved by CPU)
    if (strcmp(nombre_syscall, "STDIN") == 0) {
      if (cant_params < 2) {
        log_error(logger, "PID %d: STDIN sin parámetros suficientes", pid);
        encolar_en_ready_y_notificar(proceso);
        return;
     }

     int   dir_logica = atoi(params[0]);
     int   size       = atoi(params[1]);

     t_interfaz_io* io = buscar_interfaz_io_por_tipo(IO_TYPE_STDIN);
     if (io == NULL) {
        log_error(logger,
            "PID %d: STDIN sin interfaz IO tipo STDIN conectada",
            pid);
        encolar_en_ready_y_notificar(proceso);
        return;
     }

     t_io_request* req = crear_io_request_stdin(proceso, size, dir_logica);
     encolar_io_request(io, req);
     return;
 }

  // STDOUT: params[0] = dir_logica, params[1] = size (register values resolved by CPU)
  if (strcmp(nombre_syscall, "STDOUT") == 0) {
     if (cant_params < 2) {
         log_error(logger, "PID %d: STDOUT sin parámetros suficientes", pid);
         encolar_en_ready_y_notificar(proceso);
         return;
     }

     int   dir_logica = atoi(params[0]);
     int   size       = atoi(params[1]);

     t_interfaz_io* io = buscar_interfaz_io_por_tipo(IO_TYPE_STDOUT);
     if (io == NULL) {
            log_error(logger,
            "PID %d: STDOUT sin interfaz IO tipo STDOUT conectada",
            pid);
            encolar_en_ready_y_notificar(proceso);
        return;
    }

    /* Leer los bytes AHORA, con el proceso todavía en RAM (recién salió de
     * EXEC y aún no entró a BLOCK, así que no puede estar suspendido). Si la
     * lectura se hiciera al despachar la IO, el proceso podría llevar rato
     * esperando en la cola, haber sido suspendido por el PMP, y sus segmentos
     * ya no estarían en RAM. */
    char* buffer = NULL;
    pthread_mutex_lock(&mutex_memory_socket);
    enviar_lectura_memoria(memory_socket, pid, dir_logica, size);
    op_code ack = recibir_respuesta_de_memoria(memory_socket);
    if (ack == SEGMENTATION_FAULT) {
        recibir_ok(memory_socket);
        pthread_mutex_unlock(&mutex_memory_socket);
        log_warning(logger,
            "## (%d) SEG_FAULT en STDOUT (dir=%d, size=%d) — finalizando por ERROR",
            pid, dir_logica, size);
        finalizar_pcb_por_seg_fault(proceso);
        return;
    }
    if ((int)ack != -1) {
        // Memory mandó [size_t][data]; usar el helper que drena size_t,
        // no recibir_paquete (lee size como int y deja 4 bytes en el wire
        // → la próxima op de memoria abortaría en validar_buffer).
        buffer = recibir_datos_memoria(memory_socket, size);
    }
    pthread_mutex_unlock(&mutex_memory_socket);

    if (buffer == NULL) {
        log_error(logger,
            "STDOUT PID %d: no se pudo leer de Memory (dir=%d, size=%d)",
            pid, dir_logica, size);
        encolar_en_ready_y_notificar(proceso);
        return;
    }

    t_io_request* req = crear_io_request_stdout(proceso, dir_logica, size, buffer);
    encolar_io_request(io, req);
    return;
 }
 
    if (strcmp(nombre_syscall, "MEM_ALLOC") == 0) {
        if (cant_params < 2) {
            log_error(logger, "PID %d: MEM_ALLOC sin parámetros suficientes", proceso->pid);
            proceso->estado = EXIT_STATE;
            log_warning(logger, "## (%d) Pasa del estado EXEC al estado EXIT por ERROR (MEM_ALLOC inválido)", proceso->pid);
            log_info(logger, "## (%d) finalizó su ejecución con motivo de ERROR", proceso->pid);
            liberar_mutexes_de_proceso(proceso->pid);
            notificar_fin_a_memoria(proceso->pid);
            eliminar_pcb(proceso->pid);
            free(proceso);
            liberar_cpu_reservada(cpu_origen);
            return;
        }
        uint32_t id_segmento = (uint32_t)atoi(params[0]);
        uint32_t tamanio     = (uint32_t)atoi(params[1]);

        pthread_mutex_lock(&mutex_memory_socket);
        enviar_solicitar_segmento(memory_socket, proceso->pid, id_segmento, tamanio);
        op_code ack = recibir_respuesta_de_memoria(memory_socket);
        if ((int)ack == -1) {
            // Memoria no respondió (timeout/desconexión). Soltamos el mutex SIEMPRE
            // para no wedgear el canal y finalizamos el proceso por error en vez de
            // leer basura con recibir_respuesta_segmento.
            pthread_mutex_unlock(&mutex_memory_socket);
            log_error(logger,
                "## (%d) MEM_ALLOC sin respuesta de Memoria (seg=%u) — finalizando por ERROR",
                proceso->pid, id_segmento);
            proceso->estado = EXIT_STATE;
            log_info(logger, "## (%d) finalizó su ejecución con motivo de ERROR", proceso->pid);
            liberar_mutexes_de_proceso(proceso->pid);
            notificar_fin_a_memoria(proceso->pid);
            eliminar_pcb(proceso->pid);
            free(proceso);
            liberar_cpu_reservada(cpu_origen);
            return;
        }
        if (ack == SEGMENTATION_FAULT) {
            recibir_ok(memory_socket);
            pthread_mutex_unlock(&mutex_memory_socket);
            log_warning(logger,
                "## (%d) MEM_ALLOC sin espacio (seg=%u size=%u) — finalizando por ERROR",
                proceso->pid, id_segmento, tamanio);
            proceso->estado = EXIT_STATE;
            log_warning(logger, "## (%d) Pasa del estado EXEC al estado EXIT por ERROR", proceso->pid);
            log_info(logger, "## (%d) finalizó su ejecución con motivo de ERROR", proceso->pid);
            liberar_mutexes_de_proceso(proceso->pid);
            notificar_fin_a_memoria(proceso->pid);
            eliminar_pcb(proceso->pid);
            free(proceso);
            liberar_cpu_reservada(cpu_origen);
            return;
        }
        uint32_t id_resp, base_resp, limite_resp;
        recibir_respuesta_segmento(memory_socket, &id_resp, &base_resp, &limite_resp);
        pthread_mutex_unlock(&mutex_memory_socket);

        log_debug(logger, "## (%d) MEM_ALLOC OK seg=%u base=%u size=%u",
                 proceso->pid, id_resp, base_resp, limite_resp);

        /* Registrar segmento para el PMP */
        t_seg_info* seg = malloc(sizeof(t_seg_info));
        seg->id      = id_resp;
        seg->tamanio = limite_resp;
        list_add(proceso->segmentos_info, seg);
        proceso->total_memoria += limite_resp;

        /* Enunciado v1.1: el proceso vuelve a la CPU que hizo la syscall,
         * sin pasar por READY. Solo si la CPU se desconectó en el medio se
         * replanifica por el camino normal. */
        if (!devolver_proceso_a_misma_cpu(cpu_origen, proceso)) {
            log_warning(logger,
                "CPU %d se desconectó durante MEM_ALLOC — PID %d se replanifica por READY",
                cpu_origen, proceso->pid);
            encolar_en_ready_y_notificar(proceso);
        }
        return;
    }

    if (strcmp(nombre_syscall, "MEM_FREE") == 0) {
        if (cant_params < 1) {
            log_error(logger, "PID %d: MEM_FREE sin parámetros suficientes", proceso->pid);
            proceso->estado = EXIT_STATE;
            log_warning(logger, "## (%d) Pasa del estado EXEC al estado EXIT por ERROR (MEM_FREE inválido)", proceso->pid);
            log_info(logger, "## (%d) finalizó su ejecución con motivo de ERROR", proceso->pid);
            liberar_mutexes_de_proceso(proceso->pid);
            notificar_fin_a_memoria(proceso->pid);
            eliminar_pcb(proceso->pid);
            free(proceso);
            liberar_cpu_reservada(cpu_origen);
            return;
        }
        uint32_t id_segmento = (uint32_t)atoi(params[0]);

        pthread_mutex_lock(&mutex_memory_socket);
        enviar_eliminar_segmento(memory_socket, proceso->pid, id_segmento);
        op_code ack = recibir_respuesta_de_memoria(memory_socket);
        if ((int)ack != -1) recibir_ok(memory_socket);
        pthread_mutex_unlock(&mutex_memory_socket);

        /* Actualizar tracking del PMP */
        for (int i = 0; i < list_size(proceso->segmentos_info); i++) {
            t_seg_info* s = list_get(proceso->segmentos_info, i);
            if (s->id == id_segmento) {
                if (proceso->total_memoria >= s->tamanio)
                    proceso->total_memoria -= s->tamanio;
                else
                    proceso->total_memoria = 0;
                free(list_remove(proceso->segmentos_info, i));
                break;
            }
        }

        log_debug(logger, "## (%d) MEM_FREE OK seg=%u", proceso->pid, id_segmento);

        /* Enunciado v1.1: vuelve a la CPU que hizo la syscall. */
        if (!devolver_proceso_a_misma_cpu(cpu_origen, proceso)) {
            log_warning(logger,
                "CPU %d se desconectó durante MEM_FREE — PID %d se replanifica por READY",
                cpu_origen, proceso->pid);
            encolar_en_ready_y_notificar(proceso);
        }
        return;
    }

    if (strcmp(nombre_syscall, "MUTEX_CREATE") == 0) {
        manejar_syscall_mutex_create(proceso, params, cant_params);
        encolar_en_ready_y_notificar(proceso);
        return;
    }

    if (strcmp(nombre_syscall, "MUTEX_LOCK") == 0) {
        bool se_bloqueo = manejar_syscall_mutex_lock(proceso, params, cant_params);
        if (!se_bloqueo) {
            encolar_en_ready_y_notificar(proceso);
        }
        return;
    }

    if (strcmp(nombre_syscall, "MUTEX_UNLOCK") == 0) {
        manejar_syscall_mutex_unlock(proceso, params, cant_params);
        encolar_en_ready_y_notificar(proceso);
        return;
    }
 
    // INIT_PROC u otras syscalls que no son IO, reencolar en READY
    if (strcmp(nombre_syscall, "INIT_PROC") == 0) {
        manejar_syscall_init_proc(pid, cant_params, params);
    } else {
        log_warning(logger,
            "PID %d: syscall '%s' no reconocida, se reencola en READY",
            pid, nombre_syscall);
    }
 
    encolar_en_ready_y_notificar(proceso);
}

void manejar_syscall_mutex_create(t_pcb* proceso, char** params, int cant_params) {
    if (cant_params < 1 || params == NULL) {
        log_error(logger, "PID %d: MUTEX_CREATE sin parámetros suficientes", proceso->pid);
        return;
    }
    char* nombre_mutex = params[0];
    t_mutex_sistema* mut = buscar_mutex_por_nombre(nombre_mutex);
    if (mut == NULL) {
        mut = malloc(sizeof(t_mutex_sistema));
        mut->nombre = strdup(nombre_mutex);
        mut->asignado_a_pid = -1;
        mut->cola_bloqueados = queue_create();
        registrar_mutex(mut);
        log_debug(logger, "## (%d) Mutex Creado: %s", proceso->pid, nombre_mutex);
    } else {
        log_warning(logger, "PID %d: MUTEX_CREATE - Mutex %s ya existe", proceso->pid, nombre_mutex);
    }
}

bool manejar_syscall_mutex_lock(t_pcb* proceso, char** params, int cant_params) {
    if (cant_params < 1 || params == NULL) {
        log_error(logger, "PID %d: MUTEX_LOCK sin parámetros suficientes", proceso->pid);
        return false;
    }
    char* nombre_mutex = params[0];
    t_mutex_sistema* mut = buscar_mutex_por_nombre(nombre_mutex);
    if (mut == NULL) {
        mut = malloc(sizeof(t_mutex_sistema));
        mut->nombre = strdup(nombre_mutex);
        mut->asignado_a_pid = -1;
        mut->cola_bloqueados = queue_create();
        registrar_mutex(mut);
        log_debug(logger, "## (%d) Mutex Creado: %s (auto-creado en LOCK)", proceso->pid, nombre_mutex);
    }

    if (mut->asignado_a_pid == -1) {
        mut->asignado_a_pid = proceso->pid;
        log_info(logger, "## (%d) Toma el Mutex %s", proceso->pid, nombre_mutex);
        return false;
    } else {
        t_estado estado_anterior = proceso->estado;
        proceso->estado = BLOCK;
        proceso->mutex_esperando = mut;
        
        log_info(logger, "## (%d) Pasa del estado %s al estado BLOCK", proceso->pid, estado_a_string(estado_anterior));
        
        queue_push(mut->cola_bloqueados, proceso);
        propagar_herencia_prioridad(proceso, proceso->prioridad_dinamica);
        /* El proceso bloqueado por mutex NO está en cola_procesos_block, así
         * que el timer de suspensión no lo encuentra y no lo suspende (decisión
         * de diseño: la suspensión aplica a bloqueos por IO, ver config
         * SUSPENSION_TIMEOUT "proceso que está en IO"). */
        proceso->block_epoch++;
        notificar_proceso_en_block(proceso, proceso->block_epoch);
        return true;
    }
}

void manejar_syscall_mutex_unlock(t_pcb* proceso, char** params, int cant_params) {
    if (cant_params < 1 || params == NULL) {
        log_error(logger, "PID %d: MUTEX_UNLOCK sin parámetros suficientes", proceso->pid);
        return;
    }
    char* nombre_mutex = params[0];
    t_mutex_sistema* mut = buscar_mutex_por_nombre(nombre_mutex);
    if (mut == NULL) {
        log_error(logger, "PID %d: MUTEX_UNLOCK - Mutex %s no existe", proceso->pid, nombre_mutex);
        return;
    }

    if (mut->asignado_a_pid != proceso->pid) {
        log_error(logger, "PID %d: MUTEX_UNLOCK - El proceso no posee el mutex %s (poseedor: %d)", 
                  proceso->pid, nombre_mutex, mut->asignado_a_pid);
        return;
    }

    log_info(logger, "## (%d) Libera el Mutex %s", proceso->pid, nombre_mutex);

    if (!queue_is_empty(mut->cola_bloqueados)) {
        t_pcb* siguiente = queue_pop(mut->cola_bloqueados);
        mut->asignado_a_pid = siguiente->pid;
        siguiente->mutex_esperando = NULL;

        log_info(logger, "## (%d) Toma el Mutex %s", siguiente->pid, nombre_mutex);

        t_estado anterior = siguiente->estado;
        siguiente->estado = READY;
        log_info(logger, "## (%d) Pasa del estado %s al estado READY", siguiente->pid, estado_a_string(anterior));

        encolar_en_ready_y_notificar(siguiente);
        recalcular_prioridad_dinamica(siguiente);
    } else {
        mut->asignado_a_pid = -1;
    }

    recalcular_prioridad_dinamica(proceso);
}

// =============================================================================
// HELPERS GENERALES
// =============================================================================

void spawn_detached(void* (*fn)(void*), void* arg, const char* nombre)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, fn, arg) != 0)
        log_error(logger, "Error creando thread: %s", nombre);
    else
        pthread_detach(thread);
}

// =============================================================================
// QUANTUM TIMER (Round Robin)
// =============================================================================

typedef struct {
    int cpu_id;
    int pid;
    uint32_t quantum_ms;
} t_quantum_timer_args;

static void* quantum_timer_thread(void* arg)
{
    t_quantum_timer_args* a = (t_quantum_timer_args*)arg;
    int cpu_id = a->cpu_id, pid = a->pid;
    uint32_t quantum_ms = a->quantum_ms;
    free(a);

    usleep((useconds_t)quantum_ms * 1000);

    pthread_mutex_lock(&scheduler_connections.lock);
    for (int i = 0; i < scheduler_connections.cpu_count; i++) {
        if (scheduler_connections.cpus[i].cpu_id == cpu_id &&
            scheduler_connections.cpus[i].timer_activo &&
            scheduler_connections.cpus[i].timer_pid == pid) {

            scheduler_connections.cpus[i].timer_activo = false;
            int sock = scheduler_connections.cpus[i].socket;
            pthread_mutex_unlock(&scheduler_connections.lock);

            log_debug(logger, "## (%d) Quantum expirado - enviando INTERRUPCION", pid);
            enviar_interrupcion(sock, pid, MOTIVO_QUANTUM);
            return NULL;
        }
    }
    pthread_mutex_unlock(&scheduler_connections.lock);
    return NULL;
}

void iniciar_quantum_timer(int cpu_id, int pid, uint32_t quantum_ms)
{
    pthread_mutex_lock(&scheduler_connections.lock);
    for (int i = 0; i < scheduler_connections.cpu_count; i++) {
        if (scheduler_connections.cpus[i].cpu_id == cpu_id) {
            scheduler_connections.cpus[i].timer_activo = true;
            scheduler_connections.cpus[i].timer_pid = pid;
            break;
        }
    }
    pthread_mutex_unlock(&scheduler_connections.lock);

    t_quantum_timer_args* args = malloc(sizeof(t_quantum_timer_args));
    args->cpu_id = cpu_id;
    args->pid = pid;
    args->quantum_ms = quantum_ms;
    spawn_detached(quantum_timer_thread, args, "quantum_timer");
}

// =============================================================================
// HELPERS DE CONEXIÓN
// =============================================================================

int enviar_proceso_a_cpu(t_pcb* proceso)
{
    if (proceso == NULL) {
        log_error(logger, "No se puede enviar proceso NULL a CPU");
        return -1;
    }

    pthread_mutex_lock(&scheduler_connections.lock);

    if (scheduler_connections.cpu_count == 0) {
        pthread_mutex_unlock(&scheduler_connections.lock);
        log_warning(logger, "No hay CPUs conectadas para despachar PID:%d", proceso->pid);
        return -1;
    }

    int cpu_socket = -1;
    int cpu_id_elegido = -1;
    int pid = proceso->pid;
    for (int i = 0; i < scheduler_connections.cpu_count; i++) {
        if (scheduler_connections.cpus[i].disponible &&
            scheduler_connections.cpus[i].socket != -1) {
            scheduler_connections.cpus[i].disponible = false;
            scheduler_connections.cpus[i].proceso_actual = proceso;
            cpu_socket = scheduler_connections.cpus[i].socket;
            cpu_id_elegido = scheduler_connections.cpus[i].cpu_id;
            break;
        }
    }

    if (cpu_socket == -1) {
        pthread_mutex_unlock(&scheduler_connections.lock);
        log_warning(logger, "No hay CPUs disponibles para PID:%d", proceso->pid);
        return -1;
    }

    pthread_mutex_unlock(&scheduler_connections.lock);

    enviar_pid(cpu_socket, pid);

    log_debug(logger, "Proceso PID:%d despachado a CPU %d", pid, cpu_id_elegido);
    return cpu_id_elegido;
}

#define RETARDO_REINTENTO_MS  2000
#define HANDSHAKE_TIMEOUT_S   5
#define LOG_CADA_N_INTENTOS   10
// Red de seguridad: ninguna operación contra Memoria debería tardar más que esto.
// Si se excede, el recv retorna -1 y el caller suelta mutex_memory_socket en vez de
// colgar el canal entero para siempre (defensa en profundidad contra requests sin
// respuesta). Valor holgado: las operaciones normales tardan <1s en localhost.
#define MEMORY_OP_TIMEOUT_S   30
// Red de seguridad: ninguna operación contra Memoria debería tardar más que esto.
// Si se excede, el recv retorna -1 y el caller suelta mutex_memory_socket en vez de
// colgar el canal entero para siempre (defensa en profundidad contra requests sin
// respuesta). Valor holgado: las operaciones normales tardan <1s en localhost.
#define MEMORY_OP_TIMEOUT_S   30

bool conectar_a_kernel_memory(char* ip, char* puerto)
{
    log_debug(logger, "Conectando a Kernel Memory en %s:%s...", ip, puerto);

    int intentos = 0;

    while (1) {
        intentos++;

        int sock = crear_conexion(ip, puerto);
        if (sock == -1) {
            if (intentos % LOG_CADA_N_INTENTOS == 0)
                log_warning(logger,
                    "Kernel Memory no disponible en %s:%s — intento %d, reintentando en %d ms...",
                    ip, puerto, intentos, RETARDO_REINTENTO_MS);
            usleep(RETARDO_REINTENTO_MS * 1000);
            continue;
        }

        // Timeout en recv para no bloquearse indefinidamente durante el handshake
        struct timeval tv = { .tv_sec = HANDSHAKE_TIMEOUT_S, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        enviar_handshake_scheduler_a_memory(sock);

        op_code ack = recibir_codigo_operacion(sock);
        if ((int)ack == -1 || ack != HANDSHAKE_SCHEDULER_A_MEMORY) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                log_warning(logger, "Timeout esperando handshake de Kernel Memory — reintentando...");
            else
                log_warning(logger, "Handshake inválido desde Kernel Memory (opcode %d) — reintentando...", ack);
            close(sock);
            usleep(RETARDO_REINTENTO_MS * 1000);
            continue;
        }

        recibir_ok(sock);

        // Conexión establecida. Dejamos un timeout HOLGADO (no bloqueo infinito)
        // como red de seguridad: si Memoria alguna vez no responde una request,
        // el recv corta y el caller suelta mutex_memory_socket en vez de wedgear
        // todo el canal scheduler↔memoria.
        tv.tv_sec = MEMORY_OP_TIMEOUT_S;
        // Conexión establecida. Dejamos un timeout HOLGADO (no bloqueo infinito)
        // como red de seguridad: si Memoria alguna vez no responde una request,
        // el recv corta y el caller suelta mutex_memory_socket en vez de wedgear
        // todo el canal scheduler↔memoria.
        tv.tv_sec = MEMORY_OP_TIMEOUT_S;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        memory_socket = sock;
        log_info(logger, "## Conectado a Kernel Memory");
        log_debug(logger, "Conexión a Kernel Memory establecida en el intento %d", intentos);
        return true;
    }
}

// El socket con Memoria tiene un SO_RCVTIMEO (MEMORY_OP_TIMEOUT_S) como red de
// seguridad contra una Memoria colgada. Pero una compactación es una operación
// legítimamente lenta: tarda COMPACTION_DELAY (configurable, decenas de segundos)
// MÁS el tiempo de reubicar cada segmento, lo que puede superar ese timeout. Si el
// recv corta en medio de la compactación, el scheduler mataría por ERROR justo al
// proceso que la disparó (aunque Memoria termine bien). Por eso, entre
// INICIAR_COMPACTACION y FINALIZAR_COMPACTACION desactivamos el timeout y lo
// restauramos al terminar (ver handlers en memory_protocol.c).
void suspender_timeout_memoria(int socket)
{
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };  // 0 = sin timeout (bloqueo)
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

void restaurar_timeout_memoria(int socket)
{
    struct timeval tv = { .tv_sec = MEMORY_OP_TIMEOUT_S, .tv_usec = 0 };
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// =============================================================================
// HANDLERS DE CONEXIÓN
// =============================================================================

static void marcar_cpu_desconectada(int cpu_id, int socket)
{
    pthread_mutex_lock(&scheduler_connections.lock);
    for (int i = 0; i < scheduler_connections.cpu_count; i++) {
        if (scheduler_connections.cpus[i].cpu_id == cpu_id) {
            scheduler_connections.cpus[i].socket = -1;
            break;
        }
    }
    pthread_mutex_unlock(&scheduler_connections.lock);
    close(socket);
}

// QUITARIA ESTO Y LO PONDRIA EN OTRO ARCHIVO, POR EJEMPLO EN scheduler_cpu.c
void* scheduler_cpu_receptor_thread(void* arg)
{
    t_cpu_thread_arg* cpu_arg = (t_cpu_thread_arg*)arg;
    int socket  = cpu_arg->socket;
    int cpu_id  = cpu_arg->cpu_id;
    free(cpu_arg);

    while (1) {
        op_code codigo = recibir_codigo_operacion(socket);
        if ((int)codigo == -1) {
            log_warning(logger, "CPU %d desconectada", cpu_id);
            marcar_cpu_desconectada(cpu_id, socket);
            break;
        }

        switch (codigo) {
            case DEVOLVER_PROCESO: {
            int pid_devuelto = -1;
            t_motivo_devolucion motivo = MOTIVO_ERROR;
            char* nombre_syscall = NULL;
            int cant_params = 0;
            char** params = NULL;

            recibir_devolver_proceso(socket,
                &pid_devuelto,
                &motivo,
                &nombre_syscall,
                &params,
                &cant_params);

            if (pid_devuelto < 0) {
                log_error(logger, "CPU %d: paquete DEVOLVER_PROCESO invalido", cpu_id);
                break;
            }

            /* Syscalls de memoria (enunciado v1.1): la CPU queda reservada
             * para devolverle el mismo proceso cuando la syscall termine. */
            bool es_syscall_memoria =
                (motivo == MOTIVO_SYSCALL && nombre_syscall != NULL &&
                 (strcmp(nombre_syscall, "MEM_ALLOC") == 0 ||
                  strcmp(nombre_syscall, "MEM_FREE") == 0));

            t_pcb* proceso = es_syscall_memoria
                ? retirar_proceso_de_cpu_reservando(cpu_id, pid_devuelto)
                : retirar_proceso_de_cpu(cpu_id, pid_devuelto);
            if (proceso == NULL) {
                log_warning(logger,
                    "CPU %d devolvio PID %d pero no habia proceso en ejecucion registrado",
                    cpu_id,
                    pid_devuelto);
                if (es_syscall_memoria) {
                    liberar_cpu_reservada(cpu_id);
                }
                free(nombre_syscall);
                liberar_parametros_syscall(params, cant_params);
                break;
            }

            switch (motivo) {
                case MOTIVO_EXIT:
                    proceso->estado = EXIT_STATE;
                    log_info(logger, "## (%d) Pasa del estado EXEC al estado EXIT", proceso->pid);
                    log_info(logger, "## (%d) finalizó su ejecución con motivo de SUCCESS", proceso->pid);
                    liberar_mutexes_de_proceso(proceso->pid);
                    notificar_fin_a_memoria(proceso->pid);
                    eliminar_pcb(proceso->pid);
                    free(proceso);
                    break;

                case MOTIVO_ERROR:
                    proceso->estado = EXIT_STATE;
                    log_warning(logger,
                        "## (%d) Pasa del estado EXEC al estado EXIT por ERROR",
                        proceso->pid);
                    log_info(logger, "## (%d) finalizó su ejecución con motivo de ERROR", proceso->pid);
                    liberar_mutexes_de_proceso(proceso->pid);
                    notificar_fin_a_memoria(proceso->pid);
                    eliminar_pcb(proceso->pid);
                    free(proceso);
                    break;

                case MOTIVO_SEGFAULT:
                    proceso->estado = EXIT_STATE;
                    log_warning(logger,
                        "## (%d) Pasa del estado EXEC al estado EXIT por SEG_FAULT",
                        proceso->pid);
                    log_info(logger,
                        "## (%d) finalizó su ejecución con motivo de SEG_FAULT",
                        proceso->pid);
                    liberar_mutexes_de_proceso(proceso->pid);
                    notificar_fin_a_memoria(proceso->pid);
                    eliminar_pcb(proceso->pid);
                    free(proceso);
                    break;

                case MOTIVO_QUANTUM:
                    log_info(logger, "## (%d) - Desalojado por fin de quantum", proceso->pid);
                    log_info(logger, "## (%d) Pasa del estado EXEC al estado READY [quantum]", proceso->pid);
                    encolar_en_ready_y_notificar(proceso);
                    break;

                case MOTIVO_SYSCALL:
                   /* log_info(logger,
                        "## (%d) Devuelto por SYSCALL: %s",
                        proceso->pid,
                        nombre_syscall != NULL ? nombre_syscall : "(sin nombre)");
                    encolar_en_ready_y_notificar(proceso);*/

                    manejar_syscall_io(proceso, nombre_syscall, params, cant_params, cpu_id);
                        // No reencolar acá: manejar_syscall_io lo hace
                    break;

                case MOTIVO_DESALOJO:
                case MOTIVO_DESALOJO_COLA:
                    // Ambos desalojos (por compactación y por cola más prioritaria)
                    // devuelven el proceso al PRINCIPIO de READY (enunciado pág. 11),
                    // no al final como en fin de quantum. Solo cambia el motivo logueado.
                    proceso->estado = READY;
                    if (motivo == MOTIVO_DESALOJO_COLA) {
                        log_info(logger,
                            "## (%d) Pasa del estado EXEC al estado READY [desalojo por cola más prioritaria]",
                            proceso->pid);
                    } else {
                        log_info(logger,
                            "## (%d) Pasa del estado EXEC al estado READY [desalojo por compactación]",
                            proceso->pid);
                    }
                    if (cantidad_niveles_prioridad > 0) {
                        reencolar_proceso_ready_inicio(proceso, proceso->prioridad);
                    } else {
                        pthread_mutex_lock(&mutex_ready);
                        if (cola_procesos_ready != NULL) {
                            list_add_in_index(cola_procesos_ready->elements, 0, proceso);
                            sem_post(&sem_ready);
                        }
                        pthread_mutex_unlock(&mutex_ready);
                    }
                    break;

                default:
                    log_warning(logger,
                        "PID %d devuelto con motivo desconocido (%d). Se reencola en READY",
                        proceso->pid,
                        (int)motivo);
                    encolar_en_ready_y_notificar(proceso);
                    break;
            }

            free(nombre_syscall);
            liberar_parametros_syscall(params, cant_params);
            break;
        }

            default: {
                unsigned char peek_buf[32];
                ssize_t peeked = recv(socket, peek_buf, sizeof(peek_buf), MSG_PEEK | MSG_DONTWAIT);
                char hex[3 * sizeof(peek_buf) + 1] = {0};
                if (peeked > 0) {
                    for (ssize_t i = 0; i < peeked; i++) {
                        snprintf(hex + i * 3, 4, "%02x ", peek_buf[i]);
                    }
                }
                log_error(logger,
                    "CPU %d: opcode desconocido %d — stream desincronizado, cerrando conexion. "
                    "Bytes siguientes (peek %zd): %s",
                    cpu_id, codigo, peeked, hex);
                marcar_cpu_desconectada(cpu_id, socket);
                goto fin_receptor;
            }
        }
    }

fin_receptor:
    log_debug(logger, "Thread receptor de CPU %d finalizado", cpu_id);
    return NULL;
}

// YO DEJARIA ESTO:

void* scheduler_handshake_handler(void* socket_ptr)
{
        int client_socket = *(int*)socket_ptr;
        free(socket_ptr);

        op_code codigo = recibir_codigo_operacion(client_socket);

        if ((int)codigo == -1) {
            log_error(logger, "Error recibiendo código de operación (valor: -1)");
            close(client_socket);
            return NULL;
    }

    if (codigo == HANDSHAKE_CPU) {
        int cpu_id = recibir_handshake_cpu(client_socket);
        if (cpu_id <= 0) {
            log_error(logger, "Error recibiendo handshake de CPU");
            close(client_socket);
            return NULL;
        }

        log_info(logger, "## CPU %d Conectada", cpu_id);

        pthread_mutex_lock(&scheduler_connections.lock);
        
        if (scheduler_connections.cpu_count < MAX_CPUS) {
            scheduler_connections.cpus[scheduler_connections.cpu_count].socket = client_socket;
            scheduler_connections.cpus[scheduler_connections.cpu_count].cpu_id = cpu_id;
            scheduler_connections.cpus[scheduler_connections.cpu_count].disponible = true;
            scheduler_connections.cpus[scheduler_connections.cpu_count].proceso_actual = NULL;
            scheduler_connections.cpu_count++;
        }
        pthread_mutex_unlock(&scheduler_connections.lock);

        // Despertar al PCP si estaba esperando una CPU disponible.
        sem_post(&sem_cpu_disponible);

        pthread_mutex_lock(&mutex_ready);
        if (cola_procesos_ready != NULL && !queue_is_empty(cola_procesos_ready)) {
            sem_post(&sem_ready);
        }
        pthread_mutex_unlock(&mutex_ready);

        t_cpu_thread_arg* cpu_arg = malloc(sizeof(t_cpu_thread_arg));
        cpu_arg->socket = client_socket;
        cpu_arg->cpu_id = cpu_id;

        spawn_detached(scheduler_cpu_receptor_thread, cpu_arg, "cpu_receptor");
    }
    else if (codigo == HANDSHAKE_IO) {
       /* log_info(logger, "## Interfaz de IO Conectada");

        pthread_mutex_lock(&scheduler_connections.lock);
        if (scheduler_connections.io_count < MAX_IOS) {
            scheduler_connections.ios[scheduler_connections.io_count].socket = client_socket;
            scheduler_connections.ios[scheduler_connections.io_count].nombre = NULL;
            scheduler_connections.io_count++;
        }
        pthread_mutex_unlock(&scheduler_connections.lock); */
        char* nombre_io = recibir_handshake_io(client_socket);
        if (nombre_io == NULL) {
            log_warning(logger, "IO conectada sin nombre, cerrando socket");
            close(client_socket);
            return NULL;
        }
 
        log_debug(logger, "## Interfaz de IO '%s' Conectada", nombre_io);
 
        // Registrar en la tabla de IOs con su worker thread
        registrar_interfaz_io(client_socket, nombre_io);
 
        free(nombre_io);
    }
    else {
        log_warning(logger, "Handshake desconocido (opcode %d)", codigo);
        close(client_socket);
        return NULL;
    }

    return NULL;
}


// YO DEJARIA ESTO:
void* scheduler_listener_thread(void* arg)
{
    (void)arg;

    while (1) {
        int client_socket = esperar_cliente(server_socket);
        if (client_socket == -1) {
            break;
        }

        int* socket_ptr = malloc(sizeof(int));
        *socket_ptr = client_socket;

        spawn_detached(scheduler_handshake_handler, socket_ptr, "handshake");
    }
    return NULL;
}

// =============================================================================
// FUNCIONES PARA PREEMPTION ENTRE COLAS
// =============================================================================

// Decide y ejecuta el desalojo entre colas (CMN con QUEUE_PREEMPTION).
//
// Toda la selección de la CPU víctima se hace dentro de un único critical
// section sobre scheduler_connections.lock, copiando solo valores primitivos
// (socket, pid, prioridad). El envío de la interrupción se hace DESPUÉS de
// soltar el lock, igual que quantum_timer_thread: así no se sostiene el mutex
// durante I/O y no se desreferencia ningún t_pcb* fuera del lock (evita el
// use-after-free si la CPU devuelve y libera su proceso en paralelo).
//
// La interrupción tardía es benigna: la CPU valida el PID recibido contra el
// proceso en ejecución y descarta las que ya no corresponden
// (cpu_scheduler.c, case INTERRUPCION).
static void verificar_y_desalojar_por_prioridad(t_pcb* nuevo_proceso)
{
    if (nuevo_proceso == NULL || !queue_preemption_habilitada()) {
        return;
    }

    // Durante la compactación los procesos desalojados van al frente de READY y
    // se replanifican al terminar; preemptar en el medio rompería esa secuencia.
    if (compactacion_en_progreso) {
        return;
    }

    int prioridad_nueva = nuevo_proceso->prioridad;

    int socket_a_desalojar = -1;
    int pid_a_desalojar = -1;
    int prioridad_a_desalojar = -1;

    pthread_mutex_lock(&scheduler_connections.lock);

    t_cpu* cpu_victima = NULL;
    int menor_prioridad = -1;      // número mayor = menos prioritario
    time_t inicio_victima = 0;

    for (int i = 0; i < scheduler_connections.cpu_count; i++) {
        t_cpu* cpu = &scheduler_connections.cpus[i];

        // Solo CPUs ejecutando un proceso con socket activo
        if (cpu->disponible || cpu->proceso_actual == NULL || cpu->socket == -1) {
            continue;
        }

        int prioridad_actual = cpu->proceso_actual->prioridad;
        time_t inicio_actual = cpu->proceso_actual->tiempo_inicio_ejecucion;

        if (cpu_victima == NULL || prioridad_actual > menor_prioridad) {
            // Estrictamente menos prioritario: nuevo candidato
            cpu_victima = cpu;
            menor_prioridad = prioridad_actual;
            inicio_victima = inicio_actual;
        } else if (prioridad_actual == menor_prioridad &&
                   inicio_actual < inicio_victima) {
            // Desempate: la que lleva más tiempo ejecutando (inicio más antiguo)
            cpu_victima = cpu;
            inicio_victima = inicio_actual;
        }
    }

    // Solo desalojar si el nuevo proceso es MÁS prioritario (número menor)
    if (cpu_victima != NULL && prioridad_nueva < menor_prioridad) {
        socket_a_desalojar = cpu_victima->socket;
        pid_a_desalojar = cpu_victima->proceso_actual->pid;
        prioridad_a_desalojar = menor_prioridad;
    }

    pthread_mutex_unlock(&scheduler_connections.lock);

    if (socket_a_desalojar != -1) {
        log_info(logger,
            "## (%d) Prioridad: %d - Desalojado por cola más prioritaria por el proceso %d con prioridad %d",
            pid_a_desalojar,
            prioridad_a_desalojar,
            nuevo_proceso->pid,
            prioridad_nueva);

        enviar_interrupcion(socket_a_desalojar, pid_a_desalojar, MOTIVO_DESALOJO_COLA);
    }
}

// =============================================================================
// FUNCIONES PÚBLICAS PARA COLAS MULTINIVEL
// =============================================================================

void encolar_en_ready_por_prioridad(t_pcb* proceso)
{
    if (proceso == NULL) {
        return;
    }

    if (proceso->prioridad < 0 || proceso->prioridad >= cantidad_niveles_prioridad) {
        log_warning(logger,
            "PID %d: prioridad %d fuera de rango [0-%d]. Se descarta.",
            proceso->pid,
            proceso->prioridad,
            cantidad_niveles_prioridad - 1);
        free(proceso);
        return;
    }

    proceso->estado = READY;

    pthread_mutex_lock(&mutex_colas_prioridad);
    if (colas_ready_por_prioridad[proceso->prioridad] != NULL) {
        queue_push(colas_ready_por_prioridad[proceso->prioridad], proceso);
        sem_post(&sem_ready);
    }
    pthread_mutex_unlock(&mutex_colas_prioridad);

    log_debug(logger,
        "## (%d) Encolado en READY (prioridad %d)",
        proceso->pid,
        proceso->prioridad);

    // Verificar si hay preemption disponible
    verificar_y_desalojar_por_prioridad(proceso);
}

t_pcb* desencolar_proceso_ready(void)
{
    t_pcb* proceso = NULL;

    pthread_mutex_lock(&mutex_colas_prioridad);
    
    // Buscar desde la cola de mayor prioridad (0) hasta la menor
    for (int p = 0; p < cantidad_niveles_prioridad; p++) {
        if (colas_ready_por_prioridad[p] != NULL && 
            !queue_is_empty(colas_ready_por_prioridad[p])) {
            proceso = queue_pop(colas_ready_por_prioridad[p]);
            break;
        }
    }
    
    pthread_mutex_unlock(&mutex_colas_prioridad);

    return proceso;
}

void reencolar_proceso_ready_final(t_pcb* proceso, int prioridad)
{
    if (proceso == NULL || prioridad < 0 || prioridad >= cantidad_niveles_prioridad) {
        return;
    }

    pthread_mutex_lock(&mutex_colas_prioridad);
    if (colas_ready_por_prioridad[prioridad] != NULL) {
        queue_push(colas_ready_por_prioridad[prioridad], proceso);
        sem_post(&sem_ready);
    }
    pthread_mutex_unlock(&mutex_colas_prioridad);

    // Punto común de arribo a READY (vuelta de IO, des-suspensión, INIT_PROC,
    // syscalls). Verificar desalojo entre colas fuera del mutex de colas.
    verificar_y_desalojar_por_prioridad(proceso);
}

void reencolar_proceso_ready_inicio(t_pcb* proceso, int prioridad)
{
    if (proceso == NULL || prioridad < 0 || prioridad >= cantidad_niveles_prioridad) {
        return;
    }

    pthread_mutex_lock(&mutex_colas_prioridad);
    if (colas_ready_por_prioridad[prioridad] != NULL) {
        list_add_in_index(colas_ready_por_prioridad[prioridad]->elements, 0, proceso);
        sem_post(&sem_ready);
    }
    pthread_mutex_unlock(&mutex_colas_prioridad);
}

// =============================================================================
// FUNCIONES PARA DESALOJO Y COMPACTACIÓN
// =============================================================================

void desalojar_todos_procesos(void)
{
    log_debug(logger, "Iniciando desalojo de todos los procesos en ejecución");

    pthread_mutex_lock(&scheduler_connections.lock);
    for (int i = 0; i < scheduler_connections.cpu_count; i++) {
        if (!scheduler_connections.cpus[i].disponible && 
            scheduler_connections.cpus[i].proceso_actual != NULL &&
            scheduler_connections.cpus[i].socket != -1) {
            
            t_pcb* proceso = scheduler_connections.cpus[i].proceso_actual;
            log_debug(logger, "Desalojando PID %d de CPU %d", proceso->pid, scheduler_connections.cpus[i].cpu_id);

            // Interrumpir a la CPU con motivo DESALOJO. El proceso vuelve por el
            // flujo normal DEVOLVER_PROCESO, que lo reencola al frente de READY.
            // No tocamos disponible/proceso_actual acá: lo hace retirar_proceso_de_cpu
            // cuando la CPU devuelve el proceso.
            enviar_interrupcion(scheduler_connections.cpus[i].socket, proceso->pid, MOTIVO_DESALOJO);
        }
    }
    pthread_mutex_unlock(&scheduler_connections.lock);

    log_debug(logger, "Señales de desalojo registradas");
}

void manejar_compactacion_inicio(void)
{
    pthread_mutex_lock(&mutex_bsod);
    compactacion_en_progreso = 1;
    pthread_mutex_unlock(&mutex_bsod);

    log_info(logger, "## Inicio de compactación");
    desalojar_todos_procesos();
}

void manejar_compactacion_fin(void)
{
    pthread_mutex_lock(&mutex_bsod);
    compactacion_en_progreso = 0;
    pthread_mutex_unlock(&mutex_bsod);

    log_info(logger, "## Fin de compactación");

    /* Reanudar el planificador a corto plazo */
    sem_post(&sem_ready);

    /* Notificar al PMP: puede haber espacio para des-suspender procesos */
    notificar_reevaluar_suspendidos();
}

