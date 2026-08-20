#include "cpu_mmu.h"
#include "cpu_config.h"
#include "cpu_memory.h"
#include <commons/log.h>
#include <commons/collections/list.h>
#include <math.h>
#include <string.h>

extern t_log* logger;

// BUSCAR SEGMENTO EN LA TABLA
static t_segmento* buscar_segmento(t_contexto* contexto, uint32_t num_segmento)
{
    if (contexto == NULL || contexto->tabla_segmentos == NULL) return NULL;

    for (int i = 0; i < list_size(contexto->tabla_segmentos); i++) {
        t_segmento* seg = list_get(contexto->tabla_segmentos, i);
        if (seg != NULL && seg->id == num_segmento) {
            return seg;
        }
    }
    return NULL;
}

/* DESCOMPONER DIREC LOGICA
 Formula del enunciado:
  num_segmento  = floor(dir_logica / SEGMENT_MAX_SIZE)
   desplazamiento = dir_logica % SEGMENT_MAX_SIZE */

void mmu_descomponer_dir_logica(uint32_t dir_logica,
                                uint32_t* num_segmento_out,
                                uint32_t* desplazamiento_out)
{
    uint32_t tam = (uint32_t) config_cpu->segment_max_size;

    *num_segmento_out  = dir_logica / tam;
    *desplazamiento_out = dir_logica % tam;
}

// TRADUCIR: segmento + desplazamiento = direccion(es) fisica(s)
// Puede gen mas de una traduccion si hay mas Memory Sticks

bool mmu_traducir(t_contexto* contexto,
                  uint32_t num_segmento,
                  uint32_t desplazamiento,
                  uint32_t tamanio,
                  t_traduccion_fisica* resultado,
                  int* cant_resultado,
                  uint32_t* dir_fisica_global_out)
{
    *cant_resultado = 0;

    // Buscar el segmento en la tabla
    t_segmento* seg = buscar_segmento(contexto, num_segmento);
    if (seg == NULL) {
        log_debug(logger,
            "PID %d - SEG_FAULT: segmento %u no existe en la tabla",
            contexto->pid, num_segmento);
        return false;
    }

    // Verificar que no se pase del lim del segmento
    if (desplazamiento + tamanio > seg->limite) {
        log_debug(logger,
            "PID %d - SEG_FAULT: desplazamiento(%u) + tamanio(%u) = %u > limite(%u) del segmento %u",
            contexto->pid,
            desplazamiento, tamanio, desplazamiento + tamanio,
            seg->limite, num_segmento);
        return false;
    }

    uint32_t dir_fisica_global = seg->base + desplazamiento;
    if (dir_fisica_global_out) *dir_fisica_global_out = dir_fisica_global;

    /* Distribuir entre Memory Sticks (v1.1: direcciones físicas globales/planas).
     * El stick que contiene una dirección D es aquel con base <= D < base+size,
     * y el offset que se le envía es D - base. Soporta sticks de distinto tamaño
     * y operaciones que cruzan varios sticks. Mismo algoritmo que el lado de
     * kernel_memory (memory_io.c). */
    uint32_t bytes_restantes = tamanio;
    uint32_t dir_actual      = dir_fisica_global;

    while (bytes_restantes > 0) {
        int idx = cpu_buscar_stick_por_dir(dir_actual);
        if (idx < 0) {
            log_error(logger,
                "PID %d - dir física %u no pertenece a ningún Memory Stick",
                contexto->pid, dir_actual);
            return false;
        }

        uint32_t base = cpu_get_memory_stick_base(idx);
        uint32_t size = cpu_get_memory_stick_size(idx);
        uint32_t dir_local        = dir_actual - base;
        uint32_t espacio_en_stick = size - dir_local;
        uint32_t chunk = bytes_restantes < espacio_en_stick ? bytes_restantes : espacio_en_stick;

        resultado[*cant_resultado].stick_index    = idx;
        resultado[*cant_resultado].dir_fisica     = dir_local;
        resultado[*cant_resultado].bytes_en_stick = chunk;
        (*cant_resultado)++;

        dir_actual      += chunk;
        bytes_restantes -= chunk;
    }

    return true;
}


// FUNCION COMPLETA: dir_logica va a traducciones físicas

bool mmu_traducir_dir_logica(t_contexto* contexto,
                             uint32_t dir_logica,
                             uint32_t tamanio,
                             t_traduccion_fisica* resultado,
                             int* cant_resultado,
                             uint32_t* dir_fisica_global_out)
{
    uint32_t num_segmento, desplazamiento;
    mmu_descomponer_dir_logica(dir_logica, &num_segmento, &desplazamiento);

    log_debug(logger,
        "PID %d - MMU: dir_logica=%u → segmento=%u, desplazamiento=%u",
        contexto->pid, dir_logica, num_segmento, desplazamiento);

    return mmu_traducir(contexto, num_segmento, desplazamiento,
                        tamanio, resultado, cant_resultado, dir_fisica_global_out);
}