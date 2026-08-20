#include "memory_stick_config.h"
#include <commons/config.h>
#include <commons/log.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

t_config_memory_stick* config_memory_stick = NULL;
t_log* logger = NULL;

bool load_config_memory_stick(const char* path)
{
    t_config* config_file = config_create((char*)path);
    if (config_file == NULL)
    {
        return false;
    }

    config_memory_stick = malloc(sizeof(t_config_memory_stick));
    if (config_memory_stick == NULL)
    {
        config_destroy(config_file);
        return false;
    }

    config_memory_stick->log_level        = strdup(config_get_string_value(config_file,"LOG_LEVEL"));

    // Conexion hacia Kernel Memory (orquestador)
    config_memory_stick->kernel_memory_ip   = strdup(config_get_string_value(config_file,"MEMORY_IP"));
    config_memory_stick->kernel_memory_port = strdup(config_get_string_value(config_file,"MEMORY_PORT"));

    // Datos propios del Memory Stick, para que las CPUs puedan conectarse
    config_memory_stick->ip_propia     = strdup(config_get_string_value(config_file,"STICK_IP"));
    config_memory_stick->puerto_propio = strdup(config_get_string_value(config_file,"STICK_PORT"));

    config_memory_stick->memory_size  = (uint32_t)config_get_int_value(config_file, "MEMORY_SIZE");
    config_memory_stick->memory_delay = (uint32_t)config_get_int_value(config_file, "MEMORY_DELAY");

    config_destroy(config_file);

    if (config_memory_stick->log_level        == NULL ||
        config_memory_stick->kernel_memory_ip   == NULL ||
        config_memory_stick->kernel_memory_port == NULL ||
        config_memory_stick->ip_propia          == NULL ||
        config_memory_stick->puerto_propio      == NULL ||
        config_memory_stick->memory_size        == 0)
    {
        destroy_memory_stick_config();
        return false;
    }

    return true;
    
}

bool init_logger_memory_stick()
{
    logger = log_create("memory_stick.log", "memory_stick",true,log_level_from_string(config_memory_stick->log_level));

    return (logger != NULL);
}

void destroy_memory_stick_config(){
    if (config_memory_stick != NULL)
    {
        free(config_memory_stick->log_level);
        free(config_memory_stick->kernel_memory_ip);
        free(config_memory_stick->kernel_memory_port);
        free(config_memory_stick->ip_propia);
        free(config_memory_stick->puerto_propio);
        free(config_memory_stick);
        config_memory_stick = NULL;
    }
    
    if (logger != NULL )
    {
        log_destroy(logger);
        logger = NULL;
    }
    
}