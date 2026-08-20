#include "colaIO.h"
#include "globalesKernelScheduler.h"
#include "scheduler_conexiones.h"
#include "scheduler_config.h"
#include "utils_kernel_scheduler.h"
#include "memory_protocol.h"
#include "planificadorMedianoPlazo.h"
#include <utils/sockets.h>
#include <utils/mensajes.h>
#include <utils/serializacion.h>
#include <commons/log.h>
#include <commons/collections/queue.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

extern t_log*  logger;
extern int     memory_socket;
extern pthread_mutex_t mutex_memory_socket;

// Var globales

t_interfaz_io* tabla_ios[MAX_IOS];
int            tabla_ios_count = 0;
pthread_mutex_t mutex_tabla_ios = PTHREAD_MUTEX_INITIALIZER;

t_queue*        cola_procesos_block = NULL;
pthread_mutex_t mutex_block         = PTHREAD_MUTEX_INITIALIZER;

static t_pcb* extraer_de_block(int pid);

// ----------------------------------------------------------------------------
// MEDIANO PLAZO: suspensión por timeout en BLOCK
// ----------------------------------------------------------------------------

// Saca un proceso de cola_procesos_block de forma segura. Si está SUSP_BLOCK con
// el volcado a SWAP todavía en vuelo (suspendido_en_swap == false), espera sin
// removerlo para no romper el orden suspender→des-suspender ni liberar el PCB
// mientras el timer lo sigue usando. Devuelve el PCB removido, o NULL si no está.
static t_pcb* extraer_de_block(int pid)
{
    while (true) {
        pthread_mutex_lock(&mutex_block);
        int idx = -1;
        t_pcb* proceso = NULL;
        for (int i = 0; i < list_size(cola_procesos_block->elements); i++) {
            t_pcb* p = list_get(cola_procesos_block->elements, i);
            if (p != NULL && p->pid == pid) { proceso = p; idx = i; break; }
        }
        if (proceso == NULL) {
            pthread_mutex_unlock(&mutex_block);
            return NULL;
        }
        if (proceso->estado == SUSP_BLOCK && !proceso->suspendido_en_swap) {
            // Suspensión en progreso: esperar a que termine el volcado a SWAP.
            pthread_mutex_unlock(&mutex_block);
            usleep(2000);
            continue;
        }
        list_remove(cola_procesos_block->elements, idx);
        pthread_mutex_unlock(&mutex_block);
        return proceso;
    }
}

// El timer de suspensión vive en planificadorMedianoPlazo.c (un solo dueño:
// notificar_proceso_en_block). Maneja el flag suspendido_en_swap que
// extraer_de_block espera para no remover un PCB con el volcado a SWAP en vuelo.

void inicializar_tabla_ios(void)
{
    pthread_mutex_lock(&mutex_tabla_ios);
    for (int i = 0; i < MAX_IOS; i++) {
        tabla_ios[i] = NULL;
    }
    tabla_ios_count = 0;
    pthread_mutex_unlock(&mutex_tabla_ios);

    pthread_mutex_lock(&mutex_block);
    cola_procesos_block = queue_create();
    pthread_mutex_unlock(&mutex_block);
}


t_io_type inferir_tipo_io(const char* nombre)
{
    if (nombre == NULL) return IO_TYPE_SLEEP;

    if (strncasecmp(nombre, "SLEEP", 5) == 0) return IO_TYPE_SLEEP;
    if (strncasecmp(nombre, "STDIN", 5) == 0) return IO_TYPE_STDIN;
    if (strncasecmp(nombre, "STDOUT", 6) == 0) return IO_TYPE_STDOUT;

    return IO_TYPE_SLEEP;
}

// Registro Interfaz de io 

t_interfaz_io* registrar_interfaz_io(int socket, const char* nombre)
{
    pthread_mutex_lock(&mutex_tabla_ios);

    if (tabla_ios_count >= MAX_IOS) {
        pthread_mutex_unlock(&mutex_tabla_ios);
        log_error(logger, "No hay espacio para registrar nueva IO '%s'", nombre);
        return NULL;
    }

    t_interfaz_io* io = malloc(sizeof(t_interfaz_io));
    if (io == NULL) {
        pthread_mutex_unlock(&mutex_tabla_ios);
        log_error(logger, "No se pudo alocar memoria para IO '%s'", nombre);
        return NULL;
    }

    io->socket  = socket;
    io->nombre  = strdup(nombre);
    io->tipo    = inferir_tipo_io(nombre);
    io->cola    = queue_create();
    io->activa  = true;

    pthread_mutex_init(&io->mutex_cola, NULL);
    sem_init(&io->sem_cola, 0, 0);

    tabla_ios[tabla_ios_count++] = io;

    pthread_mutex_unlock(&mutex_tabla_ios);

    // Lanzar el thread worker de esta IO
    if (pthread_create(&io->thread_worker, NULL, io_worker_thread, io) != 0) {
        log_error(logger, "Error creando thread worker para IO '%s'", nombre);
        io->activa = false;
        return NULL;
    }
    pthread_detach(io->thread_worker);

    log_debug(logger, "## Interfaz IO '%s' registrada (socket %d)", nombre, socket);
    return io;
}

// Busco interfaz por nombre

t_interfaz_io* buscar_interfaz_io(const char* nombre)
{
    if (nombre == NULL) return NULL;

    pthread_mutex_lock(&mutex_tabla_ios);
    t_interfaz_io* resultado = NULL;

    for (int i = 0; i < tabla_ios_count; i++) {
        if (tabla_ios[i] != NULL &&
            tabla_ios[i]->activa &&
            tabla_ios[i]->nombre != NULL &&
            strcasecmp(tabla_ios[i]->nombre, nombre) == 0) {
            resultado = tabla_ios[i];
            break;
        }
    }

    pthread_mutex_unlock(&mutex_tabla_ios);
    return resultado;
}

t_interfaz_io* buscar_interfaz_io_por_tipo(t_io_type tipo)
{
    pthread_mutex_lock(&mutex_tabla_ios);
    t_interfaz_io* resultado = NULL;
    for (int i = 0; i < tabla_ios_count; i++) {
        if (tabla_ios[i] != NULL && tabla_ios[i]->activa && tabla_ios[i]->tipo == tipo) {
            resultado = tabla_ios[i];
            break;
        }
    }
    pthread_mutex_unlock(&mutex_tabla_ios);
    return resultado;
}

// Crear request

t_io_request* crear_io_request_sleep(t_pcb* proceso, int tiempo_ms)
{
    t_io_request* req = malloc(sizeof(t_io_request));
    if (req == NULL) return NULL;

    req->proceso    = proceso;
    req->tipo       = IO_TYPE_SLEEP;
    req->tiempo_ms  = tiempo_ms;
    return req;
}

t_io_request* crear_io_request_stdin(t_pcb* proceso, int size, int dir_logica)
{
    t_io_request* req = malloc(sizeof(t_io_request));
    if (req == NULL) return NULL;

    req->proceso          = proceso;
    req->tipo             = IO_TYPE_STDIN;
    req->stdin_size       = size;
    req->stdin_dir_logica = dir_logica;
    return req;
}

t_io_request* crear_io_request_stdout(t_pcb* proceso, int dir_logica, int size, char* buffer)
{
    t_io_request* req = malloc(sizeof(t_io_request));
    if (req == NULL) return NULL;

    req->proceso            = proceso;
    req->tipo               = IO_TYPE_STDOUT;
    req->stdout_dir_logica  = dir_logica;
    req->stdout_size        = size;
    req->stdout_buffer      = buffer;
    return req;
}

//Encolo y pongo el proc en BLOCK

bool encolar_io_request(t_interfaz_io* io, t_io_request* request)
{
    if (io == NULL || request == NULL || !io->activa) return false;

    t_pcb* proceso = request->proceso;

    // Cambio a BLOCK
    t_estado estado_anterior = proceso->estado;
    proceso->estado = BLOCK;
    log_info(logger,
        "## (%d) Pasa del estado %s al estado BLOCK",
        proceso->pid,
        estado_anterior == EXEC ? "EXEC" : "READY");

    // Agregar a cola de bloqueados. El epoch identifica ESTE episodio BLOCK:
    // se incrementa bajo mutex_block y viaja al timer de suspensión, que solo
    // suspende si el proceso sigue en el mismo episodio (evita que un timer de
    // un BLOCK anterior suspenda prematuramente uno nuevo).
    pthread_mutex_lock(&mutex_block);
    proceso->block_epoch++;
    uint32_t epoch_block = proceso->block_epoch;
    queue_push(cola_procesos_block, proceso);
    pthread_mutex_unlock(&mutex_block);

    /* Arrancar el timer del PMP: si el proceso sigue en BLOCK luego de
     * SUSPENSION_TIMEOUT ms, el planificador de mediano plazo lo suspenderá. */
    notificar_proceso_en_block(proceso, epoch_block);

    // Encolar en la IO específica
    pthread_mutex_lock(&io->mutex_cola);
    queue_push(io->cola, request);
    pthread_mutex_unlock(&io->mutex_cola);

    sem_post(&io->sem_cola);

    return true;
}

// Desbloqueo proc cdo IO termina

void desbloquear_proceso_por_io(int pid)
{
    // Saca de BLOCK de forma segura (espera si hay una suspensión en vuelo).
    t_pcb* proceso = extraer_de_block(pid);

    if (proceso == NULL) {
        log_warning(logger,
            "IO_FINALIZADA para PID %d pero no estaba en BLOCK", pid);
        return;
    }

    log_info(logger, "## (%d) finalizó IO y pasa a READY / SUSP. READY", pid);

    if (proceso->estado == SUSP_BLOCK) {
        proceso->estado = SUSP_READY;
        log_info(logger, "## (%d) Pasa del estado SUSP_BLOCK al estado SUSP_READY", pid);
        /* Notificar al PMP: puede haber espacio para des-suspender */
        notificar_reevaluar_suspendidos();
        return;
    }

    // Caso normal: BLOCK a READY
    log_info(logger, "## (%d) Pasa del estado BLOCK al estado READY", pid);
    encolar_en_ready_y_notificar(proceso);
}

// Finaliza por SEG_FAULT un PCB YA removido de las colas de planificación
// (sigue el patrón de MOTIVO_ERROR en utils_kernel_scheduler.c).
void finalizar_pcb_por_seg_fault(t_pcb* proceso)
{
    int pid = proceso->pid;
    const char* estado_origen = estado_a_string(proceso->estado);

    proceso->estado = EXIT_STATE;
    log_warning(logger, "## (%d) Pasa del estado %s al estado EXIT por SEG_FAULT",
                pid, estado_origen);
    log_info(logger, "## (%d) finalizó su ejecución con motivo de SEG_FAULT", pid);

    /* Si estuvo suspendido, el timer lo agregó a lista_suspendidos: sacarlo
     * ANTES de liberar el PCB o el PMP quedaría con un puntero colgante. */
    remover_de_lista_suspendidos(pid);
    liberar_mutexes_de_proceso(pid);
    notificar_fin_a_memoria(pid);
    if (proceso->stdin_pendiente != NULL) {
        free(proceso->stdin_pendiente);
        proceso->stdin_pendiente = NULL;
    }
    eliminar_pcb(pid);
    free(proceso);
}

bool escribir_stdin_en_memoria(int pid, int dir_logica, char* datos, int size)
{
    pthread_mutex_lock(&mutex_memory_socket);
    enviar_escritura_memoria(memory_socket, pid, dir_logica, datos, size);

    op_code ack = recibir_respuesta_de_memoria(memory_socket);
    if ((int)ack != -1) recibir_ok(memory_socket);
    pthread_mutex_unlock(&mutex_memory_socket);

    return ack == ESCRITURA_MEMORIA;
}

// WORKER DE IO: saca requests de la cola y los despacha al módulo IO


void* io_worker_thread(void* arg)
{
    t_interfaz_io* io = (t_interfaz_io*)arg;

    log_debug(logger, "Worker IO '%s' iniciado", io->nombre);

    while (io->activa) {
        sem_wait(&io->sem_cola);

        if (!io->activa) break;

        pthread_mutex_lock(&io->mutex_cola);
        t_io_request* req = queue_pop(io->cola);
        pthread_mutex_unlock(&io->mutex_cola);

        if (req == NULL) continue;

        int pid = req->proceso->pid;

        switch (req->tipo) {

            // SLEEP: mandar PID + tiempo_ms a la IO
            case IO_TYPE_SLEEP: {
                log_debug(logger,
                    "## (%d) Despachando IO SLEEP (%d ms) → IO '%s'",
                    pid, req->tiempo_ms, io->nombre);

                enviar_io_sleep(io->socket, pid, req->tiempo_ms);

                // Esperar IO_FINALIZADA de la interfaz IO
                op_code resp = recibir_codigo_operacion(io->socket);
                if ((int)resp == -1 || resp != IO_FINALIZADA) {
                    log_error(logger,
                        "IO '%s' desconectada o error esperando fin de SLEEP PID %d",
                        io->nombre, pid);
                    io->activa = false;
                    free(req);
                    goto worker_fin;
                }

                recibir_fin_io(io->socket);
                desbloquear_proceso_por_io(pid);
                break;
            }


            // STDIN
            case IO_TYPE_STDIN: {
                log_debug(logger,
                    "## (%d) Despachando IO STDIN (size=%d, dir_log=%d) → IO '%s'",
                    pid, req->stdin_size, req->stdin_dir_logica, io->nombre);

                // Mandar pedido a la interfaz STDIN
                enviar_io_stdin(io->socket, pid, req->stdin_size);

                // Esperar que la IO devuelva los bytes que lee
                op_code resp = recibir_codigo_operacion(io->socket);
                if ((int)resp == -1 || resp != IO_FINALIZADA) {
                    log_error(logger,
                        "IO '%s' error esperando datos STDIN PID %d",
                        io->nombre, pid);
                    io->activa = false;
                    free(req);
                    goto worker_fin;
                }

                // Leer pid + buffer con el helper centralizado (drena el size_t y lee los bytes)
                int pid_respuesta;
                char* buffer = recibir_datos_stdin(io->socket, &pid_respuesta, req->stdin_size);

                /* Sacar el proceso de BLOCK ANTES de escribir en memoria: si el
                 * PMP lo suspendió durante la IO, sus segmentos están en SWAP
                 * (base inválida) y escribir ahora corrompería RAM de otros
                 * procesos. En ese caso la escritura se difiere: queda en el
                 * PCB y el PMP la realiza después del swap-in. */
                t_pcb* proceso = extraer_de_block(pid);
                if (proceso == NULL) {
                    log_warning(logger,
                        "IO_FINALIZADA para PID %d pero no estaba en BLOCK", pid);
                    free(buffer);
                    break;
                }

                log_info(logger, "## (%d) finalizó IO y pasa a READY / SUSP. READY", pid);

                if (proceso->estado == SUSP_BLOCK) {
                    proceso->stdin_pendiente      = buffer;  // ownership pasa al PCB
                    proceso->stdin_pendiente_dir  = (uint32_t)req->stdin_dir_logica;
                    proceso->stdin_pendiente_size = (uint32_t)req->stdin_size;
                    proceso->estado = SUSP_READY;
                    log_info(logger, "## (%d) Pasa del estado SUSP_BLOCK al estado SUSP_READY", pid);
                    notificar_reevaluar_suspendidos();
                    break;
                }

                // Proceso en RAM: escribir ahora y pasarlo a READY
                bool escrito = escribir_stdin_en_memoria(pid,
                    req->stdin_dir_logica, buffer, req->stdin_size);
                free(buffer);

                if (!escrito) {
                    log_warning(logger,
                        "## (%d) SEG_FAULT en STDIN (dir=%d, size=%d) — finalizando por ERROR",
                        pid, req->stdin_dir_logica, req->stdin_size);
                    finalizar_pcb_por_seg_fault(proceso);
                    break;
                }

                log_info(logger, "## (%d) Pasa del estado BLOCK al estado READY", pid);
                encolar_en_ready_y_notificar(proceso);
                break;
            }

            // STDOUT
            case IO_TYPE_STDOUT: {
                log_debug(logger,
                    "## (%d) Despachando IO STDOUT (dir_log=%d, size=%d) → IO '%s'",
                    pid, req->stdout_dir_logica, req->stdout_size, io->nombre);

                /* Los bytes ya se leyeron de Memory en el momento de la syscall
                 * (con el proceso todavía en RAM). Leer acá sería incorrecto:
                 * si el proceso se suspendió esperando en la cola de la IO, sus
                 * segmentos están en SWAP y la lectura fallaría. */
                if (req->stdout_buffer == NULL) {
                    log_error(logger,
                        "STDOUT PID %d: request sin datos (dir=%d, size=%d)",
                        pid, req->stdout_dir_logica, req->stdout_size);
                    desbloquear_proceso_por_io(pid);
                    break;
                }

                // Mandar bytes a la interfaz STDOUT
                enviar_io_stdout(io->socket, pid, req->stdout_buffer, req->stdout_size);
                free(req->stdout_buffer);
                req->stdout_buffer = NULL;

                // Esperar confirmacion de fin de IO
                op_code resp = recibir_codigo_operacion(io->socket);
                if ((int)resp == -1 || resp != IO_FINALIZADA) {
                    log_error(logger,
                        "IO '%s' error esperando fin de STDOUT PID %d",
                        io->nombre, pid);
                    io->activa = false;
                    free(req);
                    goto worker_fin;
                }

                // IO mandó IO_FINALIZADA con [size_t=4][pid]; drenar con
                // recibir_fin_io (no recibir_paquete, que dejaría 4 bytes en el socket).
                recibir_fin_io(io->socket);

                desbloquear_proceso_por_io(pid);
                break;
            }
        }

        free(req);
    }

worker_fin:
    log_warning(logger, "Worker IO '%s' terminado", io->nombre);
    io->activa = false;
    return NULL;
}