#include "memory_config.h"
#include <commons/config.h>
#include <commons/log.h>
#include <commons/string.h>
#include <stdlib.h>
#include <string.h>

t_config_memory* config_memory = NULL;
t_log* logger = NULL;

bool load_config_memory(const char* path)
{
    t_config* config_file = config_create((char*)path);
    if (config_file == NULL) {
        return false;
    }

    config_memory = malloc(sizeof(t_config_memory));
    if (config_memory == NULL) {
        config_destroy(config_file);
        return false;
    }

    config_memory->log_level           = strdup(config_get_string_value(config_file, "LOG_LEVEL"));
    config_memory->segment_max_size    = config_get_int_value(config_file, "SEGMENT_MAX_SIZE");
    config_memory->allocation_strategy = strdup(config_get_string_value(config_file, "ALLOCATION_STRATEGY"));
    config_memory->instruction_delay   = config_get_int_value(config_file, "INSTRUCTION_DELAY");
    config_memory->compaction_delay    = config_get_int_value(config_file, "COMPACTION_DELAY");
    config_memory->scripts_basepath    = strdup(config_get_string_value(config_file, "SCRIPTS_BASEPATH"));
    config_memory->puerto_escucha      = strdup(config_get_string_value(config_file, "PUERTO_ESCUCHA"));
    config_destroy(config_file);
    return true;
}

void destroy_memory_config(void)
{
    if (config_memory != NULL) {
        free(config_memory->log_level);
        free(config_memory->allocation_strategy);
        free(config_memory->scripts_basepath);
        free(config_memory->puerto_escucha);
        free(config_memory);
        config_memory = NULL;
    }
    if (logger != NULL) {
        log_destroy(logger);
        logger = NULL;
    }
}
