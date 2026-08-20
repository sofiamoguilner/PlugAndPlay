#ifndef CPU_SCHEDULER_H_
#define CPU_SCHEDULER_H_

#include "cpu_types.h"
#include <stdbool.h>

extern int scheduler_socket;

bool  conectar_a_scheduler(char* ip, char* puerto, int cpu_id);
int   esperar_pid_del_scheduler(void);
void* cpu_scheduler_listener_thread(void* arg);

#endif // CPU_SCHEDULER_H_
