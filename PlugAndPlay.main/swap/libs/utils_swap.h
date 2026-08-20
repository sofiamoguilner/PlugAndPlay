#ifndef UTILS_SWAP_H
#define UTILS_SWAP_H

#include <stdbool.h>
#include <stdint.h>

#include <commons/log.h>

extern t_log* LOGGER;
extern int MEMORY_SOCKET;

bool swap_abrir_archivo(void);
bool swap_conectar_kernel_memory(void);
bool swap_enviar_handshake(void);
void swap_atender_operaciones(void);
void swap_cerrar_recursos(void);

/**
 * Abre el archivo SWAP con inicialización.
 * @return true si éxito, false si error
 */
bool swap_abrir_archivo(void);

/**
 * Conecta el SWAP a Kernel Memory.
 * @return true si conexión exitosa, false si falla
 */
bool swap_conectar_kernel_memory(void);

/**
 * Envía handshake al Kernel Memory.
 * @return true si éxito, false si error
 */
bool swap_enviar_handshake(void);

/**
 * Atiende operaciones de lectura/escritura desde Kernel Memory.
 * Loop principal que procesa LECTURA_BLOQUE_SWAP y ESCRITURA_BLOQUE_SWAP.
 */
void swap_atender_operaciones(void);

/**
 * Cierra conexión y archivo SWAP.
 */
void swap_cerrar_recursos(void);

#endif // UTILS_SWAP_H
