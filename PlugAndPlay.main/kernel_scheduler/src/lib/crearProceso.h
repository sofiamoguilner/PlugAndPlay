#ifndef CREAR_PROCESO_H_
#define CREAR_PROCESO_H_

#include "globalesKernelScheduler.h"
#include "utils/utils.h"
#include <stdbool.h>


t_pcb* crear_proceso(char* path, int prioridad);

extern int _contador_pids;
extern pthread_mutex_t _mutex_pids;



int generar_pid(void);

bool enviar_creacion_proceso_a_memory(int pid, char* path);

t_pcb* crear_proceso_inicial(char* path);

#endif

