#include "memory_stick_config.h"
#include "memory_stick_conexiones.h"
#include "utils_memory_stick.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char* argv[]) {

    if (argc < 2) {
        fprintf(stderr, "uso: %s [archivo config] [tamanio]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* config_path = argv[1];

    if (!load_config_memory_stick(config_path)) {
        fprintf(stderr, "error al cargar la configuración del memory stick\n");
        return EXIT_FAILURE;
    }

    // argv[2] sobreescribe el MEMORY_SIZE del config (opcional)
    uint32_t memory_size = config_memory_stick->memory_size;
    if (argc >= 3) {
        memory_size = (uint32_t)atoi(argv[2]);
    }

    t_log_level level = log_level_from_string(config_memory_stick->log_level);

    // Un .log por instancia, derivado del nombre del config
    // (configs/memory_stick_1.config -> memory_stick_1.log)
    char log_path[256];
    const char* base = strrchr(config_path, '/');
    base = base ? base + 1 : config_path;
    snprintf(log_path, sizeof(log_path), "%s", base);
    char* ext = strrchr(log_path, '.');
    if (ext) *ext = '\0';
    strncat(log_path, ".log", sizeof(log_path) - strlen(log_path) - 1);

    logger = log_create(log_path, "memory_stick", true, level);

    if (!logger) {
        perror("no se pudo crear el logger");
        destroy_memory_stick_config();
        return EXIT_FAILURE;
    }

    log_debug(logger, "iniciando módulo memory stick (tamanio: %u bytes)...", memory_size);

    uint8_t *memoria = calloc(memory_size, 1);
    if (!memoria) {
        log_error(logger, "no se pudo reservar %u bytes de memoria", memory_size);
        destroy_memory_stick_config();
        return EXIT_FAILURE;
    }

    inicializar_memoria_stick(memoria, memory_size);

    // El servidor debe estar escuchando ANTES de notificar a kernel_memory,
    // porque kernel_memory conecta de vuelta inmediatamente al recibir el handshake.
    iniciar_servidor_memory_stick(config_memory_stick->puerto_propio);

    int memory_socket = conectar_a_kernel_memory(config_memory_stick->kernel_memory_ip,
                                                 config_memory_stick->kernel_memory_port,
                                                 memory_size,
                                                 config_memory_stick->ip_propia,
                                                 config_memory_stick->puerto_propio);

    if (memory_socket == -1) {
        log_error(logger, "no se pudo conectar a kernel memory");
        free(memoria);
        destroy_memory_stick_config();
        return EXIT_FAILURE;
    }

    // mantener modulo activo
    while (1) {
        sleep(1);
    }

    close(memory_socket);
    free(memoria);
    log_debug(logger, "cerrando modulo de memory stick...");
    destroy_memory_stick_config();

    return EXIT_SUCCESS;
}