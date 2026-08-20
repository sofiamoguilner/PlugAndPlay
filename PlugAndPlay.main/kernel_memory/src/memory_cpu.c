#include "memory_cpu.h"
#include "memory_procesos.h"
#include "memory_config.h"
#include "memory_servidor.h"
#include "memory_conexiones.h"
#include "memory_bloques.h"
#include <utils/serializacion.h>
#include <utils/mensajes.h>
#include <utils/sockets.h>
#include <commons/log.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

extern t_log* logger;
extern t_config_memory* config_memory;

// recv exacto con validación: false si el peer cerró o envió de menos.
static bool recv_campo(int socket, void* dest, size_t n)
{
    ssize_t r = recv(socket, dest, n, MSG_WAITALL);
    if (r != (ssize_t)n) {
        log_error(logger, "recv incompleto de CPU (esperaba %zu bytes, obtuvo %zd)", n, r);
        return false;
    }
    return true;
}

static void* memory_stick_monitor_thread(void* arg)
{
    t_memory_stick_thread_arg* stick_arg = (t_memory_stick_thread_arg*)arg;
    int socket      = stick_arg->socket;
    int stick_index = stick_arg->stick_index;
    free(stick_arg);

    // recv con MSG_PEEK detecta cierre del socket sin consumir datos
    char buf;
    int  ret = recv(socket, &buf, 1, MSG_PEEK);
    (void)ret;

    char* ip = NULL;
    char* puerto = NULL;
    conexiones_stick_invalidate_por_indice(stick_index, &ip, &puerto);
    log_error(logger, "## Memory Stick [%s:%s] desconectada - memoria corrupta",
              ip ? ip : "?", puerto ? puerto : "?");

    close(socket);

    int scheduler_socket = conexiones_get_scheduler_socket();
    if (scheduler_socket != -1)
        enviar_memoria_corrupta(scheduler_socket);

    return NULL;
}

// =============================================================================
// CONEXIÓN: HANDSHAKE Y REGISTRO
// =============================================================================

void registrar_cpu(int client_socket)
{
    int cpu_id = recibir_handshake_cpu(client_socket);
    log_info(logger, "## CPU %d Conectada", cpu_id);

    int idx = conexiones_agregar_cpu(client_socket, cpu_id);
    if (idx < 0) {
        log_warning(logger, "Limite de CPUs alcanzado, rechazando CPU %d", cpu_id);
        close(client_socket);
        return;
    }

    // Sincrónico tras el handshake: la CPU espera el SEGMENT_MAX_SIZE para su MMU
    // antes de notificarle los sticks. Debe ir primero en el stream.
    enviar_config_mmu_cpu(client_socket, (uint32_t)config_memory->segment_max_size);

    notificar_sticks_a_cpu(client_socket, cpu_id);

    t_cpu_thread_arg* cpu_arg = malloc(sizeof(t_cpu_thread_arg));
    *cpu_arg = (t_cpu_thread_arg){ client_socket, cpu_id };

    spawn_detached(memory_receptor_cpu_thread, cpu_arg, "cpu");
}

// =============================================================================
// THREAD RECEPTOR: DESPACHA PEDIDOS DE CPU
// =============================================================================

void* memory_receptor_cpu_thread(void* arg)
{
    t_cpu_thread_arg* cpu_arg = (t_cpu_thread_arg*)arg;
    int socket = cpu_arg->socket;
    int cpu_id = cpu_arg->cpu_id;
    free(cpu_arg);

    while (1) {
        op_code codigo = recibir_codigo_operacion(socket);
        if ((int)codigo == -1) {
            log_warning(logger, "CPU %d desconectada", cpu_id);
            conexiones_invalidar_cpu_por_id(cpu_id);
            close(socket);
            break;
        }

        switch (codigo) {
            case OBTENER_INSTRUCCION:
                manejar_obtener_instruccion(socket, cpu_id);
                break;
            case OBTENER_CONTEXTO:
                manejar_obtener_contexto_cpu(socket, cpu_id);
                break;
            case ACTUALIZAR_CONTEXTO:
                manejar_actualizar_contexto_cpu(socket, cpu_id);
                break;
            default:
                log_warning(logger, "CPU %d: opcode desconocido %d", cpu_id, codigo);
                break;
        }
    }

    log_debug(logger, "Thread receptor de CPU %d finalizado", cpu_id);
    return NULL;
}

// =============================================================================
// HANDLERS DE PEDIDOS CPU
// =============================================================================

void manejar_obtener_instruccion(int socket, int cpu_id)
{
    (void)cpu_id;

    validar_buffer(socket);
    int      pid = 0;
    uint32_t pc  = 0;
    if (!recv_campo(socket, &pid, sizeof(pid))) return;
    if (!recv_campo(socket, &pc,  sizeof(pc)))  return;
    pthread_mutex_lock(&mutex_memoria);
    t_contexto_ejecucion* proc = procesos_buscar(pid);
    char* linea = "";
    if (proc && pc < (uint32_t)proc->cantidad_instrucciones) {
        linea = proc->instrucciones[pc];
        log_info(logger, "## PID: %d - Obtener instrucción: %u - Instrucción: %s", pid, pc, linea);
    } else {
        log_error(logger, "PID %d: pc=%u fuera de rango (%d instrucciones)",
                  pid, pc, proc ? proc->cantidad_instrucciones : 0);
    }
    pthread_mutex_unlock(&mutex_memoria);

    // Retardo configurable antes de contestar la instrucción (enunciado).
    if (config_memory->instruction_delay > 0) {
        usleep((useconds_t)config_memory->instruction_delay * 1000);
    }

    t_paquete* p = crear_paquete(OBTENER_INSTRUCCION);
    agregar_cadena(p, linea);
    enviar_paquete(p, socket);
    eliminar_paquete(p);
}

void manejar_obtener_contexto_cpu(int socket, int cpu_id)
{
    validar_buffer(socket);
    int pid = 0;
    if (!recv_campo(socket, &pid, sizeof(pid))) return;
    log_debug(logger, "CPU %d solicita contexto PID=%d", cpu_id, pid);

    pthread_mutex_lock(&mutex_memoria);
    t_contexto_ejecucion* proc = procesos_buscar(pid);
    if (proc == NULL) {
        // El Scheduler debe haber creado el Proceso en Memoria antes de mandarlo
        // a ejecutar. Si no existe, es un bug real: lo dejamos visible en vez de
        // fabricar un contexto dummy que enmascare el problema.
        pthread_mutex_unlock(&mutex_memoria);
        log_error(logger, "CPU %d: PID=%d inexistente al solicitar contexto (¿el Scheduler no lo creó?)",
                  cpu_id, pid);
        return;
    }

    enviar_contexto_proceso(socket, proc);
    pthread_mutex_unlock(&mutex_memoria);
}

void manejar_actualizar_contexto_cpu(int socket, int cpu_id)
{
    t_contexto* ctx = recibir_contexto_actualizar(socket);
    log_debug(logger, "CPU %d actualiza contexto PID=%d", cpu_id, ctx->pid);

    pthread_mutex_lock(&mutex_memoria);
    t_contexto_ejecucion* proc = procesos_buscar(ctx->pid);
    if (proc != NULL) {
        actualizar_proceso_desde_contexto(proc, ctx);
    } else {
        log_error(logger, "CPU %d: proceso PID=%d no encontrado al actualizar", cpu_id, ctx->pid);
    }
    pthread_mutex_unlock(&mutex_memoria);

    enviar_ok(ACTUALIZAR_CONTEXTO, socket);
    list_destroy_and_destroy_elements(ctx->tabla_segmentos, free);
    free(ctx);
}

// =============================================================================
// CONEXIÓN DE MEMORY STICKS
// =============================================================================

void registrar_memory_stick(int client_socket)
{
    uint32_t tamanio = 0;
    char*    ip      = NULL;
    char*    puerto  = NULL;

    recibir_handshake_memory_stick(client_socket, &tamanio, &ip, &puerto);
    log_info(logger, "## Memory Stick de %u bytes Conectada", tamanio);

    uint32_t base_fisica = 0;
    int stick_index = conexiones_agregar_stick(client_socket, tamanio, ip, puerto, &base_fisica);
    if (stick_index < 0) {
        log_warning(logger, "Limite de Memory Sticks alcanzado");
        free(ip); free(puerto); close(client_socket);
        return;
    }

    notificar_cpus_de_nuevo_stick(tamanio, base_fisica, ip, puerto);

    // Agregar bloque libre (requiere mutex_memoria)
    pthread_mutex_lock(&mutex_memoria);
    bloques_agregar(base_fisica, tamanio);
    pthread_mutex_unlock(&mutex_memoria);

    // Conectar outbound al Memory Stick (kernel_memory actúa como CPU con id -999)
    int out_sock = crear_conexion(ip, puerto);
    if (out_sock == -1) {
        log_error(logger, "No se pudo conectar outbound a Memory Stick [%s:%s]", ip, puerto);
    } else {
        enviar_handshake_cpu(out_sock, -999);
        conexiones_stick_set_outbound_socket(stick_index, out_sock);
        log_debug(logger, "   -> Conexion outbound a Memory Stick [%s:%s] establecida (fd=%d)",
                 ip, puerto, out_sock);
    }

    // Notificar al scheduler que hay nueva memoria disponible
    int scheduler_socket = conexiones_get_scheduler_socket();
    if (scheduler_socket != -1)
        enviar_nueva_memoria_disponible(scheduler_socket, tamanio, base_fisica, ip, puerto);

    t_memory_stick_thread_arg* stick_arg = malloc(sizeof(t_memory_stick_thread_arg));
    *stick_arg = (t_memory_stick_thread_arg){ client_socket, stick_index };
    spawn_detached(memory_stick_monitor_thread, stick_arg, "stick_monitor");
}

void notificar_sticks_a_cpu(int cpu_socket, int cpu_id)
{
    int n = conexiones_cantidad_sticks();
    for (int i = 0; i < n; i++) {
        uint32_t tam      = conexiones_stick_tamanio(i);
        uint32_t base     = conexiones_stick_base_fisica(i);
        const char* ip    = conexiones_stick_ip(i);
        const char* puerto = conexiones_stick_puerto(i);
        enviar_nueva_memoria_disponible(cpu_socket, tam, base, (char*)ip, (char*)puerto);
        log_debug(logger, "   -> Notificada CPU %d de Memory Stick preexistente [%s:%s] (%u bytes, base %u)",
                 cpu_id, ip, puerto, tam, base);
    }
}

void notificar_cpus_de_nuevo_stick(uint32_t tamanio, uint32_t base_fisica, char* ip, char* puerto)
{
    int n = conexiones_cantidad_cpus();
    for (int i = 0; i < n; i++) {
        int cpu_socket = conexiones_cpu_socket_en(i);
        if (cpu_socket == -1) continue;
        int cpu_id = conexiones_cpu_id_en(i);
        enviar_nueva_memoria_disponible(cpu_socket, tamanio, base_fisica, ip, puerto);
        log_debug(logger, "   -> Notificada CPU %d de nueva Memory Stick [%s:%s] (%u bytes, base %u)",
                 cpu_id, ip, puerto, tamanio, base_fisica);
    }
}
