#ifndef cpu_config_h_
#define cpu_config_h_

#include <commons/log.h>
#include <commons/config.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char* log_level;
    char* scheduler_ip;
    char* scheduler_port;
    char* memory_ip;
    char* memory_port;
    uint32_t segment_max_size;  // TAM_MAX_SEGMENTO para la MMU
} t_config_cpu;

extern t_config_cpu* config_cpu;
extern t_log* logger;

bool load_config_cpu(char* path);
bool init_logger_cpu(int cpu_id);
void destroy_cpu_config();

#endif