#include "scheduler_conexiones.h"
#include "scheduler_config.h"
#include "crearProceso.h"
#include "planificadorLargoPlazo.h"
#include "planificadorMedianoPlazo.h"
#include "planificadorCortoPlazo.h"
#include "globalesKernelScheduler.h"
#include "colaIO.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    char* path_config = "/home/utnso/tp-2026-1c-La-Serpiente-Shnissugah/kernel_scheduler/kernel_scheduler.conf";
    char* path_proceso_inicial = "/home/utnso/tp-2026-1c-La-Serpiente-Shnissugah/kernel_scheduler/src/1.script";

    if (argc >= 2) {
        path_config = argv[1];
    }

    if (argc >= 3) {
        path_proceso_inicial = argv[2];
    }

    if (!load_config_scheduler(path_config)) {
        fprintf(stderr, "error al cargar la configuración del scheduler\n");
        return EXIT_FAILURE;
    }

    t_log_level level = log_level_from_string(config_scheduler->log_level);

    logger = log_create( "kernel_scheduler.log","kernel_scheduler", true, level);

   
    if (!logger) {
        destroy_config_scheduler();
        return EXIT_FAILURE;
    }

    conectar_a_kernel_memory(config_scheduler->memory_ip, config_scheduler->memory_port);

    log_debug(logger, "Conexión con Kernel Memory establecida");

    iniciar_servidor_scheduler(config_scheduler->puerto_escucha);

    // Inicializar todos los recursos globales del scheduler (colas, listas, semáforos)
    inicializar_recursos_kernel_scheduler();

    // Inicializar colas multinivel solo si el algoritmo es CMN
    if (obtener_algoritmo_global() == ALGORITMO_CMN) {
        inicializar_colas_multinivel(config_scheduler->cantidad_colas, config_scheduler);
    }
    
    // Inicializar tabla de interfaces
    inicializar_tabla_ios();

    // Lanzar el planificador a largo plazo una vez que los recursos están listos
    iniciar_planificador_largo_plazo();

    // Lanzar el planificador a mediano plazo (BLOCK→SUSP_BLOCK y SUSP_READY→READY)
    iniciar_planificador_mediano_plazo();

    // Lanzar el planificador a corto plazo para despachar READY -> EXEC
    iniciar_planificador_corto_plazo();

    // Recién ahora crear el proceso inicial, que encola en NEW y hace sem_post(&sem_new)
    crear_proceso_inicial(path_proceso_inicial);

    

    while (1) {
        sleep(1);
    }

    destroy_config_scheduler();

    return EXIT_SUCCESS;
}
