#ifndef SWAP_CONFIG_H
#define SWAP_CONFIG_H

#include <commons/log.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>


extern t_log* LOGGER;

typedef struct {
    char* log_level;
    char* memory_ip;
    char* memory_port;
    char* swap_file_path;
    uint32_t swap_file_size;
    uint32_t block_size;
} t_swap_config;

extern t_swap_config* SWAP_CONFIG;

bool load_swap_config(const char* config_path);
bool register_sigint_handler(void);
void destroy_swap_config(void);

#endif