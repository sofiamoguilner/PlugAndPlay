#include "memory_compactacion.h"
#include "memory_procesos.h"
#include "memory_bloques.h"
#include "memory_conexiones.h"
#include "memory_io.h"
#include "memory_config.h"
#include "memory_tipos.h"
#include <commons/log.h>
#include <commons/collections/list.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

extern t_log* logger;

static atomic_int en_progreso = 0;

bool compactacion_en_progreso(void)
{
    return atomic_load(&en_progreso) != 0;
}

void compactacion_marcar_inicio(void)
{
    atomic_store(&en_progreso, 1);
}

void compactacion_marcar_fin(void)
{
    atomic_store(&en_progreso, 0);
}

typedef struct { t_segmento* seg; t_contexto_ejecucion* proc; } t_seg_entrada;

static int comparar_seg_por_base(const void* a, const void* b)
{
    const t_seg_entrada* ea = (const t_seg_entrada*)a;
    const t_seg_entrada* eb = (const t_seg_entrada*)b;
    if (ea->seg->base < eb->seg->base) return -1;
    if (ea->seg->base > eb->seg->base) return  1;
    return 0;
}

void compactar_memoria(void)
{
    int total_procs = procesos_cantidad();
    int total_segs  = 0;
    for (int i = 0; i < total_procs; i++) {
        t_contexto_ejecucion* p = procesos_obtener_en(i);
        total_segs += list_size(p->segmentos);
    }

    if (total_segs == 0) return;

    t_seg_entrada* entradas = malloc(sizeof(t_seg_entrada) * total_segs);
    int idx = 0;
    for (int i = 0; i < total_procs; i++) {
        t_contexto_ejecucion* p = procesos_obtener_en(i);
        for (int j = 0; j < list_size(p->segmentos); j++) {
            t_segmento* s = list_get(p->segmentos, j);
            // Los segmentos de procesos suspendidos (base == -1) están en SWAP:
            // no ocupan RAM y no se compactan. Incluirlos corría el cursor de más
            // y les daba una base "real" que después nadie liberaba.
            if (s->base == (uint32_t)-1) continue;
            entradas[idx].seg  = s;
            entradas[idx].proc = p;
            idx++;
        }
    }

    total_segs = idx;
    if (total_segs == 0) {
        free(entradas);
        uint32_t ram = conexiones_tamanio_total_ram();
        bloques_reset_a_unico(0, ram);
        return;
    }

    qsort(entradas, total_segs, sizeof(t_seg_entrada), comparar_seg_por_base);

    uint32_t cursor = 0;
    for (int i = 0; i < total_segs; i++) {
        t_segmento* seg = entradas[i].seg;
        uint32_t old_base = seg->base;

        if (old_base != cursor) {
            void* datos = memory_io_leer(entradas[i].proc->pid, old_base, seg->limite);
            if (datos) {
                memory_io_escribir(entradas[i].proc->pid, cursor, seg->limite, datos);
                free(datos);
            }
            log_debug(logger,
                     "## Compactación: Segmento %u - PID %d - Dir. Anterior: 0x%X - Dir. Nueva: 0x%X",
                     seg->id, entradas[i].proc->pid, old_base, cursor);
            seg->base = cursor;
        }
        cursor += seg->limite;
    }

    free(entradas);

    uint32_t total_ram = conexiones_tamanio_total_ram();
    bloques_reset_a_unico(cursor, total_ram > cursor ? total_ram - cursor : 0);

    usleep((useconds_t)config_memory->compaction_delay * 1000);
}
