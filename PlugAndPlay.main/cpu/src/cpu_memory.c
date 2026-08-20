#include "cpu_memory.h"
#include "cpu_config.h"
#include <utils/sockets.h>
#include <utils/mensajes.h>
#include <utils/serializacion.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <commons/log.h>

extern t_log* logger;

int memory_socket = -1;

static void spawn_detached(void* (*fn)(void*), void* arg, const char* nombre);
static int  esperar_conexion(const char* ip, const char* puerto, const char* modulo);
static void manejar_nueva_memoria(void);
static void manejar_instruccion(void);
static void manejar_contexto(void);
static void manejar_ack(op_code codigo);


void* cpu_memory_listener_thread(void* arg)
{
    (void)arg;

    while (1) {
        op_code codigo = recibir_codigo_operacion(memory_socket);

        if ((int)codigo == -1) {
            log_warning(logger, "Desconexion de Kernel Memory o error en recv");
            break;
        }

        switch (codigo) {
            case NUEVA_MEMORIA_DISPONIBLE: 
                    manejar_nueva_memoria();  
                break;
            case OBTENER_INSTRUCCION:      
                    manejar_instruccion();    
                break;
            case OBTENER_CONTEXTO:         
                    manejar_contexto();        
                break;
            case ACTUALIZAR_CONTEXTO:
                    manejar_ack(codigo);
                break;
            default:
                log_warning(logger, "Opcode desconocido recibido desde Kernel Memory: %d", codigo);
                break;
        }
    }

    return NULL;
}



bool conectar_a_memory(char* ip, char* puerto, int cpu_id)
{
    memory_socket = esperar_conexion(ip, puerto, "Kernel Memory");
    enviar_handshake_cpu(memory_socket, cpu_id);

    cpu_connections.cpu_id_global = cpu_id;
    log_debug(logger, "## CPU %d conectada a Kernel Memory", cpu_id);

    // Sincrónico: memoria nos manda su SEGMENT_MAX_SIZE para que la MMU coincida.
    // Se lee acá, antes de arrancar el listener, porque va primero en el stream.
    op_code cod = recibir_codigo_operacion(memory_socket);
    if (cod == CONFIG_MMU_CPU) {
        uint32_t sms = recibir_config_mmu_cpu(memory_socket);
        config_cpu->segment_max_size = sms;
        log_debug(logger, "## CPU %d - SEGMENT_MAX_SIZE recibido de memoria: %u", cpu_id, sms);
    } else {
        log_warning(logger, "Se esperaba CONFIG_MMU_CPU tras el handshake, llegó opcode %d", cod);
    }

    pthread_mutex_init(&cpu_connections.lock, NULL);
    pthread_mutex_init(&cpu_connections.respuesta_lock, NULL);
    sem_init(&cpu_connections.respuesta_lista, 0, 0);

    spawn_detached(cpu_memory_listener_thread, NULL, "cpu_memory_listener");
    return true;
}




// ---------------------------------------------- RESTO DE FUNCIONES AUXILIARES -------------------------------------------------------------------



// =============================================================================
// COMUNICACIÓN CON KERNEL MEMORY
// =============================================================================

bool solicitar_contexto_a_memory(int pid, t_contexto** contexto)
{
    enviar_solicitar_contexto(memory_socket, pid);

    sem_wait(&cpu_connections.respuesta_lista);

    pthread_mutex_lock(&cpu_connections.respuesta_lock);
    op_code     ack = cpu_connections.respuesta_pendiente.codigo;
    t_contexto* ctx = cpu_connections.contexto_pendiente;
    cpu_connections.contexto_pendiente = NULL;
    pthread_mutex_unlock(&cpu_connections.respuesta_lock);

    if (ack != OBTENER_CONTEXTO || ctx == NULL) {
        log_error(logger, "Respuesta inesperada de Memory al pedir contexto PID=%d: opcode=%d", pid, ack);
        if (ctx) {
            list_destroy_and_destroy_elements(ctx->tabla_segmentos, free);
            free(ctx);
        }
        return false;
    }

    ctx->pid = pid;
    *contexto = ctx;
    log_debug(logger, "## Contexto PID=%d recibido (%d segmentos)",
             pid, list_size(ctx->tabla_segmentos));
    return true;
}

bool actualizar_contexto_en_memory(t_contexto* contexto)
{
    if (!contexto) {
        log_error(logger, "actualizar_contexto_en_memory: contexto es NULL");
        return false;
    }

    log_debug(logger, "Actualizando contexto PID=%d en Kernel Memory", contexto->pid);

    enviar_actualizar_contexto(memory_socket, contexto);

    sem_wait(&cpu_connections.respuesta_lista);

    pthread_mutex_lock(&cpu_connections.respuesta_lock);
    op_code ack = cpu_connections.respuesta_pendiente.codigo;
    pthread_mutex_unlock(&cpu_connections.respuesta_lock);

    if (ack != ACTUALIZAR_CONTEXTO) {
        log_error(logger, "ACK inesperado al actualizar contexto: %d", ack);
        return false;
    }
    return true;
}

// =============================================================================
// LISTENER DE KERNEL MEMORY — handlers por opcode
// =============================================================================

static void manejar_nueva_memoria(void)
{
    uint32_t tamanio_ms = 0;
    uint32_t base_ms    = 0;
    char*    ip_ms      = NULL;
    char*    puerto_ms  = NULL;
    recibir_nueva_memoria_disponible(memory_socket, &tamanio_ms, &base_ms, &ip_ms, &puerto_ms);

    log_debug(logger, "CPU: nueva Memory Stick disponible [%s:%s] (%u bytes, base %u) - conectando...",
             ip_ms, puerto_ms, tamanio_ms, base_ms);
    conectar_a_memory_stick(ip_ms, puerto_ms, base_ms, tamanio_ms);
    free(ip_ms);
    free(puerto_ms);
}

static void manejar_instruccion(void)
{
    char* instr = recibir_instruccion(memory_socket);
    pthread_mutex_lock(&cpu_connections.respuesta_lock);
    cpu_connections.instruccion_pendiente          = instr;
    cpu_connections.respuesta_pendiente.codigo     = OBTENER_INSTRUCCION;
    pthread_mutex_unlock(&cpu_connections.respuesta_lock);
    sem_post(&cpu_connections.respuesta_lista);
}

static void manejar_contexto(void)
{
    t_contexto* ctx = recibir_contexto(memory_socket);
    pthread_mutex_lock(&cpu_connections.respuesta_lock);
    cpu_connections.contexto_pendiente             = ctx;
    cpu_connections.respuesta_pendiente.codigo     = OBTENER_CONTEXTO;
    pthread_mutex_unlock(&cpu_connections.respuesta_lock);
    sem_post(&cpu_connections.respuesta_lista);
}

static void manejar_ack(op_code codigo)
{
    recibir_ok(memory_socket);
    pthread_mutex_lock(&cpu_connections.respuesta_lock);
    cpu_connections.respuesta_pendiente.codigo = codigo;
    pthread_mutex_unlock(&cpu_connections.respuesta_lock);
    sem_post(&cpu_connections.respuesta_lista);
}

// =============================================================================
// ACCESO A MEMORIA (API SÍNCRONA PARA cpu_ciclo.c — directo a Memory Stick)
// =============================================================================

// Lee `tam` bytes desde el offset local `dir_local` del stick `stick_idx`.
// El caller debe liberar *out con free().
bool cpu_stick_leer(int stick_idx, int pid, uint32_t dir_local, uint32_t tam, void **out)
{
    int stick = cpu_get_memory_stick_socket(stick_idx);
    if (stick == -1) {
        log_error(logger, "cpu_stick_leer: stick %d no disponible", stick_idx);
        return false;
    }
    enviar_lectura_fisica(stick, pid, dir_local, tam);

    op_code cod = recibir_codigo_operacion(stick);
    if (cod != LECTURA_MEMORIA) {
        log_error(logger, "cpu_stick_leer: respuesta inesperada (opcode=%d)", cod);
        return false;
    }
    *out = recibir_datos_memoria(stick, tam);
    return *out != NULL;
}

// Escribe `tam` bytes en el offset local `dir_local` del stick `stick_idx`.
bool cpu_stick_escribir(int stick_idx, int pid, uint32_t dir_local, void *datos, uint32_t tam)
{
    int stick = cpu_get_memory_stick_socket(stick_idx);
    if (stick == -1) {
        log_error(logger, "cpu_stick_escribir: stick %d no disponible", stick_idx);
        return false;
    }
    enviar_escritura_fisica(stick, pid, dir_local, datos, tam);

    op_code cod = recibir_codigo_operacion(stick);
    if (cod != ESCRITURA_MEMORIA) {
        log_error(logger, "cpu_stick_escribir: ACK inesperado (opcode=%d)", cod);
        return false;
    }
    recibir_ok(stick);
    return true;
}

// =============================================================================
// CONEXIÓN A MEMORY STICK
// =============================================================================

bool conectar_a_memory_stick(const char* ip, const char* puerto, uint32_t base, uint32_t size)
{
    log_debug(logger, "Conectando a Memory Stick en %s:%s...", ip, puerto);

    int sock = crear_conexion((char*)ip, (char*)puerto);
    if (sock == -1) {
        log_error(logger, "No se pudo conectar a Memory Stick en %s:%s", ip, puerto);
        return false;
    }

    enviar_handshake_cpu(sock, cpu_connections.cpu_id_global);

    pthread_mutex_lock(&cpu_connections.lock);
    if (cpu_connections.memory_stick_count < MAX_MEMORY_STICKS) {
        int idx = cpu_connections.memory_stick_count++;
        cpu_connections.memory_sticks[idx] = (t_cpu_stick){ .socket = sock, .base = base, .size = size };
        log_debug(logger, "## CPU %d Conectada a Memory Stick [%s:%s] (slot %d, base %u, %u bytes)",
                 cpu_connections.cpu_id_global, ip, puerto, idx, base, size);
    } else {
        log_warning(logger, "Limite de Memory Sticks alcanzado, ignorando %s:%s", ip, puerto);
        close(sock);
        pthread_mutex_unlock(&cpu_connections.lock);
        return false;
    }
    pthread_mutex_unlock(&cpu_connections.lock);

    return true;
}

// =============================================================================
// GETTERS DE MEMORY STICKS
// =============================================================================

int cpu_get_memory_stick_count(void)
{
    pthread_mutex_lock(&cpu_connections.lock);
    int count = cpu_connections.memory_stick_count;
    pthread_mutex_unlock(&cpu_connections.lock);
    return count;
}

int cpu_get_memory_stick_socket(int index)
{
    pthread_mutex_lock(&cpu_connections.lock);
    int sock = (index >= 0 && index < cpu_connections.memory_stick_count)
               ? cpu_connections.memory_sticks[index].socket
               : -1;
    pthread_mutex_unlock(&cpu_connections.lock);
    return sock;
}

uint32_t cpu_get_memory_stick_base(int index)
{
    pthread_mutex_lock(&cpu_connections.lock);
    uint32_t base = (index >= 0 && index < cpu_connections.memory_stick_count)
                    ? cpu_connections.memory_sticks[index].base
                    : 0;
    pthread_mutex_unlock(&cpu_connections.lock);
    return base;
}

uint32_t cpu_get_memory_stick_size(int index)
{
    pthread_mutex_lock(&cpu_connections.lock);
    uint32_t size = (index >= 0 && index < cpu_connections.memory_stick_count)
                    ? cpu_connections.memory_sticks[index].size
                    : 0;
    pthread_mutex_unlock(&cpu_connections.lock);
    return size;
}

// Localiza el stick cuyo rango [base, base+size) contiene la dirección física
// global. Devuelve el índice o -1 si ninguna lo contiene.
int cpu_buscar_stick_por_dir(uint32_t dir_global)
{
    pthread_mutex_lock(&cpu_connections.lock);
    int encontrado = -1;
    for (int i = 0; i < cpu_connections.memory_stick_count; i++) {
        uint32_t base = cpu_connections.memory_sticks[i].base;
        uint32_t size = cpu_connections.memory_sticks[i].size;
        if (dir_global >= base && dir_global < base + size) {
            encontrado = i;
            break;
        }
    }
    pthread_mutex_unlock(&cpu_connections.lock);
    return encontrado;
}

// =============================================================================
// HELPERS INTERNOS
// =============================================================================

static void spawn_detached(void* (*fn)(void*), void* arg, const char* nombre)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, fn, arg) != 0)
        log_error(logger, "Error creando thread: %s", nombre);
    else
        pthread_detach(thread);
}

#define RETARDO_REINTENTO_MS 2000
#define LOG_CADA_N_INTENTOS  10

static int esperar_conexion(const char* ip, const char* puerto, const char* modulo)
{
    int intentos = 0;
    int sock     = -1;

    log_debug(logger, "Conectando a %s en %s:%s...", modulo, ip, puerto);

    while ((sock = crear_conexion((char*)ip, (char*)puerto)) == -1) {
        intentos++;
        if (intentos % LOG_CADA_N_INTENTOS == 0) {
            log_warning(logger,
                        "%s no disponible en %s:%s — intento %d, reintentando en %d ms...",
                        modulo, ip, puerto, intentos, RETARDO_REINTENTO_MS);
        }
        usleep(RETARDO_REINTENTO_MS * 1000);
    }

    log_debug(logger, "Conexion a %s establecida (intento %d)", modulo, intentos + 1);
    return sock;
}

// ACCESO FÍSICO DIRECTO
// para la MMU, reciben socket del stick específico

bool leer_de_memoria_fisica(int stick_socket, int pid,
                             uint32_t dir_fisica, uint32_t tamanio, void** dato_out)
{
    if (stick_socket == -1) {
        log_error(logger, "leer_de_memoria_fisica: socket inválido");
        return false;
    }
    enviar_lectura_fisica(stick_socket, pid, dir_fisica, tamanio);

    op_code cod = recibir_codigo_operacion(stick_socket);
    if (cod != LECTURA_MEMORIA) {
        log_error(logger, "leer_de_memoria_fisica: respuesta inesperada (opcode=%d)", cod);
        return false;
    }
    *dato_out = recibir_datos_memoria(stick_socket, tamanio);
    return *dato_out != NULL;
}

bool escribir_en_memoria_fisica(int stick_socket, int pid, uint32_t dir_fisica,
                                 void* dato, uint32_t tamanio)
{
    if (stick_socket == -1) {
        log_error(logger, "escribir_en_memoria_fisica: socket inválido");
        return false;
    }
    enviar_escritura_fisica(stick_socket, pid, dir_fisica, dato, tamanio);

    op_code cod = recibir_codigo_operacion(stick_socket);
    if (cod != ESCRITURA_MEMORIA) {
        log_error(logger, "escribir_en_memoria_fisica: ACK inesperado (opcode=%d)", cod);
        return false;
    }
    recibir_ok(stick_socket);
    return true;
}
