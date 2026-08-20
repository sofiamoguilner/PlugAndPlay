#include "cpu_config.h"
#include <commons/config.h>
#include <commons/log.h>
#include <commons/string.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

t_config_cpu *config_cpu;
t_log *logger;

bool load_config_cpu(char *path) {
    t_config *config_file = config_create(path);
    if (config_file == NULL) {
        return false;
    }

    config_cpu = malloc(sizeof(t_config_cpu));
    if (config_cpu == NULL) {
        config_destroy(config_file);
        return false;
    }

    config_cpu->log_level = strdup(config_get_string_value(config_file, "LOG_LEVEL"));
    config_cpu->scheduler_ip = strdup(config_get_string_value(config_file, "SCHEDULER_IP"));
    config_cpu->scheduler_port = strdup(config_get_string_value(config_file, "SCHEDULER_PORT"));
    config_cpu->memory_ip = strdup(config_get_string_value(config_file, "MEMORY_IP"));
    config_cpu->memory_port = strdup(config_get_string_value(config_file, "MEMORY_PORT"));

// TAM_MAX_SEGMENTO — default 1024 si no esta en el config de CPU
    if (config_has_property(config_file, "SEGMENT_MAX_SIZE")) {
        config_cpu->segment_max_size = (uint32_t) config_get_int_value(config_file, "SEGMENT_MAX_SIZE");
    } else {
        config_cpu->segment_max_size = 1024;
    }

    config_destroy(config_file);
    return true;
}

bool init_logger_cpu(int cpu_id) {
    char log_filename[256];
    snprintf(log_filename, sizeof(log_filename), "cpu_%d.log", cpu_id);
    logger = log_create(log_filename, "CPU", true, log_level_from_string(config_cpu->log_level));
    return (logger != NULL);
}

void destroy_cpu_config() {
    if (config_cpu != NULL) {
        free(config_cpu->log_level);
        free(config_cpu->scheduler_ip);
        free(config_cpu->scheduler_port);
        free(config_cpu->memory_ip);
        free(config_cpu->memory_port);
        free(config_cpu);
    }
    if (logger != NULL) {
        log_destroy(logger);
    }
}

