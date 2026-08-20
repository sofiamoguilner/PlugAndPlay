#include "cpu_config.h"
#include "cpu_scheduler.h"
#include "cpu_memory.h"
#include "cpu_ciclo.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Uso: %s [Archivo Config] [CPU_ID]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* config_path = argv[1];
    int   cpu_id      = atoi(argv[2]);

    if (cpu_id <= 0) {
        fprintf(stderr, "CPU_ID invalido: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    // ---- config ----
    if (!load_config_cpu(config_path)) {
        fprintf(stderr, "Error al cargar la configuración de CPU\n");
        return EXIT_FAILURE;
    }

    // ---- logger ----
    if (!init_logger_cpu(cpu_id)) {
        perror("Error al crear logger");
        destroy_cpu_config();
        return EXIT_FAILURE;
    }

    log_debug(logger, "Iniciando módulo CPU %d", cpu_id);

    // ---- conexiones ----
    if (!conectar_a_memory(config_cpu->memory_ip, config_cpu->memory_port, cpu_id)) {
        log_error(logger, "No se puede iniciar CPU sin conexión a Memory");
        goto cleanup;
    }
    
    if (!conectar_a_scheduler(config_cpu->scheduler_ip, config_cpu->scheduler_port, cpu_id)) {
        log_error(logger, "No se puede iniciar CPU sin conexión a Scheduler");
        goto cleanup;
    }


    log_debug(logger, "CPU %d lista para recibir procesos", cpu_id);

    // ---- loop principal ----
    while (1) {
        int pid = esperar_pid_del_scheduler();

        if (pid < 0) {
            log_error(logger, "Error recibiendo PID del Scheduler");
            break;
        }

        log_debug(logger, "## Recibido PID %d", pid);

        ejecutar_ciclo_instruccion(pid);
    }

cleanup:
    log_debug(logger, "Cerrando módulo CPU %d", cpu_id);
    if (scheduler_socket != -1) close(scheduler_socket);
    if (memory_socket    != -1) close(memory_socket);
    log_destroy(logger);
    destroy_cpu_config();
    return EXIT_SUCCESS;
}