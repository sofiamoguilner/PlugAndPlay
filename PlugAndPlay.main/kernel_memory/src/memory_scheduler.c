#include "memory_scheduler.h"
#include "memory_procesos.h"
#include "memory_config.h"
#include "memory_servidor.h"
#include "memory_conexiones.h"
#include "memory_bloques.h"
#include "memory_swap.h"
#include "memory_io.h"
#include "memory_compactacion.h"
#include <utils/serializacion.h>
#include <utils/mensajes.h>
#include <commons/log.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

extern t_log* logger;
extern t_config_memory* config_memory;

// recv exacto con validación: retorna false si el peer cerró la conexión o
// envió menos bytes de los esperados (en vez de seguir con basura en los campos).
static bool recv_campo(int socket, void* dest, size_t n)
{
    ssize_t r = recv(socket, dest, n, MSG_WAITALL);
    if (r != (ssize_t)n) {
        log_error(logger, "recv incompleto del scheduler (esperaba %zu bytes, obtuvo %zd)", n, r);
        return false;
    }
    return true;
}

// Notifica al Scheduler que se liberó memoria (eliminación de segmento o
// finalización de proceso) para que re-evalúe procesos suspendidos.
// El scheduler la demultiplexea inline en memory_protocol.c::recibir_respuesta_de_memoria.
static void notificar_memoria_liberada(void)
{
    int sched_sock = conexiones_get_scheduler_socket();
    if (sched_sock >= 0) enviar_memoria_liberada(sched_sock);
}

// =============================================================================
// CONEXIÓN: HANDSHAKE Y REGISTRO
// =============================================================================

void registrar_scheduler(int client_socket)
{
    recibir_ok(client_socket);
    log_info(logger, "## Kernel Scheduler Conectado - FD del socket: %d", client_socket);

    conexiones_set_scheduler_socket(client_socket);

    enviar_ok(HANDSHAKE_SCHEDULER_A_MEMORY, client_socket);
    spawn_detached(memory_receptor_scheduler_thread, NULL, "scheduler");
}

// =============================================================================
// THREAD RECEPTOR: DESPACHA PEDIDOS DEL SCHEDULER
// =============================================================================

void* memory_receptor_scheduler_thread(void* arg)
{
    (void)arg;

    while (1) {
        int socket_scheduler = conexiones_get_scheduler_socket();

        if (socket_scheduler == -1) {
            usleep(100000);
            continue;
        }

        op_code codigo = recibir_codigo_operacion(socket_scheduler);
        if ((int)codigo == -1) {
            log_warning(logger, "Desconexion del Kernel Scheduler");
            conexiones_set_scheduler_socket(-1);
            close(socket_scheduler);
            break;
        }

        switch (codigo) {
            case CREAR_PROCESO:
                manejar_crear_proceso(socket_scheduler);
                break;
            case ACTUALIZAR_CONTEXTO:
                manejar_actualizar_contexto_scheduler(socket_scheduler);
                break;
            case OBTENER_ESPACIO_LIBRE:
                manejar_obtener_espacio_libre(socket_scheduler);
                break;
            case LECTURA_MEMORIA:
                manejar_lectura_memoria(socket_scheduler);
                break;
            case ESCRITURA_MEMORIA:
                manejar_escritura_memoria(socket_scheduler);
                break;
            case SUSPENDER_PROCESO:
                manejar_suspender_proceso(socket_scheduler);
                break;
            case DES_SUSPENDER_PROCESO:
                manejar_des_suspender_proceso(socket_scheduler);
                break;
            case SOLICITAR_SEGMENTO:
                manejar_solicitar_segmento(socket_scheduler);
                break;
            case ELIMINAR_SEGMENTO:
                manejar_eliminar_segmento(socket_scheduler);
                break;
            case FINALIZAR_PROCESO:
                manejar_finalizar_proceso(socket_scheduler);
                break;
            default:
                log_warning(logger, "Opcode desconocido del scheduler: %d", codigo);
                break;
        }
    }

    log_debug(logger, "Thread receptor del scheduler finalizado");
    return NULL;
}

// =============================================================================
// HANDLERS DE PEDIDOS SCHEDULER
// =============================================================================

// Operacion: Crear Proceso
void manejar_crear_proceso(int socket)
{
    int   pid  = 0;
    char* path = NULL;
    recibir_crear_proceso(socket, &pid, &path);
    log_info(logger, "## PID: %d - Proceso Creado", pid);

    char script_path[512];
    const char* path_resuelto = path;
    if (path != NULL && path[0] != '/' && config_memory != NULL && config_memory->scripts_basepath != NULL) {
        snprintf(script_path, sizeof(script_path), "%s/%s", config_memory->scripts_basepath, path);
        path_resuelto = script_path;
    }

    t_contexto_ejecucion* proc = malloc(sizeof(t_contexto_ejecucion));
    proc->pid       = pid;
    proc->registros = (t_registros){ 0 };
    proc->segmentos = list_create();

    proc->instrucciones = cargar_script(path_resuelto, &proc->cantidad_instrucciones);
    if (!proc->instrucciones || proc->cantidad_instrucciones == 0) {
        log_error(logger, "PID %d: NO SE PUDO CARGAR SCRIPT (cantidad_instrucciones = %d) desde '%s'",
                  pid, proc->cantidad_instrucciones, path_resuelto);
    } else {
        log_debug(logger, "PID %d: cargadas %d instrucciones desde '%s'",
                 pid, proc->cantidad_instrucciones, path_resuelto);
    }

    free(path);

    pthread_mutex_lock(&mutex_memoria);
    procesos_agregar(proc);
    pthread_mutex_unlock(&mutex_memoria);

    enviar_ok(CREAR_PROCESO, socket);
}

void manejar_actualizar_contexto_scheduler(int socket)
{
    t_contexto* ctx = recibir_contexto_actualizar(socket);
    log_debug(logger, "Scheduler actualiza contexto PID=%d", ctx->pid);

    pthread_mutex_lock(&mutex_memoria);
    t_contexto_ejecucion* proc = procesos_buscar(ctx->pid);
    if (proc != NULL) {
        actualizar_proceso_desde_contexto(proc, ctx);
    } else {
        log_error(logger, "Scheduler: proceso PID=%d no encontrado al actualizar", ctx->pid);
    }
    pthread_mutex_unlock(&mutex_memoria);

    enviar_ok(ACTUALIZAR_CONTEXTO, socket);
    list_destroy_and_destroy_elements(ctx->tabla_segmentos, free);
    free(ctx);
}

void manejar_obtener_espacio_libre(int socket)
{
    validar_buffer(socket);
    pthread_mutex_lock(&mutex_memoria);
    uint32_t espacio = bloques_calcular_espacio_libre();
    pthread_mutex_unlock(&mutex_memoria);
    log_debug(logger, "## Consulta espacio libre -> %u bytes", espacio);
    t_paquete* p = crear_paquete(OBTENER_ESPACIO_LIBRE);
    agregar_a_paquete(p, &espacio, sizeof(espacio));
    enviar_paquete(p, socket);
    eliminar_paquete(p);
}

// Operacion: Lectura de Datos
void manejar_lectura_memoria(int socket)
{
    while (compactacion_en_progreso()) usleep(5000);
    validar_buffer(socket);
    int      pid     = 0;
    uint32_t dir     = 0;
    uint32_t tamanio = 0;
    if (!recv_campo(socket, &pid,     sizeof(pid)))     return;
    if (!recv_campo(socket, &dir,     sizeof(dir)))     return;
    if (!recv_campo(socket, &tamanio, sizeof(tamanio))) return;
    pthread_mutex_lock(&mutex_memoria);
    t_contexto_ejecucion* proc = procesos_buscar(pid);
    if (!proc) {
        pthread_mutex_unlock(&mutex_memoria);
        log_error(logger, "LECTURA_MEMORIA: PID %d no encontrado", pid);
        enviar_ok(SEGMENTATION_FAULT, socket);
        return;
    }
    uint32_t dir_fisica = traducir_dir_logica(proc, dir, tamanio, socket);
    pthread_mutex_unlock(&mutex_memoria);

    if (dir_fisica == (uint32_t)-1) return;  // traducir ya envió SEGMENTATION_FAULT

    log_info(logger, "## PID: %d - Lectura - Dir. Física: 0x%X - Tamaño: %u",
             pid, dir_fisica, tamanio);

    void* datos = memory_io_leer(pid, dir_fisica, tamanio);
    if (!datos) return;  // error ya logueado y BSOD enviado si corresponde

    t_paquete* p = crear_paquete(LECTURA_MEMORIA);
    agregar_a_paquete(p, datos, tamanio);
    enviar_paquete(p, socket);
    eliminar_paquete(p);
    free(datos);
}

// Operacion: Escritura de Datos
void manejar_escritura_memoria(int socket)
{
    while (compactacion_en_progreso()) usleep(5000);
    validar_buffer(socket);
    int      pid     = 0;
    uint32_t dir     = 0;
    uint32_t tamanio = 0;
    if (!recv_campo(socket, &pid,     sizeof(pid)))     return;
    if (!recv_campo(socket, &dir,     sizeof(dir)))     return;
    if (!recv_campo(socket, &tamanio, sizeof(tamanio))) return;
    void* datos = malloc(tamanio);
    if (!datos) {
        log_error(logger, "ESCRITURA_MEMORIA: malloc(%u) falló", tamanio);
        return;
    }
    if (!recv_campo(socket, datos, tamanio)) { free(datos); return; }

    pthread_mutex_lock(&mutex_memoria);
    t_contexto_ejecucion* proc = procesos_buscar(pid);
    if (!proc) {
        pthread_mutex_unlock(&mutex_memoria);
        free(datos);
        log_error(logger, "ESCRITURA_MEMORIA: PID %d no encontrado", pid);
        enviar_ok(SEGMENTATION_FAULT, socket);
        return;
    }
    uint32_t dir_fisica = traducir_dir_logica(proc, dir, tamanio, socket);
    pthread_mutex_unlock(&mutex_memoria);

    if (dir_fisica == (uint32_t)-1) { free(datos); return; }

    log_info(logger, "## PID: %d - Escritura - Dir. Física: 0x%X - Tamaño: %u",
             pid, dir_fisica, tamanio);

    memory_io_escribir(pid, dir_fisica, tamanio, datos);
    free(datos);
    enviar_ok(ESCRITURA_MEMORIA, socket);
}

// =============================================================================
// SUSPENSIÓN DE PROCESO (scheduler → memory → swap)
// =============================================================================

// Para un segmento ya con espacio reservado en swap, lee chunk a chunk desde
// la RAM física y lo escribe en swap. Retorna 0 si ok, -1 si error.
static int volcar_segmento_a_swap(int swap_sock, t_segmento* seg, int pid)
{
    uint32_t bs = swap_block_size();
    uint32_t n_blocks = (seg->limite + bs - 1) / bs;

    for (uint32_t b = 0; b < n_blocks; b++) {
        uint32_t chunk = (b == n_blocks - 1) ? seg->limite - b * bs : bs;

        void* bloque_buf = calloc(1, bs);
        void* chunk_data = memory_io_leer(pid, seg->base + b * bs, chunk);
        if (!chunk_data) {
            free(bloque_buf);
            log_error(logger, "## PID: %d - Error leyendo stick para suspender", pid);
            return -1;
        }
        memcpy(bloque_buf, chunk_data, chunk);
        free(chunk_data);

        int result = swap_escribir_bloque(swap_sock, seg->swap_bloque_inicio + b, bloque_buf);
        free(bloque_buf);

        if (result != 0) {
            log_error(logger, "## PID: %d - Error escribiendo bloque %u en swap",
                      pid, seg->swap_bloque_inicio + b);
            return -1;
        }
    }
    return 0;
}

// Operacion: Suspender Proceso
void manejar_suspender_proceso(int socket)
{
    int pid = recibir_suspender_proceso(socket);
    log_debug(logger, "## PID: %d - Solicitud de suspension", pid);

    pthread_mutex_lock(&mutex_memoria);
    t_contexto_ejecucion *proc = procesos_buscar(pid);
    if (!proc) {
        log_warning(logger, "## PID: %d - Proceso no encontrado para suspender", pid);
        pthread_mutex_unlock(&mutex_memoria);
        enviar_ok(SUSPENDER_PROCESO, socket);
        return;
    }

    int swap_sock = conexiones_get_swap_socket();
    if (swap_sock < 0) {
        log_error(logger, "## PID: %d - No hay swap conectado para suspender", pid);
        pthread_mutex_unlock(&mutex_memoria);
        enviar_ok(SUSPENDER_PROCESO, socket);
        return;
    }

    uint32_t bs = swap_block_size();

    for (int i = 0; i < list_size(proc->segmentos); i++) {
        t_segmento *seg = list_get(proc->segmentos, i);

        uint32_t n_blocks = (seg->limite + bs - 1) / bs;

        uint32_t bloque_inicio = swap_reservar_bloques(n_blocks);
        if (bloque_inicio == (uint32_t)-1) {
            log_error(logger, "## PID: %d - No hay espacio suficiente en SWAP", pid);
            pthread_mutex_unlock(&mutex_memoria);
            enviar_ok(SUSPENDER_PROCESO, socket);
            return;
        }
        seg->swap_bloque_inicio = bloque_inicio;

        uint32_t base_seg = seg->base;
        uint32_t lim_seg  = seg->limite;
        pthread_mutex_unlock(&mutex_memoria);

        if (volcar_segmento_a_swap(swap_sock, seg, pid) != 0) {
            return;
        }

        pthread_mutex_lock(&mutex_memoria);
        bloques_liberar(base_seg, lim_seg);
        seg->base = (uint32_t)-1;
    }

    pthread_mutex_unlock(&mutex_memoria);

    log_debug(logger, "## PID: %d - Proceso suspendido correctamente", pid);
    enviar_ok(SUSPENDER_PROCESO, socket);
}


// =============================================================================
// DES-SUSPENSIÓN DE PROCESO (scheduler → memory ← swap)
// =============================================================================

typedef struct { t_segmento* seg; uint32_t base_anterior; } reserva_t;

// Asume mutex_memoria tomado al entrar. Lo libera antes de retornar (en ambos casos).
// Retorna lista de reservas (caller hace free); NULL si no había espacio (ya se respondió SEGFAULT).
static t_list* reservar_segmentos_con_rollback(int socket, int pid, t_contexto_ejecucion* proc)
{
    t_list* reservas = list_create();

    for (int i = 0; i < list_size(proc->segmentos); i++) {
        t_segmento* seg = list_get(proc->segmentos, i);

        uint32_t nueva_base = bloques_asignar(seg->limite);
        if (nueva_base == (uint32_t)-1) {
            log_error(logger, "## PID: %d - Sin espacio suficiente en RAM para des-suspender", pid);

            for (int k = 0; k < list_size(reservas); k++) {
                reserva_t* r = list_get(reservas, k);
                bloques_liberar(r->seg->base, r->seg->limite);
                r->seg->base = r->base_anterior;
                free(r);
            }
            list_destroy(reservas);
            pthread_mutex_unlock(&mutex_memoria);
            enviar_ok(SEGMENTATION_FAULT, socket);
            return NULL;
        }

        reserva_t* r = malloc(sizeof(reserva_t));
        r->seg          = seg;
        r->base_anterior = seg->base;
        seg->base        = nueva_base;
        list_add(reservas, r);
    }

    pthread_mutex_unlock(&mutex_memoria);
    return reservas;
}

// Lee desde swap y escribe en sticks. Retorna 0 ok, -1 error (caller ya respondió ok).
static int transferir_swap_a_ram(int swap_sock, t_contexto_ejecucion* proc, int pid)
{
    uint32_t bs = swap_block_size();

    for (int i = 0; i < list_size(proc->segmentos); i++) {
        t_segmento* seg = list_get(proc->segmentos, i);
        uint32_t n_blocks = (seg->limite + bs - 1) / bs;

        for (uint32_t b = 0; b < n_blocks; b++) {
            uint32_t chunk = (b == n_blocks - 1) ? seg->limite - b * bs : bs;

            void* data = NULL;
            if (swap_leer_bloque(swap_sock, seg->swap_bloque_inicio + b, &data) != 0) {
                log_error(logger, "## PID: %d - Error leyendo swap", pid);
                return -1;
            }

            memory_io_escribir(pid, seg->base + b * bs, chunk, data);
            free(data);
        }
    }
    return 0;
}

// Operacion: Des-suspender Proceso
void manejar_des_suspender_proceso(int socket)
{
    int pid = recibir_des_suspender_proceso(socket);
    log_debug(logger, "## PID: %d - Solicitud de des-suspension", pid);

    pthread_mutex_lock(&mutex_memoria);
    t_contexto_ejecucion *proc = procesos_buscar(pid);
    if (!proc) {
        log_warning(logger, "## PID: %d - Proceso no encontrado", pid);
        pthread_mutex_unlock(&mutex_memoria);
        enviar_ok(DES_SUSPENDER_PROCESO, socket);
        return;
    }

    int swap_sock = conexiones_get_swap_socket();
    if (swap_sock < 0) {
        log_error(logger, "## PID: %d - No hay swap conectado", pid);
        pthread_mutex_unlock(&mutex_memoria);
        enviar_ok(SEGMENTATION_FAULT, socket);
        return;
    }

    t_list* reservas = reservar_segmentos_con_rollback(socket, pid, proc);
    if (!reservas) return;

    if (transferir_swap_a_ram(swap_sock, proc, pid) != 0) {
        for (int k = 0; k < list_size(reservas); k++) free(list_get(reservas, k));
        list_destroy(reservas);
        enviar_ok(SEGMENTATION_FAULT, socket);
        return;
    }

    for (int k = 0; k < list_size(reservas); k++) free(list_get(reservas, k));
    list_destroy(reservas);

    log_debug(logger, "## PID: %d - Proceso des-suspendido correctamente", pid);
    enviar_ok(DES_SUSPENDER_PROCESO, socket);
}


// =============================================================================
// SOLICITUD DE SEGMENTO + COMPACTACIÓN
// =============================================================================

// Orquesta el flujo de compactación: notifica al scheduler, espera ok, compacta,
// reintenta la asignación y re-busca el proceso (puede haber cambiado durante
// la liberación temporal del mutex). Asume mutex_memoria tomado al entrar.
// Al retornar true: mutex sigue tomado, *out_base y *out_proc están listos.
// Al retornar false: mutex liberado, ya se respondió SEGMENTATION_FAULT.
static bool compactar_y_reasignar(int socket, int pid, uint32_t tamanio,
                                  uint32_t* out_base, t_contexto_ejecucion** out_proc)
{
    compactacion_marcar_inicio();
    pthread_mutex_unlock(&mutex_memoria);

    int sched_sock = conexiones_get_scheduler_socket();
    enviar_ok(INICIAR_COMPACTACION, sched_sock);
    // El scheduler responde con enviar_ok(INICIAR_COMPACTACION) (paquete completo
    // con su op_code), así que hay que drenar el op_code ANTES de recibir_ok().
    // Sin esto, recibir_ok()->validar_buffer() lee el op_code como parte del size
    // y aborta ("Buffer inválido"). Mismo idiom que memory_io.c.
    recibir_codigo_operacion(sched_sock);
    recibir_ok(sched_sock);

    pthread_mutex_lock(&mutex_memoria);
    compactar_memoria();
    pthread_mutex_unlock(&mutex_memoria);

    compactacion_marcar_fin();
    enviar_ok(FINALIZAR_COMPACTACION, sched_sock);

    pthread_mutex_lock(&mutex_memoria);
    *out_base = bloques_asignar(tamanio);
    if (*out_base == (uint32_t)-1) {
        pthread_mutex_unlock(&mutex_memoria);
        log_error(logger, "## PID: %d - Sin espacio tras compactacion", pid);
        enviar_ok(SEGMENTATION_FAULT, socket);
        return false;
    }

    *out_proc = procesos_buscar(pid);
    if (!*out_proc) {
        pthread_mutex_unlock(&mutex_memoria);
        log_error(logger, "SOLICITAR_SEGMENTO: PID %d desapareció tras compactacion", pid);
        enviar_ok(SEGMENTATION_FAULT, socket);
        return false;
    }
    return true;
}

// Operacion: Crear Segmento
void manejar_solicitar_segmento(int socket)
{
    validar_buffer(socket);
    int      pid          = 0;
    uint32_t id_segmento  = 0;
    uint32_t tamanio      = 0;
    if (!recv_campo(socket, &pid,         sizeof(pid)))         return;
    if (!recv_campo(socket, &id_segmento, sizeof(id_segmento))) return;
    if (!recv_campo(socket, &tamanio,     sizeof(tamanio)))     return;
    log_debug(logger, "## PID: %d - Solicitar Segmento - ID: %u - Tamaño: %u",
             pid, id_segmento, tamanio);

    if (tamanio > (uint32_t)config_memory->segment_max_size) {
        log_warning(logger,
            "## PID: %d - Pedido de segmento %u excede SEGMENT_MAX_SIZE (%u > %d)",
            pid, id_segmento, tamanio, config_memory->segment_max_size);
        enviar_ok(SEGMENTATION_FAULT, socket);
        return;
    }

    pthread_mutex_lock(&mutex_memoria);
    t_contexto_ejecucion* proc = procesos_buscar(pid);
    if (!proc) {
        pthread_mutex_unlock(&mutex_memoria);
        log_error(logger, "SOLICITAR_SEGMENTO: PID %d no encontrado", pid);
        enviar_ok(SEGMENTATION_FAULT, socket);
        return;
    }

    uint32_t base = bloques_asignar(tamanio);
    if (base == (uint32_t)-1) {
        uint32_t espacio_libre = bloques_calcular_espacio_libre();

        if (espacio_libre < tamanio) {
            pthread_mutex_unlock(&mutex_memoria);
            log_warning(logger, "## PID: %d - Sin espacio para segmento ID=%u size=%u",
                        pid, id_segmento, tamanio);
            enviar_ok(SEGMENTATION_FAULT, socket);
            return;
        }

        log_debug(logger, "## PID: %d - Sin espacio contiguo, iniciando compactacion (libre=%u, pedido=%u)",
                 pid, espacio_libre, tamanio);

        if (!compactar_y_reasignar(socket, pid, tamanio, &base, &proc)) return;
    }

    t_segmento* seg = malloc(sizeof(t_segmento));
    seg->id                 = id_segmento;
    seg->base               = base;
    seg->limite             = tamanio;
    seg->swap_bloque_inicio = 0;
    list_add(proc->segmentos, seg);
    pthread_mutex_unlock(&mutex_memoria);

    log_info(logger, "## PID: %d - Segmento Creado %u - Tamaño: %u",
             pid, id_segmento, tamanio);

    t_paquete* p = crear_paquete(SOLICITAR_SEGMENTO);
    agregar_a_paquete(p, &id_segmento, sizeof(id_segmento));
    agregar_a_paquete(p, &base,        sizeof(base));
    agregar_a_paquete(p, &tamanio,     sizeof(tamanio));
    enviar_paquete(p, socket);
    eliminar_paquete(p);
}

// Operacion: Eliminar Segmento
void manejar_eliminar_segmento(int socket)
{
    validar_buffer(socket);
    int      pid         = 0;
    uint32_t id_segmento = 0;
    if (!recv_campo(socket, &pid,         sizeof(pid)))         return;
    if (!recv_campo(socket, &id_segmento, sizeof(id_segmento))) return;
    log_debug(logger, "## PID: %d - Eliminar Segmento - ID: %u", pid, id_segmento);

    bool libero = false;
    pthread_mutex_lock(&mutex_memoria);
    t_contexto_ejecucion* proc = procesos_buscar(pid);
    if (!proc) {
        pthread_mutex_unlock(&mutex_memoria);
        enviar_ok(ELIMINAR_SEGMENTO, socket);
        return;
    }

    for (int i = 0; i < list_size(proc->segmentos); i++) {
        t_segmento* seg = list_get(proc->segmentos, i);
        if (seg->id == id_segmento) {
            bloques_liberar(seg->base, seg->limite);
            free(list_remove(proc->segmentos, i));
            libero = true;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_memoria);

    enviar_ok(ELIMINAR_SEGMENTO, socket);

    if (libero) notificar_memoria_liberada();
}

// Operacion: Finalizar Proceso
void manejar_finalizar_proceso(int socket)
{
    validar_buffer(socket);
    int pid = 0;
    if (!recv_campo(socket, &pid, sizeof(pid))) return;
    log_debug(logger, "## PID: %d - Proceso Finalizado", pid);

    bool libero = false;
    pthread_mutex_lock(&mutex_memoria);
    int n = procesos_cantidad();
    for (int i = 0; i < n; i++) {
        t_contexto_ejecucion* proc = procesos_obtener_en(i);
        if (proc->pid != pid) continue;

        for (int j = 0; j < list_size(proc->segmentos); j++) {
            t_segmento* seg = list_get(proc->segmentos, j);
            if (seg->base != (uint32_t)-1) {
                bloques_liberar(seg->base, seg->limite);
                libero = true;
            }
            free(seg);
        }
        list_destroy(proc->segmentos);

        if (proc->instrucciones) {
            for (int j = 0; j < proc->cantidad_instrucciones; j++)
                free(proc->instrucciones[j]);
            free(proc->instrucciones);
        }

        procesos_quitar_en(i);
        free(proc);
        break;
    }
    pthread_mutex_unlock(&mutex_memoria);

    enviar_ok(FINALIZAR_PROCESO, socket);

    if (libero) notificar_memoria_liberada();
}
