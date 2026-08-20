#include "memory_bloques.h"
#include "memory_tipos.h"
#include "memory_config.h"
#include <commons/collections/list.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static t_list* bloques_libres = NULL;

void bloques_init(void)
{
    bloques_libres = list_create();
}

void bloques_agregar(uint32_t base, uint32_t tamanio)
{
    // Un stick nuevo puede ser contiguo al espacio libre que ya había, así que
    // se inserta con merge (mismo camino que liberar) y no como bloque suelto.
    bloques_liberar(base, tamanio);
}

uint32_t bloques_asignar(uint32_t tamanio)
{
    int      mejor_idx = -1;
    uint32_t mejor_tam = 0;
    // El enunciado define los valores BEST / WORST. Aceptamos también los alias
    // BEST_FIT / WORST_FIT por compatibilidad: cualquier cosa que empiece con
    // "WORST" es Worst Fit; el resto (incl. BEST/BEST_FIT) es Best Fit.
    bool     worst_fit = (config_memory->allocation_strategy != NULL &&
                          strncmp(config_memory->allocation_strategy, "WORST", 5) == 0);

    for (int i = 0; i < list_size(bloques_libres); i++) {
        t_bloque_libre* b = list_get(bloques_libres, i);
        if (b->tamanio < tamanio) continue;

        if (mejor_idx == -1) {
            mejor_idx = i;
            mejor_tam = b->tamanio;
        } else if (!worst_fit && b->tamanio < mejor_tam) {
            mejor_idx = i;
            mejor_tam = b->tamanio;
        } else if (worst_fit && b->tamanio > mejor_tam) {
            mejor_idx = i;
            mejor_tam = b->tamanio;
        }
    }

    if (mejor_idx == -1) return (uint32_t)-1;

    t_bloque_libre* elegido = list_get(bloques_libres, mejor_idx);
    uint32_t base = elegido->base;

    if (elegido->tamanio == tamanio) {
        free(list_remove(bloques_libres, mejor_idx));
    } else {
        elegido->base    += tamanio;
        elegido->tamanio -= tamanio;
    }

    return base;
}

void bloques_liberar(uint32_t base, uint32_t tamanio)
{
    t_bloque_libre* nuevo = malloc(sizeof(t_bloque_libre));
    nuevo->base    = base;
    nuevo->tamanio = tamanio;
    list_add(bloques_libres, nuevo);

    // Merge: fusionar bloques contiguos (puede requerir múltiples pasadas)
    bool mergeado = true;
    while (mergeado) {
        mergeado = false;
        for (int i = 0; i < list_size(bloques_libres) && !mergeado; i++) {
            t_bloque_libre* a = list_get(bloques_libres, i);
            for (int j = 0; j < list_size(bloques_libres) && !mergeado; j++) {
                if (i == j) continue;
                t_bloque_libre* b = list_get(bloques_libres, j);
                if (a->base + a->tamanio == b->base) {
                    a->tamanio += b->tamanio;
                    free(list_remove(bloques_libres, j));
                    mergeado = true;
                }
            }
        }
    }
}

uint32_t bloques_calcular_espacio_libre(void)
{
    uint32_t total = 0;
    for (int i = 0; i < list_size(bloques_libres); i++) {
        t_bloque_libre* b = list_get(bloques_libres, i);
        total += b->tamanio;
    }
    return total;
}

void bloques_reset_a_unico(uint32_t base, uint32_t tamanio)
{
    list_destroy_and_destroy_elements(bloques_libres, free);
    bloques_libres = list_create();
    if (tamanio > 0) {
        t_bloque_libre* unico = malloc(sizeof(t_bloque_libre));
        unico->base    = base;
        unico->tamanio = tamanio;
        list_add(bloques_libres, unico);
    }
}
