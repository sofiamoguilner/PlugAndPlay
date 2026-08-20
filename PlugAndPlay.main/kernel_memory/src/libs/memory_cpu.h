#ifndef MEMORY_CPU_H
#define MEMORY_CPU_H

#include "memory_tipos.h"

// =============================================================================
// CONEXIÓN
// =============================================================================

void registrar_cpu(int client_socket);

// =============================================================================
// THREAD RECEPTOR
// =============================================================================

void* memory_receptor_cpu_thread(void* arg);

// =============================================================================
// HANDLERS DE PEDIDOS CPU
// =============================================================================

void manejar_obtener_instruccion(int socket, int cpu_id);
void manejar_obtener_contexto_cpu(int socket, int cpu_id);
void manejar_actualizar_contexto_cpu(int socket, int cpu_id);

// =============================================================================
// CONEXIÓN DE MEMORY STICKS
// =============================================================================

void registrar_memory_stick(int client_socket);
void notificar_sticks_a_cpu(int cpu_socket, int cpu_id);
void notificar_cpus_de_nuevo_stick(uint32_t tamanio, uint32_t base_fisica, char* ip, char* puerto);

#endif // MEMORY_CPU_H
