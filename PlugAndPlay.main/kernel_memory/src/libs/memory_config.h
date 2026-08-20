#ifndef MEMORY_CONFIG_H
#define MEMORY_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char* log_level;
    int segment_max_size;
    char* allocation_strategy;
    int instruction_delay;
    int compaction_delay;
    char* scripts_basepath;
    char* puerto_escucha;
} t_config_memory;

extern t_config_memory* config_memory;

bool load_config_memory(const char* path);
void destroy_memory_config(void);

#endif
