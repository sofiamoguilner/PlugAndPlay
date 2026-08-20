#ifndef MEMORY_CONEXIONES_H
#define MEMORY_CONEXIONES_H

#include <stdint.h>
#include <stdbool.h>

void conexiones_init(void);

// Scheduler
void conexiones_set_scheduler_socket(int s);
int  conexiones_get_scheduler_socket(void);

// Swap
void conexiones_set_swap_socket(int s);
int  conexiones_get_swap_socket(void);

// CPUs (retornan -1 si falló, índice si ok)
int  conexiones_agregar_cpu(int socket, int cpu_id);
void conexiones_invalidar_cpu_por_id(int cpu_id);
int  conexiones_cantidad_cpus(void);
int  conexiones_cpu_socket_en(int idx);
int  conexiones_cpu_id_en(int idx);

// Memory Sticks (retorna -1 si falló, índice si ok)
int  conexiones_agregar_stick(int socket, uint32_t tamanio, char* ip, char* puerto, uint32_t* out_base_fisica);
int  conexiones_cantidad_sticks(void);
int  conexiones_stick_outbound_socket(int idx);
void conexiones_stick_outbound_lock(int idx);
void conexiones_stick_outbound_unlock(int idx);
void conexiones_stick_set_outbound_socket(int idx, int sock);
void conexiones_stick_invalidate_por_indice(int idx, char** out_ip, char** out_puerto);
uint32_t conexiones_stick_base_fisica(int idx);
uint32_t conexiones_stick_tamanio(int idx);
const char* conexiones_stick_ip(int idx);
const char* conexiones_stick_puerto(int idx);

// Suma de tamaños de todos los sticks registrados.
uint32_t conexiones_tamanio_total_ram(void);

// Para I/O cross-stick: localiza el stick que contiene una dirección física.
// Retorna índice o -1. Caller obtiene snapshot de campos por separado vía getters.
int conexiones_buscar_stick_por_dir(uint32_t dir_fisica);

#endif
