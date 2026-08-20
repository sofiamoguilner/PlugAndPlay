/*
 * Implementación del demux scheduler ↔ memory.
 *
 * Ver memory_protocol.h para la documentación de uso, precondiciones y
 * cómo agregar handlers nuevos.
 */

#include "memory_protocol.h"
#include "globalesKernelScheduler.h"
#include "planificadorLargoPlazo.h"
#include "planificadorMedianoPlazo.h"
#include "utils_kernel_scheduler.h"
#include <commons/log.h>
#include <utils/utils.h>
#include <utils/mensajes.h>
#include <utils/serializacion.h>
#include <stdlib.h>

/* --- HANDLERS DE NOTIFICACIONES ------------------------------------------ *
 * Cada handler corre con `mutex_memory_socket` tomado por el caller original
 * de `recibir_respuesta_de_memoria`. Ver "REGLA DE ORO" en el header.
 * El comentario al lado de cada uno declara el wire format que drena.
 * -------------------------------------------------------------------------- */

// Wire: enviar_ok(MEMORIA_CORRUPTA_BSOD)  →  recibir_ok().
// Acción: termina el proceso vía manejar_bsod (enunciado pág. 10: BSOD).
static void handle_memoria_corrupta_bsod(int socket)
{
    recibir_ok(socket);
    log_error(logger, "¡¡¡ MEMORIA CORRUPTA DETECTADA !!!");
    manejar_bsod("Corrupción de memoria reportada por Kernel Memory");
}

// Wire: enviar_ok(MEMORIA_LIBERADA)  →  recibir_ok().
static void handle_memoria_liberada(int socket)
{
    recibir_ok(socket);
    log_debug(logger, "## Memoria liberada — re-evaluando procesos suspendidos");
    notificar_reevaluar_suspendidos();
}

// Wire: enviar_ok(INICIAR_COMPACTACION)  →  recibir_ok().
// Memory queda bloqueada en recibir_ok() esperando que el scheduler desaloje
// las CPUs. El handler corre síncrono y responde antes de retornar.
static void handle_iniciar_compactacion(int socket)
{
    recibir_ok(socket);
    // La compactación puede tardar más que MEMORY_OP_TIMEOUT_S: desactivamos el
    // timeout del socket para no matar por ERROR al proceso que la disparó.
    suspender_timeout_memoria(socket);
    manejar_compactacion_inicio();
    enviar_ok(INICIAR_COMPACTACION, socket);
}

// Wire: enviar_ok(FINALIZAR_COMPACTACION)  →  recibir_ok().
// Memory NO espera respuesta — solo notifica que terminó de compactar.
static void handle_finalizar_compactacion(int socket)
{
    recibir_ok(socket);
    // Terminó la compactación: rearmamos la red de seguridad para operaciones normales.
    restaurar_timeout_memoria(socket);
    manejar_compactacion_fin();
}

// Wire: NUEVA_MEMORIA_DISPONIBLE con payload
//       [tamanio:u32][base_fisica:u32][ip:cadena][puerto:cadena].
// Memory la pushea cuando se conecta un nuevo Memory Stick.
static void handle_nueva_memoria_disponible(int socket)
{
    uint32_t tamanio = 0;
    uint32_t base_fisica = 0;
    char* ip = NULL;
    char* puerto = NULL;
    recibir_nueva_memoria_disponible(socket, &tamanio, &base_fisica, &ip, &puerto);
    log_debug(logger, "## Nueva memoria disponible: %u bytes en [%s:%s] — re-evaluando suspendidos",
             tamanio, ip ? ip : "?", puerto ? puerto : "?");
    free(ip);
    free(puerto);
    notificar_reevaluar_suspendidos();
}

// Tabla de despacho. Devuelve true si el opcode era una notificación y fue
// drenada + manejada; false si es opcode de respuesta a una request del caller.
static bool despachar_si_es_notificacion(int socket, op_code op)
{
    switch (op) {
        case MEMORIA_CORRUPTA_BSOD:     handle_memoria_corrupta_bsod(socket);     return true;
        case MEMORIA_LIBERADA:          handle_memoria_liberada(socket);          return true;
        case INICIAR_COMPACTACION:      handle_iniciar_compactacion(socket);      return true;
        case FINALIZAR_COMPACTACION:    handle_finalizar_compactacion(socket);    return true;
        case NUEVA_MEMORIA_DISPONIBLE:  handle_nueva_memoria_disponible(socket);  return true;
        default:                                                                  return false;
    }
}

op_code recibir_respuesta_de_memoria(int socket)
{
    while (1) {
        op_code op = recibir_codigo_operacion(socket);
        if ((int)op == -1) return op;                          // socket cerrado/error
        if (!despachar_si_es_notificacion(socket, op)) return op;
        // era notificación: ya se drenó y manejó, seguir leyendo hasta la respuesta real
    }
}
