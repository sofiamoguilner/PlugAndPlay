#include "scheduler_config.h"
#include <commons/config.h>
#include <commons/log.h>
#include <commons/string.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int contar_algoritmos(char** algoritmos)
{
    int cantidad = 0;

    if (algoritmos == NULL) {
        return 0;
    }

    while (algoritmos[cantidad] != NULL) {
        cantidad++;
    }

    return cantidad;
}

static void cargar_algoritmos_legacy(t_config* config_file)
{
    for (int i = 0; i < config_scheduler->cantidad_colas; i++) {
        char key[32];
        snprintf(key, sizeof(key), "ALGORITMO_COLA_%d", i);

        if (config_has_property(config_file, key)) {
            config_scheduler->algoritmos_colas[i] = strdup(config_get_string_value(config_file, key));
        } else {
            config_scheduler->algoritmos_colas[i] = strdup(config_scheduler->planification_algorithm);
        }
    }
}

static void cargar_algoritmos_moderno(t_config* config_file)
{
    char** algoritmos = config_get_array_value(config_file, "QUEUES_ALGORITHMS");
    int cantidad_algoritmos = contar_algoritmos(algoritmos);

    for (int i = 0; i < config_scheduler->cantidad_colas; i++) {
        if (i < cantidad_algoritmos && algoritmos[i] != NULL) {
            config_scheduler->algoritmos_colas[i] = strdup(algoritmos[i]);
        } else {
            config_scheduler->algoritmos_colas[i] = strdup(config_scheduler->planification_algorithm);
        }
    }

    if (algoritmos != NULL) {
        string_array_destroy(algoritmos);
    }
}

t_config_scheduler *config_scheduler = NULL;
t_log *logger = NULL;

bool load_config_scheduler(char *path) {
    t_config *config_file = config_create(path);
    if (config_file == NULL) {
        return false;
    }

    config_scheduler = malloc(sizeof(t_config_scheduler));
    if (config_scheduler == NULL) {
        config_destroy(config_file);
        return false;
    }

    // VALIDACIONES BÁSICAS
    if (!config_has_property(config_file, "LOG_LEVEL") ||
        !config_has_property(config_file, "PLANIFICATION_ALGORITHM") ||
        !config_has_property(config_file, "QUEUE_PREEMPTION") ||
        !config_has_property(config_file, "RR_QUANTUM") ||
        !config_has_property(config_file, "SUSPENSION_TIMEOUT") ||
        !config_has_property(config_file, "PUERTO_ESCUCHA") ||
        !config_has_property(config_file, "MEMORY_IP") ||
        !config_has_property(config_file, "MEMORY_PORT")) {

        config_destroy(config_file);
        free(config_scheduler);
        return false;
    }

    // CARGA BÁSICA
    config_scheduler->log_level = strdup(config_get_string_value(config_file, "LOG_LEVEL"));
    config_scheduler->planification_algorithm = strdup(config_get_string_value(config_file, "PLANIFICATION_ALGORITHM"));
    config_scheduler->queue_preemption = strdup(config_get_string_value(config_file, "QUEUE_PREEMPTION"));
    config_scheduler->rr_quantum = config_get_int_value(config_file, "RR_QUANTUM");
    config_scheduler->suspension_timeout = config_get_int_value(config_file, "SUSPENSION_TIMEOUT");
    config_scheduler->puerto_escucha = strdup(config_get_string_value(config_file, "PUERTO_ESCUCHA"));
    config_scheduler->memory_ip = strdup(config_get_string_value(config_file, "MEMORY_IP"));
    config_scheduler->memory_port = strdup(config_get_string_value(config_file, "MEMORY_PORT"));

    // CARGA DE COLAS MULTINIVEL (con valores por defecto)
    config_scheduler->cantidad_colas = 1;
    for (int i = 0; i < MAX_QUEUES; i++) {
        config_scheduler->algoritmos_colas[i] = NULL;
    }

    // Si existe CANTIDAD_COLAS, cargarla
    if (config_has_property(config_file, "CANTIDAD_COLAS")) {
        config_scheduler->cantidad_colas = config_get_int_value(config_file, "CANTIDAD_COLAS");
        if (config_scheduler->cantidad_colas > MAX_QUEUES) {
            config_scheduler->cantidad_colas = MAX_QUEUES;
        }
    }

    // Si no vino CANTIDAD_COLAS pero sí QUEUES_ALGORITHMS, inferimos la cantidad de colas
    if (!config_has_property(config_file, "CANTIDAD_COLAS") &&
        config_has_property(config_file, "QUEUES_ALGORITHMS")) {
        char** algoritmos = config_get_array_value(config_file, "QUEUES_ALGORITHMS");
        int cantidad_algoritmos = contar_algoritmos(algoritmos);

        config_scheduler->cantidad_colas = cantidad_algoritmos;
        if (config_scheduler->cantidad_colas <= 0) {
            config_scheduler->cantidad_colas = 1;
        }
        if (config_scheduler->cantidad_colas > MAX_QUEUES) {
            config_scheduler->cantidad_colas = MAX_QUEUES;
        }

        if (algoritmos != NULL) {
            string_array_destroy(algoritmos);
        }
    }

    // Cargar algoritmo de cada cola
    if (config_has_property(config_file, "QUEUES_ALGORITHMS")) {
        cargar_algoritmos_moderno(config_file);
    } else {
        cargar_algoritmos_legacy(config_file);
    }

    config_destroy(config_file);
    return true;
}

bool init_logger_scheduler() {
    if (config_scheduler == NULL || config_scheduler->log_level == NULL) {
        return false;
    }

    t_log_level level = log_level_from_string(config_scheduler->log_level);

    logger = log_create(
        "kernel_scheduler.log",
        "kernel_scheduler",
        true,
        level
    );

    return (logger != NULL);
}

void destroy_config_scheduler() {
    if (config_scheduler != NULL) {
        free(config_scheduler->log_level);
        free(config_scheduler->planification_algorithm);
        free(config_scheduler->queue_preemption);
        free(config_scheduler->puerto_escucha);
        free(config_scheduler->memory_ip);
        free(config_scheduler->memory_port);

        for (int i = 0; i < MAX_QUEUES; i++) {
            if (config_scheduler->algoritmos_colas[i] != NULL) {
                free(config_scheduler->algoritmos_colas[i]);
            }
        }

        free(config_scheduler);
        config_scheduler = NULL;
    }

    if (logger != NULL) {
        log_destroy(logger);
        logger = NULL;
    }
}