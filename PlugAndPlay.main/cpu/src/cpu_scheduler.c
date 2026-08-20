#include "cpu_scheduler.h"
#include <utils/sockets.h>
#include <utils/mensajes.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <pthread.h>
#include <commons/log.h>

extern t_log* logger;

int scheduler_socket = -1;

t_conexiones_cpu cpu_connections = {
    .scheduler_socket   = -1,
    .memory_socket      = -1,
    .memory_stick_count = 0,
    .cpu_id_global      = -1
};

// =============================================================================
// HELPERS INTERNOS
// =============================================================================

static void spawn_detached(void* (*fn)(void*), void* arg, const char* nombre)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, fn, arg) != 0)
        log_error(logger, "Error creando thread: %s", nombre);
    else
        pthread_detach(thread);
}

#define RETARDO_REINTENTO_MS 2000
#define LOG_CADA_N_INTENTOS  10

static int esperar_conexion(const char* ip, const char* puerto, const char* modulo)
{
    int intentos = 0;
    int sock     = -1;

    log_debug(logger, "Conectando a %s en %s:%s...", modulo, ip, puerto);

    while ((sock = crear_conexion((char*)ip, (char*)puerto)) == -1) {
        intentos++;
        if (intentos % LOG_CADA_N_INTENTOS == 0) {
            log_warning(logger,
                        "%s no disponible en %s:%s — intento %d, reintentando en %d ms...",
                        modulo, ip, puerto, intentos, RETARDO_REINTENTO_MS);
        }
        usleep(RETARDO_REINTENTO_MS * 1000);
    }

    log_debug(logger, "Conexion a %s establecida (intento %d)", modulo, intentos + 1);
    return sock;
}

// =============================================================================
// LISTENER DEL SCHEDULER
// =============================================================================

void* cpu_scheduler_listener_thread(void* arg)
{
    (void)arg;

    while (1) {
        op_code codigo = recibir_codigo_operacion(scheduler_socket);

        if ((int)codigo == -1) {
            /* Sin Scheduler no hay sistema: si siguiéramos, el ciclo de
             * ejecución continuaría fetcheando y ejecutando instrucciones
             * contra memoria para siempre (p.ej. tras un BSOD). Cortar todo. */
            log_error(logger, "## Kernel Scheduler desconectado — finalizando CPU");
            exit(EXIT_FAILURE);
        }

        switch (codigo) {
            case EJECUTAR_PROCESO: {
                int pid = recibir_pid(scheduler_socket);
                cpu_connections.pid_a_ejecutar = pid;
                sem_post(&cpu_connections.sem_proceso_listo);
                break;
            }
            case INTERRUPCION: {
                int pid, motivo;
                recibir_interrupcion(scheduler_socket, &pid, &motivo);
                pthread_mutex_lock(&cpu_connections.interrupcion_lock);
                int actual = cpu_connections.pid_en_ejecucion;
                if (pid == actual) {
                    cpu_connections.interrupcion_pendiente = true;
                    cpu_connections.motivo_interrupcion = motivo;
                    log_debug(logger, "## Interrupción recibida para PID %d (motivo %d)", pid, motivo);
                } else if (actual == -1) {
                    log_trace(logger,
                        "## Interrupción tardía descartada (PID %d, CPU idle) — race benigna con devolución de proceso",
                        pid);
                } else {
                    log_warning(logger,
                        "## Interrupción descartada: recibida para PID %d, ejecutando PID %d",
                        pid, actual);
                }
                pthread_mutex_unlock(&cpu_connections.interrupcion_lock);
                break;
            }
            default: {
                unsigned char peek_buf[32];
                ssize_t peeked = recv(scheduler_socket, peek_buf, sizeof(peek_buf), MSG_PEEK | MSG_DONTWAIT);
                char hex[3 * sizeof(peek_buf) + 1] = {0};
                if (peeked > 0) {
                    for (ssize_t i = 0; i < peeked; i++) {
                        snprintf(hex + i * 3, 4, "%02x ", peek_buf[i]);
                    }
                }
                log_error(logger,
                    "Opcode inesperado del Scheduler: %d — stream desincronizado, cerrando conexion. "
                    "Bytes siguientes (peek %zd): %s",
                    (int)codigo, peeked, hex);
                close(scheduler_socket);
                scheduler_socket = -1;
                /* Sin canal con el Scheduler la CPU no puede seguir operando. */
                exit(EXIT_FAILURE);
            }
        }
    }

    return NULL;
}

// =============================================================================
// CONEXIÓN AL SCHEDULER
// =============================================================================

bool conectar_a_scheduler(char* ip, char* puerto, int cpu_id)
{
    scheduler_socket = esperar_conexion(ip, puerto, "Kernel Scheduler");
    enviar_handshake_cpu(scheduler_socket, cpu_id);
    log_debug(logger, "## CPU %d conectada a Scheduler", cpu_id);

    pthread_mutex_init(&cpu_connections.interrupcion_lock, NULL);
    sem_init(&cpu_connections.sem_proceso_listo, 0, 0);
    cpu_connections.interrupcion_pendiente = false;
    cpu_connections.motivo_interrupcion = MOTIVO_QUANTUM;
    cpu_connections.pid_en_ejecucion = -1;

    spawn_detached(cpu_scheduler_listener_thread, NULL, "cpu_scheduler_listener");
    return true;
}

// =============================================================================
// COMUNICACIÓN CON EL SCHEDULER
// =============================================================================

int esperar_pid_del_scheduler(void)
{
    sem_wait(&cpu_connections.sem_proceso_listo);
    return cpu_connections.pid_a_ejecutar;
}
