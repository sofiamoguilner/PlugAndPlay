#ifndef CPU_TYPES_H_
#define CPU_TYPES_H_

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <commons/collections/list.h>
#include <utils/utils.h>

// =============================================================================
// TIPOS DEL CICLO DE INSTRUCCIÓN
// =============================================================================

typedef enum {
    CONTINUAR,
    SYSCALL,
    EXIT_PROCESO,
    SEG_FAULT,
    ERROR
} t_resultado_execute;

typedef struct {
    char*  nombre;
    int    cantidad_parametros;
    char** parametros;

    bool requiere_mmu;
    bool es_syscall;
    bool es_syscall_memoria;
} t_instruccion;

typedef enum {
    SYSCALL_NONE,
    SYSCALL_IO,
    SYSCALL_EXIT,
    SYSCALL_MEM,
    SYSCALL_MUTEX,
    SYSCALL_INIT_PROC
} t_syscall_tipo;

// =============================================================================
// ESTADO COMPARTIDO DE CONEXIONES
// =============================================================================

typedef struct {
    op_code codigo;
} t_respuesta_memory;

// Metadata de cada Memory Stick conectado. La dirección física es global/plana
// (v1.1): el stick que contiene una dirección D es aquel con base <= D < base+size,
// y el offset local enviado al stick es D - base.
typedef struct {
    int      socket;
    uint32_t base;   // dirección física global donde arranca este stick
    uint32_t size;   // tamaño en bytes
} t_cpu_stick;

typedef struct {
    int scheduler_socket;
    int memory_socket;
    t_cpu_stick memory_sticks[MAX_MEMORY_STICKS];
    int memory_stick_count;
    int cpu_id_global;
    pthread_mutex_t lock;

    // respuestas a requests hacia Kernel Memory
    t_respuesta_memory respuesta_pendiente;
    char*              instruccion_pendiente;
    t_contexto*        contexto_pendiente;
    pthread_mutex_t    respuesta_lock;
    sem_t              respuesta_lista;

    // interrupción del Scheduler
    volatile bool   interrupcion_pendiente;
    volatile int    motivo_interrupcion;  // motivo con que se devuelve el proceso (MOTIVO_QUANTUM / MOTIVO_DESALOJO)
    pthread_mutex_t interrupcion_lock;

    // despacho via listener thread
    int   pid_a_ejecutar;
    sem_t sem_proceso_listo;

    // PID actualmente en ejecución (-1 si ninguno)
    volatile int pid_en_ejecucion;
} t_conexiones_cpu;

extern t_conexiones_cpu cpu_connections;

#endif // CPU_TYPES_H_
