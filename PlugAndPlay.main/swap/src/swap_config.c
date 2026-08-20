#include "swap_config.h"
#include <commons/config.h>
#include <stdlib.h>
#include <string.h>
#include "utils_swap.h"

t_swap_config *SWAP_CONFIG = NULL;

static sig_atomic_t sigint_count = 0;

void sigint_handler(int signum) {
    if (signum == SIGINT) {
        sigint_count++;

        log_warning(LOGGER, "Señal SIGINT recibida. Iniciando cierre controlado...");

        // Si es la segunda vez, abortar inmediatamente
        if (sigint_count >= 2) {
            log_error(LOGGER, "Segunda señal SIGINT recibida. Abortando inmediatamente...");
            abort();
        }

        log_destroy(LOGGER);
        destroy_swap_config();
        swap_cerrar_recursos();
        exit(EXIT_SUCCESS);
    }
}

bool register_sigint_handler() {
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        log_error(LOGGER, "Error al registrar el handler para SIGINT");
        return false;
    }
    return true;
}

bool load_swap_config(const char *config_path) {
    t_config *config_file = config_create((char *)config_path);
    if (config_file == NULL) {
        return false;
    }

    SWAP_CONFIG = malloc(sizeof(t_swap_config));
    if (SWAP_CONFIG == NULL) {
        config_destroy(config_file);
        return false;
    }

    SWAP_CONFIG->log_level = strdup(config_get_string_value(config_file, "LOG_LEVEL"));
    SWAP_CONFIG->memory_ip = strdup(config_get_string_value(config_file, "MEMORY_IP"));
    SWAP_CONFIG->memory_port = strdup(config_get_string_value(config_file, "MEMORY_PORT"));
    SWAP_CONFIG->swap_file_path = strdup(config_get_string_value(config_file, "SWAP_FILE_PATH"));
    SWAP_CONFIG->swap_file_size = (uint32_t)config_get_int_value(config_file, "SWAP_FILE_SIZE");
    SWAP_CONFIG->block_size = (uint32_t)config_get_int_value(config_file, "BLOCK_SIZE");

    config_destroy(config_file);

    if (SWAP_CONFIG->log_level == NULL ||
        SWAP_CONFIG->memory_ip == NULL ||
        SWAP_CONFIG->memory_port == NULL ||
        SWAP_CONFIG->swap_file_path == NULL ||
        SWAP_CONFIG->swap_file_size == 0 ||
        SWAP_CONFIG->block_size == 0 ||
        SWAP_CONFIG->block_size > SWAP_CONFIG->swap_file_size) {
        destroy_swap_config();
        return false;
    }

    return true;
}

void destroy_swap_config(void) {
    if (SWAP_CONFIG == NULL) {
        return;
    }

    free(SWAP_CONFIG->log_level);
    free(SWAP_CONFIG->memory_ip);
    free(SWAP_CONFIG->memory_port);
    free(SWAP_CONFIG->swap_file_path);
    free(SWAP_CONFIG);
    SWAP_CONFIG = NULL;
}