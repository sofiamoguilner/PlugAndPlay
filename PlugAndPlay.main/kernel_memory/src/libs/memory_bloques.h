#ifndef MEMORY_BLOQUES_H
#define MEMORY_BLOQUES_H

#include <stdint.h>

// Todas las funciones de este módulo asumen que el caller tiene
// tomado el lock global de memoria (mutex_memoria).

void     bloques_init(void);
void     bloques_agregar(uint32_t base, uint32_t tamanio);

// Retorna base asignada, o (uint32_t)-1 si no hay bloque que alcance.
// Usa BEST_FIT o WORST_FIT según config.
uint32_t bloques_asignar(uint32_t tamanio);

// Inserta el bloque y fusiona contiguos.
void     bloques_liberar(uint32_t base, uint32_t tamanio);

uint32_t bloques_calcular_espacio_libre(void);

// Resetea la lista a un único bloque libre [base, base+tamanio).
// Usado por compactación.
void     bloques_reset_a_unico(uint32_t base, uint32_t tamanio);

#endif
