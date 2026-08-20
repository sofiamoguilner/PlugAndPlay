#ifndef COMUNICACIONES_SCHEDULER_H
#define COMUNICACIONES_SCHEDULER_H

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <commons/log.h>
#include "globalesKernelScheduler.h"  // para t_pcb y MAX_CPUS

// Variables globales
extern int memory_socket;
extern int server_socket;
extern t_log* logger;


bool conectar_a_kernel_memory(char* ip, char* puerto);




void* scheduler_handshake_handler(void* socket_ptr);


void iniciar_servidor_scheduler(char* puerto);

int  enviar_proceso_a_cpu(t_pcb* proceso);

#endif