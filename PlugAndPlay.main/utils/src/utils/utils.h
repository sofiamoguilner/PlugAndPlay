#ifndef UTILS_H_
#define UTILS_H_

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <commons/collections/list.h>

// Limite maximo de Memory Sticks que se gestionan desde Kernel Memory y CPU
#define MAX_MEMORY_STICKS 1000

/**
* @brief Imprime un saludo por consola
* @param quien Módulo desde donde se llama a la función
* @return No devuelve nada
*/
void saludar(char* quien);

// Registros de CPU compartidos entre módulos
typedef struct {
    uint32_t pc;

    uint8_t ax;
    uint8_t bx;
    uint8_t cx;
    uint8_t dx;

    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    uint32_t si;
    uint32_t di;
} t_registros;

// Segmento de memoria
typedef struct {
    uint32_t id;
    uint32_t base;
    uint32_t limite;
    uint32_t swap_bloque_inicio; // primer bloque en swap; valido solo mientras suspendido
} t_segmento;

// Contexto de ejecución de un proceso
typedef struct {
    uint32_t pid;
    t_registros registros;
    t_list* tabla_segmentos; 
} t_contexto;

typedef enum {
    MOTIVO_QUANTUM ,
    MOTIVO_SYSCALL ,
    MOTIVO_EXIT ,
    MOTIVO_ERROR ,
    MOTIVO_SEGFAULT ,
    MOTIVO_DESALOJO ,        // desalojo por compactación: el proceso vuelve al frente de READY
    MOTIVO_DESALOJO_COLA ,   // desalojo por cola más prioritaria: idem, vuelve al frente de READY
} t_motivo_devolucion;


#ifndef PROTOCOLO_H_
#define PROTOCOLO_H_

typedef enum {
    // Handshakes
    HANDSHAKE_SCHEDULER_A_MEMORY,
    HANDSHAKE_CPU,
    HANDSHAKE_IO,
    HANDSHAKE_MEMORY_STICK,
    HANDSHAKE_SWAP,

    // Procesos
    CREAR_PROCESO,
    FINALIZAR_PROCESO,
    SUSPENDER_PROCESO,
    DES_SUSPENDER_PROCESO,
    
    // Segmentación
    SOLICITAR_SEGMENTO,
    ELIMINAR_SEGMENTO,
    INICIAR_COMPACTACION,
    FINALIZAR_COMPACTACION,

    // CPU
    OBTENER_CONTEXTO,
    ACTUALIZAR_CONTEXTO,
    OBTENER_INSTRUCCION,
    INTERRUPCION,
    EJECUTAR_PROCESO,
    CONFIG_MMU_CPU,        // Kernel Memory -> CPU: SEGMENT_MAX_SIZE para la MMU

    // Memoria
    OBTENER_ESPACIO_LIBRE,
    LECTURA_MEMORIA,
    ESCRITURA_MEMORIA,
    LECTURA_BLOQUE_SWAP,
    ESCRITURA_BLOQUE_SWAP,
    RESPUESTA_SWAP,

    // IO
    IO_STDIN,
    IO_STDOUT,
    IO_SLEEP,
    IO_FINALIZADA,

    // Sistema
    MEMORIA_CORRUPTA_BSOD,
    NUEVA_MEMORIA_DISPONIBLE,
    MEMORIA_LIBERADA,
    SEGMENTATION_FAULT,
    DEVOLVER_PROCESO
} op_code;

#endif // PROTOCOLO_H_

#endif // UTILS_H_


