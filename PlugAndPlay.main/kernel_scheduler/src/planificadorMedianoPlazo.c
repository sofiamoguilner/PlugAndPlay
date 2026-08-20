#include "planificadorMedianoPlazo.h"
#include "globalesKernelScheduler.h"
#include "scheduler_config.h"
#include "utils_kernel_scheduler.h"
#include "memory_protocol.h"
#include "colaIO.h"
#include <utils/mensajes.h>
#include <utils/serializacion.h>
#include <commons/log.h>
#include <commons/collections/list.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

extern t_log* logger;
extern int    memory_socket;

/* ======================================================================
 * Globales del PMP
 * ====================================================================== */

t_list*         lista_suspendidos = NULL;
pthread_mutex_t mutex_suspendidos = PTHREAD_MUTEX_INITIALIZER;
sem_t           sem_reevaluar_suspendidos;

/* ======================================================================
 * LISTA DE SUSPENDIDOS
 * ====================================================================== */

void agregar_a_lista_suspendidos(t_pcb* pcb)
{
    t_proceso_suspendido* ps = malloc(sizeof(t_proceso_suspendido));
    ps->pcb               = pcb;
    ps->tiempo_suspension = time(NULL);

    pthread_mutex_lock(&mutex_suspendidos);
    list_add(lista_suspendidos, ps);
    pthread_mutex_unlock(&mutex_suspendidos);
}

void remover_de_lista_suspendidos(int pid)
{
    pthread_mutex_lock(&mutex_suspendidos);
    for (int i = 0; i < list_size(lista_suspendidos); i++) {
        t_proceso_suspendido* ps = list_get(lista_suspendidos, i);
        if (ps->pcb->pid == pid) {
            list_remove(lista_suspendidos, i);
            free(ps);
            break;
        }
    }
    pthread_mutex_unlock(&mutex_suspendidos);
}

/* ======================================================================
 * TRIGGER: RE-EVALUACIÓN DE SUSPENDIDOS
 * ====================================================================== */

void notificar_reevaluar_suspendidos(void)
{
    sem_post(&sem_reevaluar_suspendidos);
}

/* ======================================================================
 * TIMER DE SUSPENSIÓN: BLOCK → SUSP_BLOCK
 *
 * Se lanza un thread detached por cada proceso que entra a BLOCK.
 * Al expirar SUSPENSION_TIMEOUT ms, si el proceso aún está en BLOCK,
 * se lo mueve a SUSP_BLOCK y se le pide a Memory que vuelque sus
 * segmentos al SWAP.
 * ====================================================================== */

typedef struct { int pid; uint32_t epoch; } t_suspension_timer_arg;

static void* suspension_timer_thread(void* arg)
{
    t_suspension_timer_arg* a = (t_suspension_timer_arg*)arg;
    int pid = a->pid;
    uint32_t epoch = a->epoch;
    free(a);

    if (config_scheduler == NULL || config_scheduler->suspension_timeout <= 0) {
        return NULL;
    }

    usleep((useconds_t)config_scheduler->suspension_timeout * 1000UL);

    if (sistema_corrupto) return NULL;

    /*
     * Bajo mutex_block buscamos al proceso en cola_procesos_block.
     * Si ya salió (IO terminó antes del timeout) no hacemos nada.
     * El epoch invalida timers de episodios BLOCK anteriores: si el proceso
     * salió de BLOCK y volvió a entrar (nueva IO), block_epoch cambió y este
     * timer viejo NO debe suspenderlo — su episodio ya terminó.
     * Si sigue en el mismo BLOCK, cambiamos el estado a SUSP_BLOCK en el mismo
     * bloque crítico para evitar la race con desbloquear_proceso_por_io.
     */
    t_pcb* pcb = NULL;

    pthread_mutex_lock(&mutex_block);
    for (int i = 0; i < list_size(cola_procesos_block->elements); i++) {
        t_pcb* p = list_get(cola_procesos_block->elements, i);
        if (p != NULL && p->pid == pid && p->estado == BLOCK && p->block_epoch == epoch) {
            p->estado = SUSP_BLOCK;
            /* Volcado a SWAP en vuelo: extraer_de_block no removerá el PCB de
             * la cola hasta que este flag vuelva a true (evita liberar/mover el
             * PCB mientras memory copia los segmentos). */
            p->suspendido_en_swap = false;
            pcb = p;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_block);

    if (pcb == NULL) {
        /* El proceso ya salió de BLOCK (IO completó) antes del timeout,
         * o este timer era de un episodio BLOCK anterior (epoch viejo). */
        return NULL;
    }

    log_info(logger, "## (%d) Pasa del estado BLOCK al estado SUSP_BLOCK", pid);

    /* Agregar a la lista ANTES de llamar a memory para evitar race condition:
     * la IO puede terminar (→SUSP_READY) mientras esperamos la respuesta de
     * memory. Si la lista está vacía cuando el PMP se despierta, no des-suspende.
     * Al agregar primero y notificar después, garantizamos que el proceso está
     * en la lista cuando el PMP evalúe. */
    agregar_a_lista_suspendidos(pcb);

    /* Pedirle a Kernel Memory que mueva los segmentos del proceso al SWAP */
    pthread_mutex_lock(&mutex_memory_socket);
    enviar_suspender_proceso(memory_socket, pid);
    op_code ack = recibir_respuesta_de_memoria(memory_socket);
    if ((int)ack != -1) recibir_ok(memory_socket);
    pthread_mutex_unlock(&mutex_memory_socket);

    /* Volcado terminado: destrabar a extraer_de_block (worker de IO), que
     * espera este flag para poder sacar al proceso de la cola de BLOCK. */
    pthread_mutex_lock(&mutex_block);
    for (int i = 0; i < list_size(cola_procesos_block->elements); i++) {
        t_pcb* p = list_get(cola_procesos_block->elements, i);
        if (p != NULL && p->pid == pid) {
            p->suspendido_en_swap = true;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_block);

    /* Si la IO ya terminó mientras esperábamos la respuesta de memory,
     * el proceso ya está en SUSP_READY — notificar al PMP para que lo evalue. */
    notificar_reevaluar_suspendidos();

    return NULL;
}

void notificar_proceso_en_block(t_pcb* pcb, uint32_t epoch)
{
    if (pcb == NULL) return;
    if (config_scheduler == NULL || config_scheduler->suspension_timeout <= 0) return;

    t_suspension_timer_arg* arg = malloc(sizeof(t_suspension_timer_arg));
    arg->pid = pcb->pid;
    arg->epoch = epoch;

    pthread_t t;
    if (pthread_create(&t, NULL, suspension_timer_thread, arg) != 0) {
        log_error(logger, "PID %d: no se pudo crear timer de suspensión", pcb->pid);
        free(arg);
        return;
    }
    pthread_detach(t);
}

/* ======================================================================
 * LOOP PRINCIPAL: SUSP_READY → READY
 * ====================================================================== */

/* Comparador para list_sort: devuelve true si 'a' debe ir ANTES que 'b'.
 * Orden: prioridad_dinamica asc (menor = mayor prioridad),
 *        luego tiempo_suspension asc (el más viejo primero). */
static bool comparar_suspendidos(void* a, void* b)
{
    t_proceso_suspendido* pa = (t_proceso_suspendido*)a;
    t_proceso_suspendido* pb = (t_proceso_suspendido*)b;

    if (pa->pcb->prioridad_dinamica != pb->pcb->prioridad_dinamica)
        return pa->pcb->prioridad_dinamica < pb->pcb->prioridad_dinamica;

    return pa->tiempo_suspension < pb->tiempo_suspension;
}

/* Optimista: intenta des-suspender sin consultar espacio.
 * Si no hay espacio, memory rechaza el DES_SUSPENDER y el proceso
 * permanece en SUSP_READY hasta el próximo evento de memoria liberada. */

/* Itera los procesos en SUSP_READY y des-suspende los que caben. */
static void intentar_des_suspender_susp_ready(void)
{
    /* Construir snapshot de candidatos SUSP_READY */
    pthread_mutex_lock(&mutex_suspendidos);
    int n = list_size(lista_suspendidos);
    log_debug(logger, "[PMP-DBG] lista_suspendidos tiene %d entradas", n);
    if (n == 0) {
        pthread_mutex_unlock(&mutex_suspendidos);
        return;
    }

    t_list* candidatos = list_create();
    for (int i = 0; i < n; i++) {
        t_proceso_suspendido* ps = list_get(lista_suspendidos, i);
        log_debug(logger, "[PMP-DBG] PID %d estado=%d (SUSP_BLOCK=%d SUSP_READY=%d)",
                 ps->pcb->pid, ps->pcb->estado, SUSP_BLOCK, SUSP_READY);
        if (ps->pcb->estado == SUSP_READY) {
            list_add(candidatos, ps);
        }
    }
    pthread_mutex_unlock(&mutex_suspendidos);

    log_debug(logger, "[PMP-DBG] candidatos SUSP_READY: %d", list_size(candidatos));
    if (list_size(candidatos) == 0) {
        list_destroy(candidatos);
        return;
    }

    list_sort(candidatos, comparar_suspendidos);

    log_debug(logger,
        "Mediano Plazo: intentando des-suspender %d proceso(s) en SUSP_READY",
        list_size(candidatos));

    for (int i = 0; i < list_size(candidatos) && !sistema_corrupto; i++) {
        t_proceso_suspendido* ps = list_get(candidatos, i);
        t_pcb* pcb               = ps->pcb;

        log_debug(logger,
            "## PID %d: intentando des-suspender (total_memoria=%u bytes)",
            pcb->pid, pcb->total_memoria);

        /* Pedirle a Kernel Memory que restaure los segmentos desde SWAP.
         * Si no hay espacio (u otro error), memory responde SEGMENTATION_FAULT
         * y el proceso queda en SUSP_READY para reintentar más adelante. */
        pthread_mutex_lock(&mutex_memory_socket);
        enviar_des_suspender_proceso(memory_socket, pcb->pid);
        op_code ack = recibir_respuesta_de_memoria(memory_socket);
        if ((int)ack != -1) recibir_ok(memory_socket);
        pthread_mutex_unlock(&mutex_memory_socket);

        if (ack != DES_SUSPENDER_PROCESO) {
            log_debug(logger, "## PID %d: no hay espacio para des-suspender", pcb->pid);
            continue;
        }

        /* Swap-in completado: los segmentos vuelven a tener base válida. */
        remover_de_lista_suspendidos(pcb->pid);

        /* Si la IO STDIN terminó mientras estaba suspendido, la escritura del
         * resultado quedó diferida en el PCB: hacerla AHORA, con el proceso ya
         * en RAM, antes de que vuelva a ejecutar. */
        if (pcb->stdin_pendiente != NULL) {
            bool escrito = escribir_stdin_en_memoria(pcb->pid,
                (int)pcb->stdin_pendiente_dir,
                pcb->stdin_pendiente,
                (int)pcb->stdin_pendiente_size);

            free(pcb->stdin_pendiente);
            pcb->stdin_pendiente = NULL;

            if (!escrito) {
                log_warning(logger,
                    "## (%d) SEG_FAULT en STDIN diferido (dir=%u, size=%u) — finalizando por ERROR",
                    pcb->pid, pcb->stdin_pendiente_dir, pcb->stdin_pendiente_size);
                finalizar_pcb_por_seg_fault(pcb);
                continue;
            }
        }

        log_info(logger, "## (%d) Pasa del estado SUSP_READY al estado READY", pcb->pid);
        encolar_en_ready_y_notificar(pcb);
    }

    list_destroy(candidatos);  /* solo destruye la lista snapshot, no los elementos */
}

void planificadorMedianoPlazo(void)
{
    log_debug(logger, "Planificador a Mediano Plazo iniciado");

    while (!sistema_corrupto) {
        sem_wait(&sem_reevaluar_suspendidos);

        if (sistema_corrupto) break;

        intentar_des_suspender_susp_ready();
    }

    log_debug(logger, "Planificador a Mediano Plazo terminado");
}

void* _planificador_mediano_plazo_thread(void* arg)
{
    (void)arg;
    planificadorMedianoPlazo();
    return NULL;
}

/* ======================================================================
 * INICIALIZACIÓN
 * ====================================================================== */

void iniciar_planificador_mediano_plazo(void)
{
    lista_suspendidos = list_create();
    sem_init(&sem_reevaluar_suspendidos, 0, 0);

    pthread_t thread;
    if (pthread_create(&thread, NULL, _planificador_mediano_plazo_thread, NULL) != 0) {
        log_error(logger, "Error al crear thread del Planificador a Mediano Plazo");
        return;
    }
    pthread_detach(thread);
    log_debug(logger, "Thread Planificador a Mediano Plazo creado");
}