#ifndef GLOBALES_KERNEL_SCHEDULER_H_
#define GLOBALES_KERNEL_SCHEDULER_H_

#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <commons/collections/queue.h>
#include <commons/collections/list.h>
#include <semaphore.h>
#include <pthread.h>

typedef struct t_config_scheduler t_config_scheduler;

/* ====== LÍMITES DE RECURSOS ====== */
/* Fuente única de verdad para el tamaño de las tablas de CPUs e IOs.
   Definidos acá porque este header lo incluyen todos los módulos del scheduler. */
#define MAX_CPUS 1000
#define MAX_IOS 1000

/* ====== ENUMS Y ESTRUCTURAS ====== */

typedef enum {
    NEW,
    READY,
    EXEC,
    BLOCK,
    SUSP_READY,
    SUSP_BLOCK,
    EXIT_STATE
} t_estado;

typedef struct {
    char* nombre;
    int asignado_a_pid;         // PID del proceso poseedor, o -1 si libre
    t_queue* cola_bloqueados;   // Cola de t_pcb* bloqueados esperando este mutex
} t_mutex_sistema;

/* Información mínima de un segmento que el scheduler necesita trackear */
typedef struct {
    uint32_t id;
    uint32_t tamanio;
} t_seg_info;

typedef struct {
    int pid;
    int prioridad;
    t_estado estado;
    uint32_t quantum_restante;
    time_t tiempo_llegada;
    time_t tiempo_inicio_ejecucion;
    int prioridad_dinamica;
    uint32_t tiempo_total_cpu;
    uint32_t conteo_context_switch;
    void* mutex_esperando;      // Puntero a t_mutex_sistema* o NULL

    volatile bool suspendido_en_swap;  // true cuando el volcado a SWAP (suspensión) terminó
    uint32_t block_epoch;       // Se incrementa en cada entrada a BLOCK (bajo mutex_block);
                                // invalida timers de suspensión de episodios BLOCK anteriores

    /* Tracking de memoria para el Planificador de Mediano Plazo */
    uint32_t total_memoria;     // bytes actualmente asignados (suma de segmentos activos)
    t_list*  segmentos_info;    // lista de t_seg_info* (id, tamanio por segmento)

    /* Escritura STDIN diferida: si la IO terminó con el proceso en SUSP_BLOCK,
     * sus segmentos están en SWAP y escribir en ese momento corrompería RAM
     * ajena. El buffer queda acá y el PMP lo escribe después del swap-in. */
    char*    stdin_pendiente;      // buffer a escribir, o NULL si no hay pendiente
    uint32_t stdin_pendiente_dir;  // dirección lógica destino
    uint32_t stdin_pendiente_size; // cantidad de bytes
} t_pcb;


/* ====== COLAS ====== */
extern t_queue* cola_procesos_new;
extern t_queue* cola_procesos_ready;

/* Colas multinivel por prioridad */
#define MAX_PRIORITY_LEVELS 10
extern t_queue* colas_ready_por_prioridad[MAX_PRIORITY_LEVELS];
extern int cantidad_niveles_prioridad;

/* ====== MUTEXES Y SEMÁFOROS ====== */
extern pthread_mutex_t mutex_new;
extern pthread_mutex_t mutex_ready;
extern pthread_mutex_t mutex_colas_prioridad;

extern sem_t sem_new;
extern sem_t sem_ready;
extern sem_t sem_cpu_disponible;

/* ====== RECURSOS ====== */
extern t_list* cpus;
extern pthread_mutex_t mutex_cpus;

/* ====== LISTAS DE MUTEXES Y PROCESOS ====== */
extern t_list* todos_los_procesos;
extern pthread_mutex_t mutex_todos_los_procesos;

extern t_list* lista_mutexes;
extern pthread_mutex_t mutex_lista_mutexes;

/* ====== ESTADO DEL SISTEMA ====== */
extern volatile int sistema_corrupto;
extern volatile int compactacion_en_progreso;
extern pthread_mutex_t mutex_bsod;
extern pthread_mutex_t mutex_memory_socket;


void inicializar_recursos_kernel_scheduler(void);
void inicializar_colas_multinivel(int cantidad_colas, t_config_scheduler* config);

bool string_igual_ignora_mayusculas(const char* izquierda, const char* derecha);
int obtener_algoritmo_global(void);
int obtener_algoritmo_por_prioridad(int prioridad);
const char* algoritmo_a_string(int algoritmo);
bool queue_preemption_habilitada(void);
t_pcb* pop_ready(void);
const char* estado_a_string(t_estado estado);

/* Helpers para PCB y Mutex */
t_pcb* buscar_pcb_por_pid(int pid);
void registrar_pcb(t_pcb* pcb);
void eliminar_pcb(int pid);

t_mutex_sistema* buscar_mutex_por_nombre(const char* nombre);
void registrar_mutex(t_mutex_sistema* mut);
void liberar_mutexes_de_proceso(int pid);

/* Funciones de Herencia de Prioridades */
void propagar_herencia_prioridad(t_pcb* proceso_bloqueado, int prioridad_a_heredar);
void recalcular_prioridad_dinamica(t_pcb* proceso);
void reubicar_proceso_en_ready(t_pcb* pcb, int prioridad_anterior, int prioridad_nueva);

#endif