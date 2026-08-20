#ifndef MEMORY_TIPOS_H
#define MEMORY_TIPOS_H

#include <stdint.h>
#include <commons/collections/list.h>
#include <utils/utils.h>

typedef struct {
    int         pid;
    t_registros registros;
    t_list*     segmentos;
    char**      instrucciones;
    int         cantidad_instrucciones;
} t_contexto_ejecucion;

typedef struct {
    uint32_t base;
    uint32_t tamanio;
} t_bloque_libre;

typedef struct {
    int socket;
    int cpu_id;
} t_cpu_thread_arg;

typedef struct {
    int socket;
    int stick_index;
} t_memory_stick_thread_arg;

#endif
