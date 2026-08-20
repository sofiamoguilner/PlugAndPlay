#ifndef CPU_MEMORY_H_
#define CPU_MEMORY_H_

#include "cpu_types.h"
#include <stdbool.h>
#include <stdint.h>

extern int memory_socket;

bool  conectar_a_memory(char* ip, char* puerto, int cpu_id);
bool  conectar_a_memory_stick(const char* ip, const char* puerto, uint32_t base, uint32_t size);
void* cpu_memory_listener_thread(void* arg);

int      cpu_get_memory_stick_count(void);
int      cpu_get_memory_stick_socket(int index);
uint32_t cpu_get_memory_stick_base(int index);
uint32_t cpu_get_memory_stick_size(int index);
// Índice del stick cuyo rango [base, base+size) contiene la dirección global, o -1.
int      cpu_buscar_stick_por_dir(uint32_t dir_global);

bool solicitar_contexto_a_memory(int pid, t_contexto** contexto);
bool actualizar_contexto_en_memory(t_contexto* contexto);

// Lectura/escritura física directa a un Memory Stick (bloqueantes), usando el
// offset local del stick. cpu_stick_leer: el caller debe liberar *out con free().
bool cpu_stick_leer(int stick_idx, int pid, uint32_t dir_local, uint32_t tam, void **out);
bool cpu_stick_escribir(int stick_idx, int pid, uint32_t dir_local, void *datos, uint32_t tam);

#endif // CPU_MEMORY_H_
