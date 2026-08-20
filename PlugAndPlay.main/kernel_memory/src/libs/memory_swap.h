#ifndef MEMORY_SWAP_H
#define MEMORY_SWAP_H

#include <stdint.h>

void     swap_inicializar(uint32_t block_size, uint32_t total_blocks);
uint32_t swap_block_size(void);

// Reserva n_blocks contiguos. Retorna el primer bloque, o (uint32_t)-1 si no hay espacio.
uint32_t swap_reservar_bloques(uint32_t n_blocks);

// Envía un bloque al swap y espera el OK. Retorna 0 si ok, -1 si error.
int swap_escribir_bloque(int swap_sock, uint32_t bloque, void* buf);

// Pide un bloque al swap. *out_buf queda con un buffer malloc'd de swap_block_size
// que el caller debe liberar. Retorna 0 si ok, -1 si error (en cuyo caso *out_buf es NULL).
int swap_leer_bloque(int swap_sock, uint32_t bloque, void** out_buf);

#endif
