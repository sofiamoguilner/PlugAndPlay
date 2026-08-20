#ifndef UTILS_KERNEL_SCHEDULER_H
#define UTILS_KERNEL_SCHEDULER_H

#include <stdint.h>
#include <pthread.h>
#include <utils/utils.h>
#include "scheduler_conexiones.h"

// =============================================================================
// HELPERS GENERALES (threading, logging)
// =============================================================================

void spawn_detached(void* (*fn)(void*), void* arg, const char* nombre);

// =============================================================================
// FUNCIONES PARA DESALOJO Y COMPACTACIÓN
// =============================================================================

void desalojar_todos_procesos(void);
void manejar_compactacion_inicio(void);
void manejar_compactacion_fin(void);

// =============================================================================
// =============================================================================

void encolar_en_ready_por_prioridad(t_pcb* proceso);
void encolar_en_ready_y_notificar(t_pcb* proceso);
t_pcb* desencolar_proceso_ready(void);
void reencolar_proceso_ready_final(t_pcb* proceso, int prioridad);
void reencolar_proceso_ready_inicio(t_pcb* proceso, int prioridad);

// =============================================================================
// =============================================================================

int  enviar_proceso_a_cpu(t_pcb* proceso);
void iniciar_quantum_timer(int cpu_id, int pid, uint32_t quantum_ms);
bool conectar_a_kernel_memory(char* ip, char* puerto);
void notificar_fin_a_memoria(int pid);

// Timeout del socket con Memoria: se desactiva durante la compactación (que puede
// tardar más que la red de seguridad MEMORY_OP_TIMEOUT_S) y se restaura al terminar.
void suspender_timeout_memoria(int socket);
void restaurar_timeout_memoria(int socket);

// =============================================================================
// HANDLERS DE CONEXIÓN (handshakes, receptores)
// =============================================================================

void* scheduler_handshake_handler(void* socket_ptr);
void* scheduler_cpu_receptor_thread(void* arg);
void* scheduler_listener_thread(void* arg);

// =============================================================================
// STRUCTS Y VARIABLES GLOBALES
// =============================================================================

typedef struct {
    int socket;
    int cpu_id;
    bool disponible;  // true = libre, false = ejecutando un proceso
    t_pcb* proceso_actual;
    volatile bool timer_activo;
    int timer_pid;
} t_cpu;

typedef struct {
    int socket;
    char* nombre;  // "STDIN", "STDOUT", "SLEEP", etc.
} t_io;

typedef struct {
    int server_socket;
    int memory_socket;

    t_cpu cpus[MAX_CPUS];
    int cpu_count;

    t_io ios[MAX_IOS];
    int io_count;

    pthread_mutex_t lock;
} t_conexiones_activas;

extern t_conexiones_activas scheduler_connections;

#endif // UTILS_KERNEL_SCHEDULER_H
