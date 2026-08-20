#include "cpu_ciclo.h"
#include "cpu_scheduler.h"
#include "cpu_memory.h"
#include "cpu_mmu.h"
#include "cpu_mmu.h"
#include <commons/log.h>
#include <commons/string.h>
#include <utils/mensajes.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

extern t_log* logger;

static void manejar_error_fetch(int pid);
static void manejar_interrupcion(t_contexto* contexto, t_instruccion* instruccion);
static bool manejar_resultado_execute(t_contexto* contexto, t_instruccion* instruccion, t_resultado_execute resultado);
static void limpiar_estado_cpu(t_contexto* contexto);

// CICLO PRINCIPAL

void ejecutar_ciclo_instruccion(int pid)
{
    log_debug(logger, "## Iniciando ciclo de instrucción para PID %d", pid);

    t_contexto* contexto = NULL;
    if (!solicitar_contexto_a_memory(pid, &contexto)) {
        log_error(logger, "No se pudo obtener contexto para PID %d", pid);
        return;
    }

    cpu_connections.pid_en_ejecucion = pid;

    while (1) {
        t_instruccion* instruccion = fetch_instruccion(contexto);
        if (instruccion == NULL) {
            manejar_error_fetch(pid);
            break;
        }

        t_resultado_execute resultado = execute_instruccion(contexto, instruccion);

        if (resultado != CONTINUAR) {
            manejar_resultado_execute(contexto, instruccion, resultado);
            goto fin_ciclo;
        }

        if (check_interrupt()) {
            manejar_interrupcion(contexto, instruccion);
            break;
        }

        liberar_instruccion(instruccion);
    }

fin_ciclo:
    limpiar_estado_cpu(contexto);
}

// =============================================================================
// FETCH
// =============================================================================

t_instruccion* fetch_instruccion(t_contexto* contexto)
{
    int      pid = contexto->pid;
    uint32_t pc  = contexto->registros.pc;

    log_info(logger, "## PID: %d - FETCH - Program Counter: %u", pid, pc);

    enviar_obtener_instruccion(memory_socket, pid, pc);

    sem_wait(&cpu_connections.respuesta_lista);
    pthread_mutex_lock(&cpu_connections.respuesta_lock);
    char* linea_instruccion = cpu_connections.instruccion_pendiente;
    cpu_connections.instruccion_pendiente = NULL;
    pthread_mutex_unlock(&cpu_connections.respuesta_lock);

    if (linea_instruccion == NULL) {
        log_error(logger, "Error recibiendo instruccion desde Memory");
        return NULL;
    }

    t_instruccion* instruccion = malloc(sizeof(t_instruccion));
    if (instruccion == NULL) {
        log_error(logger, "Error asignando memoria para instruccion");
        free(linea_instruccion);
        return NULL;
    }

    decode_instruccion(linea_instruccion, instruccion);
    free(linea_instruccion);

    log_debug(logger, "## PID: %d - DECODE: %s (%d params)",
             pid, instruccion->nombre, instruccion->cantidad_parametros);

    return instruccion;
}
// =============================================================================
// DECODE
// =============================================================================

static bool es_instruccion_cpu_memoria(const char* nombre)
{
    return strcmp(nombre, "MOV_IN")   == 0 ||
           strcmp(nombre, "MOV_OUT")  == 0 ||
           strcmp(nombre, "COPY_MEM") == 0;
}

static bool es_syscall(const char* nombre)
{
    return strcmp(nombre, "SLEEP")        == 0 ||
           strcmp(nombre, "STDIN")        == 0 ||
           strcmp(nombre, "STDOUT")       == 0 ||
           strcmp(nombre, "INIT_PROC")    == 0 ||
           strcmp(nombre, "EXIT")         == 0 ||
           strcmp(nombre, "MEM_ALLOC")    == 0 ||
           strcmp(nombre, "MEM_FREE")     == 0 ||
           strcmp(nombre, "MUTEX_CREATE") == 0 ||
           strcmp(nombre, "MUTEX_LOCK")   == 0 ||
           strcmp(nombre, "MUTEX_UNLOCK") == 0;
}

static bool es_syscall_memoria(const char* nombre)
{
    return strcmp(nombre, "STDIN")  == 0 ||
           strcmp(nombre, "STDOUT") == 0;
}

void decode_instruccion(char* linea_instruccion, t_instruccion* instruccion)
{
    char*  copia = strdup(linea_instruccion);
    char*  fin   = copia + strlen(copia) - 1;
    while (fin > copia && isspace((unsigned char)*fin)) *fin-- = '\0';
    char** tokens = string_split(copia, " ");

    int cantidad = 0;
    while (tokens[cantidad] != NULL) cantidad++;

    instruccion->nombre              = strdup(tokens[0]);
    instruccion->cantidad_parametros = cantidad - 1;

    if (cantidad > 1) {
        instruccion->parametros = malloc(sizeof(char*) * (cantidad - 1));
        for (int i = 1; i < cantidad; i++)
            instruccion->parametros[i - 1] = strdup(tokens[i]);
    } else {
        instruccion->parametros = NULL;
    }

    instruccion->requiere_mmu       = es_instruccion_cpu_memoria(instruccion->nombre);
    instruccion->es_syscall         = es_syscall(instruccion->nombre);
    instruccion->es_syscall_memoria = es_syscall_memoria(instruccion->nombre);

    string_array_destroy(tokens);
    free(copia);
}

// REGISTROS

uint32_t obtener_valor_registro(t_registros* reg, const char* nombre_reg)
{
    if (!nombre_reg) return 0;
    if (strcmp(nombre_reg, "AX")  == 0) return reg->ax;
    if (strcmp(nombre_reg, "BX")  == 0) return reg->bx;
    if (strcmp(nombre_reg, "CX")  == 0) return reg->cx;
    if (strcmp(nombre_reg, "DX")  == 0) return reg->dx;
    if (strcmp(nombre_reg, "EAX") == 0) return reg->eax;
    if (strcmp(nombre_reg, "EBX") == 0) return reg->ebx;
    if (strcmp(nombre_reg, "ECX") == 0) return reg->ecx;
    if (strcmp(nombre_reg, "EDX") == 0) return reg->edx;
    if (strcmp(nombre_reg, "SI")  == 0) return reg->si;
    if (strcmp(nombre_reg, "DI")  == 0) return reg->di;
    log_warning(logger, "Registro desconocido: %s", nombre_reg);
    return 0;
}

void asignar_valor_registro(t_registros* reg, const char* nombre_reg, uint32_t valor)
{
    if (!nombre_reg) return;
    if (strcmp(nombre_reg, "AX")  == 0) { reg->ax  = (uint8_t)valor; return; }
    if (strcmp(nombre_reg, "BX")  == 0) { reg->bx  = (uint8_t)valor; return; }
    if (strcmp(nombre_reg, "CX")  == 0) { reg->cx  = (uint8_t)valor; return; }
    if (strcmp(nombre_reg, "DX")  == 0) { reg->dx  = (uint8_t)valor; return; }
    if (strcmp(nombre_reg, "EAX") == 0) { reg->eax = valor;          return; }
    if (strcmp(nombre_reg, "EBX") == 0) { reg->ebx = valor;          return; }
    if (strcmp(nombre_reg, "ECX") == 0) { reg->ecx = valor;          return; }
    if (strcmp(nombre_reg, "EDX") == 0) { reg->edx = valor;          return; }
    if (strcmp(nombre_reg, "SI")  == 0) { reg->si  = valor;          return; }
    if (strcmp(nombre_reg, "DI")  == 0) { reg->di  = valor;          return; }
    log_warning(logger, "Registro desconocido al asignar: %s", nombre_reg);
}

// MMU

// EXECUTE — tamaño de registros

static uint32_t sizeof_registro(const char* nombre)
{
    if (strcmp(nombre, "AX") == 0 || strcmp(nombre, "BX") == 0 ||
        strcmp(nombre, "CX") == 0 || strcmp(nombre, "DX") == 0) return 1;
    return 4;
}

// Traduce la dir lógica (vía MMU) y lee `tam` bytes, consolidando las porciones
// que puedan caer en distintos Memory Sticks. Retorna:
//   CONTINUAR  → ok; *buffer_out apunta a `tam` bytes (el caller libera con free).
//   SEG_FAULT  → la traducción excedió el segmento / dirección inválida.
//   ERROR      → falló la E/S con algún Memory Stick.
static t_resultado_execute mmu_leer(t_contexto* ctx, uint32_t dir_logica, uint32_t tam,
                                    void** buffer_out, uint32_t* dir_fisica_out)
{
    t_traduccion_fisica trad[MAX_MEMORY_STICKS + 1];
    int      cant       = 0;
    uint32_t dir_fisica = 0;
    if (!mmu_traducir_dir_logica(ctx, dir_logica, tam, trad, &cant, &dir_fisica)) {
        log_error(logger, "PID %d - SEGMENTATION FAULT leyendo dir_logica=0x%X tam=%u",
                  ctx->pid, dir_logica, tam);
        return SEG_FAULT;
    }

    uint8_t* buffer = malloc(tam);
    if (!buffer) return ERROR;

    uint32_t off = 0;
    for (int i = 0; i < cant; i++) {
        void* parte = NULL;
        if (!cpu_stick_leer(trad[i].stick_index, ctx->pid, trad[i].dir_fisica, trad[i].bytes_en_stick, &parte)) {
            free(buffer);
            return ERROR;
        }
        memcpy(buffer + off, parte, trad[i].bytes_en_stick);
        free(parte);
        off += trad[i].bytes_en_stick;
    }

    *buffer_out = buffer;
    if (dir_fisica_out) *dir_fisica_out = dir_fisica;
    return CONTINUAR;
}

// Traduce la dir lógica (vía MMU) y escribe `tam` bytes desde `datos`,
// repartiendo entre los Memory Sticks involucrados. Mismos códigos de retorno.
static t_resultado_execute mmu_escribir(t_contexto* ctx, uint32_t dir_logica, void* datos,
                                        uint32_t tam, uint32_t* dir_fisica_out)
{
    t_traduccion_fisica trad[MAX_MEMORY_STICKS + 1];
    int      cant       = 0;
    uint32_t dir_fisica = 0;
    if (!mmu_traducir_dir_logica(ctx, dir_logica, tam, trad, &cant, &dir_fisica)) {
        log_error(logger, "PID %d - SEGMENTATION FAULT escribiendo dir_logica=0x%X tam=%u",
                  ctx->pid, dir_logica, tam);
        return SEG_FAULT;
    }

    uint32_t off = 0;
    for (int i = 0; i < cant; i++) {
        if (!cpu_stick_escribir(trad[i].stick_index, ctx->pid, trad[i].dir_fisica,
                                (uint8_t*)datos + off, trad[i].bytes_en_stick))
            return ERROR;
        off += trad[i].bytes_en_stick;
    }

    if (dir_fisica_out) *dir_fisica_out = dir_fisica;
    return CONTINUAR;
}

static t_resultado_execute ejecutar_mov_in(t_instruccion* instr, t_contexto* ctx)
{
    if (instr->cantidad_parametros < 1) {
        log_error(logger, "MOV_IN: parámetros insuficientes");
        return ERROR;
    }

    char*    reg     = instr->parametros[0];
    uint32_t tamanio = sizeof_registro(reg);

    void*    dato  = NULL;
    uint32_t dir_f = 0;
    t_resultado_execute r = mmu_leer(ctx, ctx->registros.si, tamanio, &dato, &dir_f);
    if (r != CONTINUAR) return r;

    uint32_t valor = 0;
    memcpy(&valor, dato, tamanio);
    free(dato);

    asignar_valor_registro(&ctx->registros, reg, valor);

    log_info(logger, "PID: %d - Acción: LEER - Dirección Física: %u - Valor: %u",
             ctx->pid, dir_f, valor);
    return CONTINUAR;
}

// MOV_OUT reg  →  memoria[DI] ← reg
static t_resultado_execute ejecutar_mov_out(t_instruccion* instr, t_contexto* ctx)
{
    if (instr->cantidad_parametros < 1) {
        log_error(logger, "MOV_OUT: parámetros insuficientes");
        return ERROR;
    }

    char*    reg     = instr->parametros[0];
    uint32_t tamanio = sizeof_registro(reg);
    uint32_t valor   = obtener_valor_registro(&ctx->registros, reg);

    uint32_t dir_f = 0;
    t_resultado_execute r = mmu_escribir(ctx, ctx->registros.di, &valor, tamanio, &dir_f);
    if (r != CONTINUAR) return r;

    log_info(logger, "PID: %d - Acción: ESCRIBIR - Dirección Física: %u - Valor: %u",
             ctx->pid, dir_f, valor);
    return CONTINUAR;
}

// COPY_MEM reg  →  memoria[DI] ← memoria[SI]  (reg = cantidad de bytes)
static t_resultado_execute ejecutar_copy_mem(t_instruccion* instr, t_contexto* ctx)
{
    if (instr->cantidad_parametros < 1) {
        log_error(logger, "COPY_MEM: parámetros insuficientes");
        return ERROR;
    }

    char*    reg     = instr->parametros[0];
    uint32_t tamanio = obtener_valor_registro(&ctx->registros, reg);

    void*    dato   = NULL;
    uint32_t orig_f = 0;
    t_resultado_execute r = mmu_leer(ctx, ctx->registros.si, tamanio, &dato, &orig_f);
    if (r != CONTINUAR) return r;
    log_info(logger, "PID: %d - Acción: LEER - Dirección Física: %u - Tamaño: %u",
             ctx->pid, orig_f, tamanio);

    uint32_t dest_f = 0;
    r = mmu_escribir(ctx, ctx->registros.di, dato, tamanio, &dest_f);
    free(dato);
    if (r != CONTINUAR) return r;
    log_info(logger, "PID: %d - Acción: ESCRIBIR - Dirección Física: %u - Tamaño: %u",
             ctx->pid, dest_f, tamanio);
    return CONTINUAR;
}

static t_resultado_execute ejecutar_instruccion_mmu(t_instruccion* instr, t_contexto* ctx)
{
    if (strcmp(instr->nombre, "MOV_IN")   == 0) return ejecutar_mov_in(instr, ctx);
    if (strcmp(instr->nombre, "MOV_OUT")  == 0) return ejecutar_mov_out(instr, ctx);
    if (strcmp(instr->nombre, "COPY_MEM") == 0) return ejecutar_copy_mem(instr, ctx);
    log_error(logger, "Instruccion MMU desconocida: %s", instr->nombre);
    return ERROR;
}

// EXECUTE — resto de instrucciones

static t_resultado_execute ejecutar_noop(void) { return CONTINUAR; }

static t_resultado_execute ejecutar_set(t_contexto* ctx, t_instruccion* instr, bool* pc_modificado)
{
    if (instr->cantidad_parametros < 2) { log_error(logger, "SET: params insuficientes"); return ERROR; }
    char*    reg   = instr->parametros[0];
    uint32_t valor = (uint32_t)atoi(instr->parametros[1]);
    if (strcmp(reg, "PC") == 0) { ctx->registros.pc = valor; *pc_modificado = true; }
    else asignar_valor_registro(&ctx->registros, reg, valor);
    return CONTINUAR;
}

static t_resultado_execute ejecutar_sum(t_contexto* ctx, t_instruccion* instr)
{
    if (instr->cantidad_parametros < 2) { log_error(logger, "SUM: params insuficientes"); return ERROR; }
    char* dest = instr->parametros[0];
    char* orig = instr->parametros[1];
    uint32_t v1 = obtener_valor_registro(&ctx->registros, dest);
    uint32_t v2 = isdigit((unsigned char)orig[0]) ? (uint32_t)atoi(orig)
                                                   : obtener_valor_registro(&ctx->registros, orig);
    asignar_valor_registro(&ctx->registros, dest, v1 + v2);
    return CONTINUAR;
}

static t_resultado_execute ejecutar_sub(t_contexto* ctx, t_instruccion* instr)
{
    if (instr->cantidad_parametros < 2) { log_error(logger, "SUB: params insuficientes"); return ERROR; }
    char* dest = instr->parametros[0];
    char* orig = instr->parametros[1];
    uint32_t v1 = obtener_valor_registro(&ctx->registros, dest);
    uint32_t v2 = isdigit((unsigned char)orig[0]) ? (uint32_t)atoi(orig)
                                                   : obtener_valor_registro(&ctx->registros, orig);
    asignar_valor_registro(&ctx->registros, dest, v1 - v2);
    return CONTINUAR;
}

static t_resultado_execute ejecutar_jnz(t_contexto* ctx, t_instruccion* instr, bool* pc_modificado)
{
    if (instr->cantidad_parametros < 2) { log_error(logger, "JNZ: params insuficientes"); return ERROR; }
    char*    reg      = instr->parametros[0];
    uint32_t nueva_pc = (uint32_t)atoi(instr->parametros[1]);
    if (obtener_valor_registro(&ctx->registros, reg) != 0) {
        ctx->registros.pc = nueva_pc;
        *pc_modificado = true;
    }
    return CONTINUAR;
}

// RESOLVER PARÁMETROS SYSCALL IO (STDOUT/STDIN: registros → valores numéricos)


static char** resolver_parametros_syscall_io(const char* nombre_syscall, char** params,
                                              int cant_params, t_registros* registros)
{
    if ((strcmp(nombre_syscall, "STDOUT") != 0 && strcmp(nombre_syscall, "STDIN") != 0)
        || cant_params < 2) {
        return params;
    }

    char** resueltos = malloc(sizeof(char*) * cant_params);
    if (!resueltos) return params;

    for (int i = 0; i < cant_params; i++) {
        if (i < 2) {
            uint32_t valor = obtener_valor_registro(registros, params[i]);
            int len = snprintf(NULL, 0, "%u", valor) + 1;
            resueltos[i] = malloc(len);
            if (resueltos[i]) snprintf(resueltos[i], len, "%u", valor);
        } else {
            resueltos[i] = strdup(params[i]);
        }
    }
    return resueltos;
}

static void liberar_parametros_resueltos(char** params, int cant)
{
    if (!params) return;
    for (int i = 0; i < cant; i++) free(params[i]);
    free(params);
}

// EXECUTE — dispatcher principal

t_resultado_execute execute_instruccion(t_contexto* contexto, t_instruccion* instruccion)
{
    if (!instruccion || !instruccion->nombre) return ERROR;

    bool pc_modificado = false;
    t_resultado_execute resultado;

    if      (strcmp(instruccion->nombre, "NOOP") == 0) resultado = ejecutar_noop();
    else if (strcmp(instruccion->nombre, "SET")  == 0) resultado = ejecutar_set(contexto, instruccion, &pc_modificado);
    else if (strcmp(instruccion->nombre, "SUM")  == 0) resultado = ejecutar_sum(contexto, instruccion);
    else if (strcmp(instruccion->nombre, "SUB")  == 0) resultado = ejecutar_sub(contexto, instruccion);
    else if (strcmp(instruccion->nombre, "JNZ")  == 0) resultado = ejecutar_jnz(contexto, instruccion, &pc_modificado);
    else if (instruccion->requiere_mmu)                resultado = ejecutar_instruccion_mmu(instruccion, contexto);
    else if (strcmp(instruccion->nombre, "EXIT") == 0) resultado = EXIT_PROCESO;
    else if (instruccion->es_syscall)                  resultado = SYSCALL;
    else {
        log_warning(logger, "Instruccion desconocida: %s", instruccion->nombre);
        return ERROR;
    }

    char* params_str = string_new();
    for (int i = 0; i < instruccion->cantidad_parametros; i++) {
        if (i > 0) string_append(&params_str, " ");
        string_append(&params_str, instruccion->parametros[i]);
    }
    log_info(logger, "## PID: %u - Ejecutando: %s - %s",
             contexto->pid, instruccion->nombre, params_str);
    free(params_str);

    if (!pc_modificado) contexto->registros.pc++;

    return resultado;
}

// =============================================================================
// CHECK INTERRUPT
// =============================================================================

bool check_interrupt(void)
{
    pthread_mutex_lock(&cpu_connections.interrupcion_lock);
    bool tiene = cpu_connections.interrupcion_pendiente;
    if (tiene) cpu_connections.interrupcion_pendiente = false;
    pthread_mutex_unlock(&cpu_connections.interrupcion_lock);
    return tiene;
}

// =============================================================================
// MANEJADORES DEL CICLO
// =============================================================================

static void manejar_error_fetch(int pid)
{
    log_error(logger, "## PID: %d - Error en FETCH", pid);
    enviar_devolver_proceso(scheduler_socket, pid, MOTIVO_ERROR, NULL, NULL, 0);
}

static void manejar_interrupcion(t_contexto* contexto, t_instruccion* instruccion)
{
    log_info(logger, "## Interrupción recibida");
    actualizar_contexto_en_memory(contexto);

    // Devolver con el motivo recibido en la INTERRUPCION (fin de quantum o
    // desalojo por compactación); por defecto, fin de quantum.
    pthread_mutex_lock(&cpu_connections.interrupcion_lock);
    int motivo = cpu_connections.motivo_interrupcion;
    pthread_mutex_unlock(&cpu_connections.interrupcion_lock);

    enviar_devolver_proceso(scheduler_socket, contexto->pid, motivo, NULL, NULL, 0);
    liberar_instruccion(instruccion);
}

static bool manejar_resultado_execute(t_contexto* contexto, t_instruccion* instruccion,
                                       t_resultado_execute resultado)
{
    switch (resultado) {
        case CONTINUAR:
            liberar_instruccion(instruccion);
            return true;

        case SYSCALL:
            log_debug(logger, "## PID: %d - Syscall, devolviendo al Scheduler", contexto->pid);
            actualizar_contexto_en_memory(contexto);

            if (instruccion->es_syscall_memoria) {
                char** resueltos = resolver_parametros_syscall_io(
                    instruccion->nombre, instruccion->parametros,
                    instruccion->cantidad_parametros, &contexto->registros);
                enviar_devolver_proceso(scheduler_socket, contexto->pid, MOTIVO_SYSCALL,
                    instruccion->nombre, resueltos, instruccion->cantidad_parametros);
                liberar_parametros_resueltos(resueltos, instruccion->cantidad_parametros);
            } else {
                enviar_devolver_proceso(scheduler_socket, contexto->pid, MOTIVO_SYSCALL,
                    instruccion->nombre, instruccion->parametros,
                    instruccion->cantidad_parametros);
            }
            liberar_instruccion(instruccion);
            return false;

        case EXIT_PROCESO:
            log_debug(logger, "## PID: %d - EXIT, finalizando proceso", contexto->pid);
            actualizar_contexto_en_memory(contexto);
            enviar_devolver_proceso(scheduler_socket, contexto->pid, MOTIVO_EXIT, NULL, NULL, 0);
            liberar_instruccion(instruccion);
            return false;

        case SEG_FAULT:
            log_warning(logger, "## PID: %d - SEG_FAULT, devolviendo al Scheduler para finalizar", contexto->pid);
            actualizar_contexto_en_memory(contexto);
            enviar_devolver_proceso(scheduler_socket, contexto->pid, MOTIVO_SEGFAULT, NULL, NULL, 0);
            liberar_instruccion(instruccion);
            return false;

        case ERROR:
            log_error(logger, "## PID: %d - Error en ejecución", contexto->pid);
            actualizar_contexto_en_memory(contexto);
            enviar_devolver_proceso(scheduler_socket, contexto->pid, MOTIVO_ERROR, NULL, NULL, 0);
            liberar_instruccion(instruccion);
            return false;
    }
    return false;
}

static void limpiar_estado_cpu(t_contexto* contexto)
{
    cpu_connections.pid_en_ejecucion = -1;
    pthread_mutex_lock(&cpu_connections.interrupcion_lock);
    cpu_connections.interrupcion_pendiente = false;
    pthread_mutex_unlock(&cpu_connections.interrupcion_lock);
    log_debug(logger, "## Fin ciclo de instrucción para PID %d (PC=%u)",
             contexto->pid, contexto->registros.pc);
    liberar_contexto(contexto);
}

// =============================================================================
// LIMPIEZA
// =============================================================================

void liberar_instruccion(t_instruccion* instr)
{
    if (instr == NULL) return;
    free(instr->nombre);
    if (instr->parametros != NULL) {
        for (int i = 0; i < instr->cantidad_parametros; i++)
            free(instr->parametros[i]);
        free(instr->parametros);
    }
    free(instr);
}

void liberar_contexto(t_contexto* contexto)
{
    if (contexto == NULL) return;
    if (contexto->tabla_segmentos != NULL)
        list_destroy_and_destroy_elements(contexto->tabla_segmentos, free);
    free(contexto);
}