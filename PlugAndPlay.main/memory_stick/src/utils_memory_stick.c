#include "utils_memory_stick.h"
#include "memory_stick_config.h"
#include <utils/sockets.h>
#include <utils/mensajes.h>
#include <utils/serializacion.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <commons/log.h>

extern t_log* logger;

// =============================================================================
// BUFFER DE MEMORIA FÍSICA
// =============================================================================

static uint8_t           *memoria_buffer = NULL;
static uint32_t           memoria_size   = 0;
static pthread_mutex_t    memoria_lock   = PTHREAD_MUTEX_INITIALIZER;

void inicializar_memoria_stick(uint8_t *buf, uint32_t size)
{
    memoria_buffer = buf;
    memoria_size   = size;
}

// =============================================================================
// HANDLERS DE OPERACIONES
// =============================================================================

static void manejar_lectura(int socket)
{
    validar_buffer(socket);

    int pid;
    uint32_t dir, tam;
    recv(socket, &pid, sizeof(pid), MSG_WAITALL);
    recv(socket, &dir, sizeof(dir), MSG_WAITALL);
    recv(socket, &tam, sizeof(tam), MSG_WAITALL);

    usleep(config_memory_stick->memory_delay * 1000);

    t_paquete *p = crear_paquete(LECTURA_MEMORIA);

    pthread_mutex_lock(&memoria_lock);
    if (dir + tam <= memoria_size) {
        agregar_a_paquete(p, memoria_buffer + dir, (int)tam);
        log_info(logger, "## Lectura de %u bytes", tam);
    } else {
        log_warning(logger, "Memory Stick: lectura fuera de rango (PID=%d dir=%u tam=%u size=%u)",
                    pid, dir, tam, memoria_size);
    }
    pthread_mutex_unlock(&memoria_lock);

    enviar_paquete(p, socket);
    eliminar_paquete(p);
    (void)pid;
}

static void manejar_escritura(int socket)
{
    validar_buffer(socket);

    int pid;
    uint32_t dir, tam;
    recv(socket, &pid, sizeof(pid), MSG_WAITALL);
    recv(socket, &dir, sizeof(dir), MSG_WAITALL);
    recv(socket, &tam, sizeof(tam), MSG_WAITALL);

    void *datos = malloc(tam);
    recv(socket, datos, tam, MSG_WAITALL);

    usleep(config_memory_stick->memory_delay * 1000);

    pthread_mutex_lock(&memoria_lock);
    if (dir + tam <= memoria_size) {
        memcpy(memoria_buffer + dir, datos, tam);
        log_info(logger, "## Escritura de %u bytes", tam);
    } else {
        log_warning(logger, "Memory Stick: escritura fuera de rango (PID=%d dir=%u tam=%u size=%u)",
                    pid, dir, tam, memoria_size);
    }
    pthread_mutex_unlock(&memoria_lock);

    free(datos);
    enviar_ok(ESCRITURA_MEMORIA, socket);
    (void)pid;
}

// =============================================================================
// CONEXIÓN A KERNEL MEMORY
// =============================================================================

int conectar_a_kernel_memory(char* ip_kernel_memory, char* puerto_kernel_memory,
                              uint32_t memory_size,
                              char* ip_propia, char* puerto_propio)
{
    log_debug(logger, "Intentando conexion con Kernel Memory en %s:%s...",
             ip_kernel_memory, puerto_kernel_memory);

    int memory_socket = crear_conexion(ip_kernel_memory, puerto_kernel_memory);
    if (memory_socket == -1) {
        log_error(logger, "Error en conexion con Kernel Memory");
        return -1;
    }

    enviar_handshake_memory_stick(memory_socket, memory_size, ip_propia, puerto_propio);

    log_info(logger, "## Conectado a Kernel Memory");
    return memory_socket;
}

// =============================================================================
// SERVIDOR PARA ACEPTAR CONEXIONES DE CPUs
// =============================================================================

static int server_socket = -1;

static void* memory_stick_cpu_handler(void* socket_ptr)
{
    int client_socket = *(int*)socket_ptr;
    free(socket_ptr);

    op_code cod_op = recibir_codigo_operacion(client_socket);

    if (cod_op != HANDSHAKE_CPU) {
        log_warning(logger, "Memory Stick: handshake desconocido (opcode %d)", cod_op);
        close(client_socket);
        return NULL;
    }

    int cpu_id = recibir_handshake_cpu(client_socket);
    // Kernel Memory reusa HANDSHAKE_CPU con id=-999 para su canal outbound
    // (memory_cpu.c::registrar_memory_stick). No es una CPU real.
    if (cpu_id < 0) {
        log_debug(logger, "## Conexión de Kernel Memory");
    } else {
        log_info(logger, "## CPU %d Conectada", cpu_id);
    }

    while (1) {
        op_code cod = recibir_codigo_operacion(client_socket);
        if (cod == LECTURA_MEMORIA) {
            manejar_lectura(client_socket);
        } else if (cod == ESCRITURA_MEMORIA) {
            manejar_escritura(client_socket);
        } else {
            log_debug(logger, "Memory Stick: CPU %d desconectada (opcode %d)", cpu_id, cod);
            break;
        }
    }

    close(client_socket);
    return NULL;
}

static void* memory_stick_listener_thread(void* arg)
{
    (void)arg;

    while (1) {
        int client_socket = esperar_cliente(server_socket);
        if (client_socket == -1) {
            log_warning(logger, "Memory Stick: error al aceptar cliente");
            continue;
        }

        int* socket_ptr = malloc(sizeof(int));
        if (!socket_ptr) {
            log_error(logger, "Memory Stick: sin memoria para manejar nuevo cliente");
            close(client_socket);
            continue;
        }

        *socket_ptr = client_socket;

        pthread_t thread;
        if (pthread_create(&thread, NULL, memory_stick_cpu_handler, socket_ptr) != 0) {
            log_error(logger, "Memory Stick: error creando thread para CPU");
            close(client_socket);
            free(socket_ptr);
            continue;
        }

        pthread_detach(thread);
    }

    return NULL;
}

void iniciar_servidor_memory_stick(const char* puerto)
{
    server_socket = iniciar_servidor(puerto);

    if (server_socket == -1) {
        log_error(logger, "Memory Stick: no se pudo iniciar servidor en puerto %s", puerto);
        return;
    }

    log_debug(logger, "Servidor Memory Stick escuchando en puerto %s", puerto);

    pthread_t listener;
    if (pthread_create(&listener, NULL, memory_stick_listener_thread, NULL) != 0) {
        log_error(logger, "Memory Stick: no se pudo crear hilo listener de CPUs");
        close(server_socket);
        server_socket = -1;
        return;
    }

    pthread_detach(listener);
}
