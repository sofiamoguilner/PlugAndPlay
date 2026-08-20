#include "scheduler_conexiones.h"
#include "scheduler_config.h"
#include "utils_kernel_scheduler.h"
#include <utils/sockets.h>
#include <utils/utils.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

extern t_log* logger;

int memory_socket   = -1;
int server_socket   = -1;

// =============================================================================
// SERVIDOR DEL SCHEDULER
// =============================================================================

void iniciar_servidor_scheduler(char* puerto) {
    server_socket = iniciar_servidor(puerto);

    if (server_socket == -1) {
        log_error(logger, "No se pudo iniciar el servidor del Kernel Scheduler");
        exit(EXIT_FAILURE);
    }

    log_debug(logger, "Servidor Scheduler escuchando en puerto %s", puerto);

    pthread_mutex_init(&scheduler_connections.lock, NULL);

    spawn_detached(scheduler_listener_thread, NULL, "scheduler_listener");
}