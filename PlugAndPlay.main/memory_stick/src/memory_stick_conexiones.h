#ifndef MEMORY_STICK_CONEXIONES_H_
#define MEMORY_STICK_CONEXIONES_H_

#include <stdbool.h>
#include <stdint.h>
#include <commons/log.h>
#include <utils/utils.h>

extern t_log* logger;

// Conexion saliente hacia Kernel Memory (orquestador)
int conectar_a_kernel_memory(char* ip_kernel_memory, char* puerto_kernel_memory,
                             uint32_t memory_size,
                             char* ip_propia, char* puerto_propio);

// Servidor donde se conectan las CPUs a este Memory Stick
void iniciar_servidor_memory_stick(const char* puerto);

#endif