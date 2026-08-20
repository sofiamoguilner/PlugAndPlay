#include "memory_procesos.h"
#include "memory_config.h"
#include <utils/serializacion.h>
#include <utils/mensajes.h>
#include <commons/log.h>
#include <commons/collections/list.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>

extern t_log* logger;

pthread_mutex_t mutex_memoria;

static t_list* procesos = NULL;

// =============================================================================
// GESTIÓN DE PROCESOS
// =============================================================================

void procesos_init(void)
{
    procesos = list_create();
}

t_contexto_ejecucion* procesos_buscar(int pid)
{
    for (int i = 0; i < list_size(procesos); i++) {
        t_contexto_ejecucion* p = list_get(procesos, i);
        if (p->pid == pid) return p;
    }
    return NULL;
}

t_contexto_ejecucion* buscar_proceso(int pid)
{
    return procesos_buscar(pid);
}

void procesos_agregar(t_contexto_ejecucion* p)
{
    list_add(procesos, p);
}

int procesos_cantidad(void)
{
    return list_size(procesos);
}

t_contexto_ejecucion* procesos_obtener_en(int idx)
{
    return list_get(procesos, idx);
}

void procesos_quitar_en(int idx)
{
    list_remove(procesos, idx);
}

void actualizar_proceso_desde_contexto(t_contexto_ejecucion* proc, t_contexto* ctx)
{
    proc->registros = ctx->registros;

    list_destroy_and_destroy_elements(proc->segmentos, free);
    proc->segmentos = list_create();

    int cantidad = list_size(ctx->tabla_segmentos);
    for (int i = 0; i < cantidad; i++) {
        t_segmento* orig  = list_get(ctx->tabla_segmentos, i);
        t_segmento* copia = malloc(sizeof(t_segmento));
        *copia = *orig;
        list_add(proc->segmentos, copia);
    }
}

// =============================================================================
// SERIALIZACIÓN DE CONTEXTO
// =============================================================================

void enviar_contexto_proceso(int socket, t_contexto_ejecucion* proc)
{
    int cantidad = list_size(proc->segmentos);

    t_paquete* p = crear_paquete(OBTENER_CONTEXTO);
    agregar_a_paquete(p, &proc->registros, sizeof(proc->registros));
    agregar_a_paquete(p, &cantidad,        sizeof(cantidad));
    for (int i = 0; i < cantidad; i++) {
        t_segmento* seg = list_get(proc->segmentos, i);
        agregar_a_paquete(p, seg, sizeof(t_segmento));
    }
    enviar_paquete(p, socket);
    eliminar_paquete(p);
}

t_contexto* recibir_contexto_actualizar(int socket)
{
    validar_buffer(socket);
    t_contexto* ctx = malloc(sizeof(t_contexto));
    recv(socket, &ctx->pid,       sizeof(ctx->pid),       MSG_WAITALL);
    recv(socket, &ctx->registros, sizeof(ctx->registros), MSG_WAITALL);
    int cantidad = 0;
    recv(socket, &cantidad,       sizeof(cantidad),        MSG_WAITALL);
    ctx->tabla_segmentos = list_create();
    for (int i = 0; i < cantidad; i++) {
        t_segmento* seg = malloc(sizeof(t_segmento));
        recv(socket, seg, sizeof(t_segmento), MSG_WAITALL);
        list_add(ctx->tabla_segmentos, seg);
    }
    return ctx;
}

// =============================================================================
// CARGA DE SCRIPTS
// =============================================================================

// Devuelve un puntero al primer caracter no-blanco de la línea, o NULL si la
// línea es vacía o un comentario ('#' como primer caracter no-blanco).
static char* skip_blanco_y_comentario(char* linea)
{
    char* p = linea;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') return NULL;
    return p;
}

char** cargar_script(const char* path, int* out_cantidad)
{
    if (!path) {
        *out_cantidad = 0;
        return NULL;
    }

    FILE* f = fopen(path, "r");
    if (!f) {
        log_error(logger, "No se puede abrir archivo de script: %s", path);
        *out_cantidad = 0;
        return NULL;
    }

    int   capacidad = 16;
    int   cantidad  = 0;
    char** arr      = malloc(sizeof(char*) * capacidad);
    if (!arr) {
        log_error(logger, "Error al reservar memoria para instrucciones");
        fclose(f);
        *out_cantidad = 0;
        return NULL;
    }

    char linea[256];
    while (fgets(linea, sizeof(linea), f)) {
        char* contenido = skip_blanco_y_comentario(linea);
        if (!contenido) continue;
        contenido[strcspn(contenido, "\n")] = '\0';

        if (cantidad == capacidad) {
            capacidad *= 2;
            char** nuevo = realloc(arr, sizeof(char*) * capacidad);
            if (!nuevo) {
                log_error(logger, "Error al expandir arreglo de instrucciones");
                for (int i = 0; i < cantidad; i++) free(arr[i]);
                free(arr);
                fclose(f);
                *out_cantidad = 0;
                return NULL;
            }
            arr = nuevo;
        }
        arr[cantidad++] = strdup(contenido);
    }
    fclose(f);

    if (cantidad == 0) {
        log_warning(logger, "Script vacío (o sólo comentarios): %s", path);
        free(arr);
        *out_cantidad = 0;
        return NULL;
    }

    *out_cantidad = cantidad;
    return arr;
}

// =============================================================================
// TRADUCCIÓN DE DIRECCIONES (segmentación pura)
// =============================================================================

uint32_t traducir_dir_logica(t_contexto_ejecucion* proc,
                              uint32_t dir_logica,
                              uint32_t tamanio,
                              int socket_para_segfault)
{
    uint32_t num_seg      = dir_logica / (uint32_t)config_memory->segment_max_size;
    uint32_t desplazamiento = dir_logica % (uint32_t)config_memory->segment_max_size;

    t_segmento* seg = NULL;
    for (int i = 0; i < list_size(proc->segmentos); i++) {
        t_segmento* s = list_get(proc->segmentos, i);
        if (s->id == num_seg) { seg = s; break; }
    }

    if (!seg || desplazamiento + tamanio > seg->limite) {
        log_warning(logger, "SEGMENTATION FAULT - PID %d dir_logica=0x%X tam=%u",
                    proc->pid, dir_logica, tamanio);
        if (socket_para_segfault >= 0)
            enviar_ok(SEGMENTATION_FAULT, socket_para_segfault);
        return (uint32_t)-1;
    }

    /* Un segmento swapeado tiene base (uint32_t)-1: traducir con esa base
     * produciría direcciones físicas basura (y con desplazamiento 0, colisiona
     * con el centinela de error). El scheduler no debe operar sobre memoria de
     * un proceso suspendido; si llega acá es un bug — fallar ruidosamente. */
    if (seg->base == (uint32_t)-1) {
        log_error(logger,
            "PID %d: acceso a memoria de proceso SUSPENDIDO (segmento %u en swap) dir_logica=0x%X tam=%u",
            proc->pid, num_seg, dir_logica, tamanio);
        if (socket_para_segfault >= 0)
            enviar_ok(SEGMENTATION_FAULT, socket_para_segfault);
        return (uint32_t)-1;
    }

    return seg->base + desplazamiento;
}
