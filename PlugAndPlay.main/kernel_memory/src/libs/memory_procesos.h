#ifndef MEMORY_PROCESOS_H
#define MEMORY_PROCESOS_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "memory_tipos.h"

// Lock global de memoria compartido entre módulos (procesos, bloques, scheduler, cpu).
// Se inicializa en main.c.
extern pthread_mutex_t mutex_memoria;

// =============================================================================
// GESTIÓN DE PROCESOS
// =============================================================================

void                    procesos_init(void);
t_contexto_ejecucion*   procesos_buscar(int pid);
void                    procesos_agregar(t_contexto_ejecucion* p);
int                     procesos_cantidad(void);
t_contexto_ejecucion*   procesos_obtener_en(int idx);
void                    procesos_quitar_en(int idx);

t_contexto_ejecucion*   buscar_proceso(int pid);  // alias retenido por compatibilidad interna
void                    actualizar_proceso_desde_contexto(t_contexto_ejecucion* proc, t_contexto* ctx);

// =============================================================================
// SERIALIZACIÓN DE CONTEXTO
// =============================================================================

void        enviar_contexto_proceso(int socket, t_contexto_ejecucion* proc);
t_contexto* recibir_contexto_actualizar(int socket);

// =============================================================================
// CARGA DE SCRIPTS
// =============================================================================

char** cargar_script(const char* path, int* out_cantidad);

// =============================================================================
// TRADUCCIÓN DE DIRECCIONES (segmentación pura)
// =============================================================================

// Caller debe tener mutex_memoria tomado.
// Retorna dir_fisica, o (uint32_t)-1 y envía SEGMENTATION_FAULT si falla.
uint32_t traducir_dir_logica(t_contexto_ejecucion* proc,
                              uint32_t dir_logica,
                              uint32_t tamanio,
                              int socket_para_segfault);

#endif
