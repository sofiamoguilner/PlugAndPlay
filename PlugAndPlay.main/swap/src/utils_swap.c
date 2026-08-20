#include "utils_swap.h"
#include "swap_config.h"
#include <utils/sockets.h>
#include <utils/mensajes.h>
#include <utils/serializacion.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <commons/log.h>

extern t_swap_config* SWAP_CONFIG;
extern int MEMORY_SOCKET;
extern t_log* LOGGER;

static int SWAP_FD = -1;

// =============================================================================
// HELPERS INTERNOS
// =============================================================================

static bool is_valid_block(uint32_t block_number)
{
    uint32_t blocks_count = SWAP_CONFIG->swap_file_size / SWAP_CONFIG->block_size;
    return block_number < blocks_count;
}

static bool swap_write_block(void)
{
    uint32_t block_number = 0;
    void* block_data = NULL;
    recibir_escritura_bloque_swap(MEMORY_SOCKET, &block_number, &block_data, SWAP_CONFIG->block_size);

    log_info(LOGGER, "## Escritura del bloque: %u", block_number);

    int status = 0;
    if (!is_valid_block(block_number)) {
        status = -1;
    } else {
        off_t offset = (off_t)block_number * (off_t)SWAP_CONFIG->block_size;
        if (pwrite(SWAP_FD, block_data, SWAP_CONFIG->block_size, offset) != (ssize_t)SWAP_CONFIG->block_size) {
            status = -1;
        }
    }

    free(block_data);

    enviar_resultado_swap(MEMORY_SOCKET, status);
    return true;
}

static bool swap_read_block(void)
{
    uint32_t block_number = 0;
    block_number = recibir_lectura_bloque_swap(MEMORY_SOCKET);

    log_info(LOGGER, "## Lectura del bloque: %u", block_number);

    int status = 0;
    void* block_data = calloc(1, SWAP_CONFIG->block_size);
    if (block_data == NULL) {
        return false;
    }

    if (!is_valid_block(block_number)) {
        status = -1;
    } else {
        off_t offset = (off_t)block_number * (off_t)SWAP_CONFIG->block_size;
        if (pread(SWAP_FD, block_data, SWAP_CONFIG->block_size, offset) != (ssize_t)SWAP_CONFIG->block_size) {
            status = -1;
        }
    }

    enviar_bloque_swap(MEMORY_SOCKET, status, block_data, SWAP_CONFIG->block_size);

    free(block_data);
    return true;
}

// =============================================================================
// GESTIÓN DE ARCHIVO SWAP
// =============================================================================

bool swap_abrir_archivo(void)
{
    SWAP_FD = open(SWAP_CONFIG->swap_file_path, O_RDWR | O_CREAT, 0644);
    if (SWAP_FD == -1) {
        return false;
    }

    if (ftruncate(SWAP_FD, (off_t)SWAP_CONFIG->swap_file_size) == -1) {
        close(SWAP_FD);
        SWAP_FD = -1;
        return false;
    }

    return true;
}

// =============================================================================
// CONEXIÓN A KERNEL MEMORY
// =============================================================================

bool swap_conectar_kernel_memory(void)
{
    MEMORY_SOCKET = crear_conexion(SWAP_CONFIG->memory_ip, SWAP_CONFIG->memory_port);
    return MEMORY_SOCKET != -1;
}

bool swap_enviar_handshake(void)
{
    enviar_handshake_swap(MEMORY_SOCKET, SWAP_CONFIG->block_size, SWAP_CONFIG->swap_file_size);

    log_info(LOGGER, "## Conectado a Kernel Memory");
    return true;
}

// =============================================================================
// ATENCIÓN DE OPERACIONES
// =============================================================================

void swap_atender_operaciones(void)
{
    while (MEMORY_SOCKET != -1) {
        op_code op = recibir_codigo_operacion(MEMORY_SOCKET);
        if ((int)op == -1) {
            log_warning(LOGGER, "Desconexión de Kernel Memory");
            break;
        }

        log_trace(LOGGER, "## Operación recibida: %d", op);

        switch (op) {
            case LECTURA_BLOQUE_SWAP:
                if (!swap_read_block()) {
                    log_warning(LOGGER, "Error durante lectura de bloque en SWAP");
                    close(MEMORY_SOCKET);
                    MEMORY_SOCKET = -1;
                }
                break;

            case ESCRITURA_BLOQUE_SWAP:
                if (!swap_write_block()) {
                    log_warning(LOGGER, "Error durante escritura de bloque en SWAP");
                    close(MEMORY_SOCKET);
                    MEMORY_SOCKET = -1;
                }
                break;

            default:
                log_warning(LOGGER, "Operación desconocida: %d", op);
                break;
        }
    }
}

// =============================================================================
// LIMPIEZA DE RECURSOS
// =============================================================================

void swap_cerrar_recursos(void)
{
    if (MEMORY_SOCKET != -1) {
        close(MEMORY_SOCKET);
        MEMORY_SOCKET = -1;
    }

    if (SWAP_FD != -1) {
        close(SWAP_FD);
        SWAP_FD = -1;
    }
}
