#include "memory_servidor.h"
#include "memory_cpu.h"
#include "memory_scheduler.h"
#include "memory_conexiones.h"
#include "memory_swap.h"
#include <utils/sockets.h>
#include <utils/mensajes.h>
#include <commons/log.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

extern t_log* logger;

static int server_socket = -1;

// =============================================================================
// UTILIDADES DE THREADING
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
// REGISTRO DE SWAP
// =============================================================================

void registrar_swap(int client_socket)
{
    uint32_t block_size = 0, total_size = 0;
    recibir_handshake_swap(client_socket, &block_size, &total_size);
    log_debug(logger, "## Modulo SWAP conectado - block_size:%u total_swap:%u", block_size, total_size);

    swap_inicializar(block_size, total_size / block_size);
    conexiones_set_swap_socket(client_socket);
}

// =============================================================================
// DISPATCHER DE HANDSHAKE: identifica el módulo y lo deriva
// =============================================================================

static void* memory_handshake_handler(void* socket_ptr)
{
    int client_socket = *(int*)socket_ptr;
    free(socket_ptr);

    op_code cod_op = recibir_codigo_operacion(client_socket);
    if ((int)cod_op == -1) {
        close(client_socket);
        return NULL;
    }

    switch (cod_op) {
        case HANDSHAKE_SCHEDULER_A_MEMORY:
            registrar_scheduler(client_socket);
            break;
        case HANDSHAKE_CPU:
            registrar_cpu(client_socket);
            break;
        case HANDSHAKE_MEMORY_STICK:
            registrar_memory_stick(client_socket);
            break;
        case HANDSHAKE_SWAP:
            registrar_swap(client_socket);
            break;
        default:
            log_warning(logger, "Operacion desconocida en handshake: %d", cod_op);
            close(client_socket);
            break;
    }

    return NULL;
}

// =============================================================================
// LISTENER PRINCIPAL: acepta conexiones y lanza handshake por cada una
// =============================================================================

static void* _memory_listener_thread(void* arg)
{
    (void)arg;

    while (1) {
        int client_socket = esperar_cliente(server_socket);
        if (client_socket == -1) break;

        int* socket_ptr = malloc(sizeof(int));
        *socket_ptr = client_socket;

        spawn_detached(memory_handshake_handler, socket_ptr, "handshake");
    }

    return NULL;
}

// =============================================================================
// INICIO DEL SERVIDOR
// =============================================================================

void iniciar_servidor_memory(char* puerto)
{
    server_socket = iniciar_servidor(puerto);
    if (server_socket == -1) {
        log_error(logger, "No se pudo iniciar el servidor del Kernel Memory");
        exit(EXIT_FAILURE);
    }

    log_debug(logger, "Servidor Kernel Memory escuchando en puerto %s", puerto);

    spawn_detached(_memory_listener_thread, NULL, "listener");
}
