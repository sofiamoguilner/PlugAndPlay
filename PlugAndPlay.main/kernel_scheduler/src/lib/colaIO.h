#ifndef COLA_IO_H_
#define COLA_IO_H_

#include "globalesKernelScheduler.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>


typedef enum {
    IO_TYPE_SLEEP,
    IO_TYPE_STDIN,
    IO_TYPE_STDOUT
} t_io_type;

// ENTRADA EN LA COLA DE UNA INTERFAZ IO
// Representa un proceso esperando ser atendido por una IO específica
typedef struct {
    t_pcb*      proceso;
    t_io_type   tipo;

    // SLEEP: tiempo en ms
    int         tiempo_ms;

    // STDIN: tamaño a leer + dirección lógica donde escribir en Memory
    int         stdin_size;
    int         stdin_dir_logica;

    // STDOUT: dirección lógica a leer de Memory + tamaño.
    // Los bytes se leen de Memory en el momento de la syscall (proceso aún en
    // RAM) y viajan en stdout_buffer; el worker solo los despacha a la IO.
    int         stdout_dir_logica;
    int         stdout_size;
    char*       stdout_buffer;
} t_io_request;

 
// REPRESENTACIÓN DE UNA INTERFAZ IO CONECTADA (con su cola propia)

typedef struct {
    int             socket;
    char*           nombre;       // nombre que mandó al conectarse ("SLEEP1", "STDIN", etc.)
    t_io_type       tipo;         // tipo inferido del nombre

    t_queue*        cola;         // cola de requests pendientes para esta IO
    pthread_mutex_t mutex_cola;
    sem_t           sem_cola;     // señala que hay trabajo en la cola

    pthread_t       thread_worker; // thread que despacha requests a esta IO
    bool            activa;
} t_interfaz_io;

// TABLA DE INTERFACES IO ACTIVAS
// MAX_IOS viene de globalesKernelScheduler.h

extern t_interfaz_io* tabla_ios[MAX_IOS];
extern int            tabla_ios_count;
extern pthread_mutex_t mutex_tabla_ios;

// Cola de procesos bloqueados por IO (BLOCK)
extern t_queue*        cola_procesos_block;
extern pthread_mutex_t mutex_block;

// API PÚBLICA

// Inicializa la tabla de IOs
void inicializar_tabla_ios(void);

// Registra una nueva interfaz IO que se conectó (llamar desde handshake_handler)
t_interfaz_io* registrar_interfaz_io(int socket, const char* nombre);

// Busca una interfaz IO por nombre (ej: "SLEEP", "STDIN", "STDOUT")
t_interfaz_io* buscar_interfaz_io(const char* nombre);

// Busca la primera interfaz IO activa del tipo dado
t_interfaz_io* buscar_interfaz_io_por_tipo(t_io_type tipo);

// Encola un request de IO para un proceso 
bool encolar_io_request(t_interfaz_io* io, t_io_request* request);

// Crea un t_io_request para SLEEP
t_io_request* crear_io_request_sleep(t_pcb* proceso, int tiempo_ms);

// Crea un t_io_request para STDIN
t_io_request* crear_io_request_stdin(t_pcb* proceso, int size, int dir_logica);

// Crea un t_io_request para STDOUT. Toma ownership de buffer (bytes ya leídos
// de Memory al momento de la syscall).
t_io_request* crear_io_request_stdout(t_pcb* proceso, int dir_logica, int size, char* buffer);

// Escribe el resultado de un STDIN en Kernel Memory (bajo mutex_memory_socket).
// Devuelve false si Memory respondió SEGMENTATION_FAULT o se desconectó.
bool escribir_stdin_en_memoria(int pid, int dir_logica, char* datos, int size);

// Finaliza por SEG_FAULT un PCB que ya fue removido de las colas de
// planificación (lo usa el worker de IO y el PMP con la escritura diferida).
void finalizar_pcb_por_seg_fault(t_pcb* proceso);

// Thread worker de cada IO: saca de la cola y despacha al socket de la IO
void* io_worker_thread(void* arg);

// Mueve un proceso de BLOCK a READY (o SUSP_READY si está suspendido)
void desbloquear_proceso_por_io(int pid);

// Infiere el tipo de IO a partir del nombre ("SLEEP" -> IO_TYPE_SLEEP, etc.)
t_io_type inferir_tipo_io(const char* nombre);

#endif 