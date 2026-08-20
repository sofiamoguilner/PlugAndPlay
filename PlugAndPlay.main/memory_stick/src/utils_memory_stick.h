#ifndef UTILS_MEMORY_STICK_H
#define UTILS_MEMORY_STICK_H

#include <stdint.h>

/**
 * Conecta el Memory Stick a Kernel Memory.
 * @param ip_kernel_memory dirección del Kernel Memory
 * @param puerto_kernel_memory puerto del Kernel Memory
 * @param memory_size tamaño total de memoria del stick
 * @param ip_propia IP propia del memory stick para que KM sepa contactarlo
 * @param puerto_propio puerto propio del memory stick
 * @return socket de conexión, o -1 si falla
 */
int conectar_a_kernel_memory(char* ip_kernel_memory, char* puerto_kernel_memory,
                              uint32_t memory_size,
                              char* ip_propia, char* puerto_propio);

/**
 * Inicializa el buffer de memoria física del stick.
 * Debe llamarse antes de iniciar_servidor_memory_stick().
 * @param buf  puntero al buffer reservado con calloc()
 * @param size tamaño del buffer en bytes
 */
void inicializar_memoria_stick(uint8_t *buf, uint32_t size);

/**
 * Inicia servidor para aceptar conexiones de CPUs.
 * Lanza thread listener en background.
 * @param puerto puerto donde escuchar
 */
void iniciar_servidor_memory_stick(const char* puerto);

#endif // UTILS_MEMORY_STICK_H
