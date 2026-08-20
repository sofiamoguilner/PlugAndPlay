#include "memory_swap.h"
#include <utils/mensajes.h>
#include <pthread.h>
#include <stdlib.h>

static uint32_t        block_size   = 0;
static uint32_t        next_block   = 0;
static uint32_t        total_blocks = 0;
static pthread_mutex_t reserva_lock = PTHREAD_MUTEX_INITIALIZER;

void swap_inicializar(uint32_t bs, uint32_t tb)
{
    pthread_mutex_lock(&reserva_lock);
    block_size   = bs;
    total_blocks = tb;
    next_block   = 0;
    pthread_mutex_unlock(&reserva_lock);
}

uint32_t swap_block_size(void)
{
    return block_size;
}

uint32_t swap_reservar_bloques(uint32_t n_blocks)
{
    pthread_mutex_lock(&reserva_lock);
    if (next_block + n_blocks > total_blocks) {
        pthread_mutex_unlock(&reserva_lock);
        return (uint32_t)-1;
    }
    uint32_t inicio = next_block;
    next_block += n_blocks;
    pthread_mutex_unlock(&reserva_lock);
    return inicio;
}

int swap_escribir_bloque(int swap_sock, uint32_t bloque, void* buf)
{
    if (swap_sock < 0) return -1;
    enviar_escritura_bloque_swap(swap_sock, bloque, buf, block_size);
    return recibir_resultado_swap(swap_sock);
}

int swap_leer_bloque(int swap_sock, uint32_t bloque, void** out_buf)
{
    *out_buf = NULL;
    if (swap_sock < 0) return -1;
    enviar_lectura_bloque_swap(swap_sock, bloque);
    int status = 0;
    void* data = recibir_bloque_swap(swap_sock, &status, block_size);
    if (status != 0 || !data) {
        free(data);
        return -1;
    }
    *out_buf = data;
    return 0;
}
