#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <utils/utils.h>

#include "utils_swap.h"
#include "swap_config.h"

t_log *LOGGER = NULL;
int MEMORY_SOCKET = -1;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "uso: %s [Archivo Config]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (!load_swap_config(argv[1])) {
        fprintf(stderr, "error al cargar la configuracion de swap\n");
        return EXIT_FAILURE;
    }

    // Inicializar logger
    LOGGER = log_create("swap.log", "SWAP", true, log_level_from_string(SWAP_CONFIG->log_level));
    if (!LOGGER) {
        perror("Error al crear logger");
        destroy_swap_config();
        return EXIT_FAILURE;
    }

    if (!register_sigint_handler()) {
        log_error(LOGGER, "No se pudo registrar el handler para SIGINT");
        log_destroy(LOGGER);
        destroy_swap_config();
        return -1;
    }

    if (!swap_abrir_archivo()) {
        log_error(LOGGER, "No se pudo abrir o inicializar el archivo SWAP");
        log_destroy(LOGGER);
        destroy_swap_config();
        return EXIT_FAILURE;
    }

    log_debug(LOGGER, "Iniciando módulo SWAP");
    log_debug(LOGGER, "Conectando a Kernel Memory en %s:%s", SWAP_CONFIG->memory_ip, SWAP_CONFIG->memory_port);

    if (!swap_conectar_kernel_memory()) {
        log_error(LOGGER, "No se pudo conectar a Kernel Memory");
        swap_cerrar_recursos();
        log_destroy(LOGGER);
        destroy_swap_config();
        return EXIT_FAILURE;
    }

    if (!swap_enviar_handshake()) {
        log_error(LOGGER, "Error enviando handshake a Kernel Memory");
        swap_cerrar_recursos();
        log_destroy(LOGGER);
        destroy_swap_config();
        return EXIT_FAILURE;
    }

    swap_atender_operaciones();

    log_debug(LOGGER, "Cerrando módulo SWAP");
    swap_cerrar_recursos();
    log_destroy(LOGGER);
    destroy_swap_config();

    return EXIT_SUCCESS;
}
