#include "memory_config.h"
#include "memory_procesos.h"
#include "memory_servidor.h"
#include "memory_scheduler.h"
#include "memory_conexiones.h"
#include "memory_bloques.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <commons/log.h>

extern t_log* logger;

static bool inicializar_logger(void);
static void inicializar_estructuras(void);


int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "uso: %s [archivo config]\n", argv[0]);
        return EXIT_FAILURE;
    }


    if (!load_config_memory(argv[1]))  {
        fprintf(stderr, "No se pudo cargar la config de Memory (%s)\n", argv[1]);
        return EXIT_FAILURE;
    }

    if (!inicializar_logger()) {
        return EXIT_FAILURE;
    }

    inicializar_estructuras();
    iniciar_servidor_memory(config_memory->puerto_escucha);

    while (1) {
        sleep(1);
    }

    log_destroy(logger);
    destroy_memory_config();
    return EXIT_SUCCESS;
}


static bool inicializar_logger(void)
{
    t_log_level level = log_level_from_string(config_memory->log_level);
    logger = log_create("kernel_memory.log", "kernel_memory", true, level);
    if (!logger) {
        perror("no se pudo crear el logger");
        destroy_memory_config();
        return false;
    }
    log_debug(logger, "iniciando módulo kernel memory...");
    return true;
}

static void inicializar_estructuras(void)
{
    procesos_init();
    bloques_init();      // arranca vacío; los bloques se agregan al conectar los Memory Sticks
    conexiones_init();
    pthread_mutex_init(&mutex_memoria, NULL);
}
