#ifndef MEMORY_STICK_CONFIG_H_
#define MEMORY_STICK_CONFIG_H_

#include <commons/config.h>
#include <commons/log.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct 
{
    char* log_level;
    // Datos de conexion hacia Kernel Memory (orquestador)
    char* kernel_memory_ip;
    char* kernel_memory_port;

    // Datos propios del Memory Stick (donde se conectan las CPUs)
    char* ip_propia;
    char* puerto_propio;

    uint32_t memory_size;
    uint32_t memory_delay;
} t_config_memory_stick ;

extern t_config_memory_stick* config_memory_stick;
extern t_log* logger;

bool load_config_memory_stick(const char* path);
bool init_logger_memory_stick();
void destroy_memory_stick_config();

#endif
