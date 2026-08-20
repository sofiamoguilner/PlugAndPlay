#ifndef SCHEDULER_CONFIG_H_
#define SCHEDULER_CONFIG_H_

#include <commons/log.h>
#include <commons/config.h>
#include <stdbool.h>

#define MAX_QUEUES 10

typedef struct t_config_scheduler {
    char* log_level;
    char* planification_algorithm;
    char* queue_preemption;
    int rr_quantum;
    int suspension_timeout;
    char* puerto_escucha;
    char* memory_ip;
    char* memory_port;
    
    /* Colas multinivel */
    int cantidad_colas;
    char* algoritmos_colas[MAX_QUEUES];  /* FIFO o RR para cada cola */
} t_config_scheduler;

extern t_config_scheduler* config_scheduler;
extern t_log* logger;

bool load_config_scheduler(char* path);
bool init_logger_scheduler();
void destroy_config_scheduler();

#endif