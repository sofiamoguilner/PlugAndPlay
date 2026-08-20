#ifndef CPU_MMU_H_
#define CPU_MMU_H_

#include <stdint.h>
#include <stdbool.h>
#include "cpu_types.h"

// RESULTADO DE TRADUCCION

typedef struct {
    int      stick_index;    
    uint32_t dir_fisica;     
    uint32_t bytes_en_stick; 
} t_traduccion_fisica;

// API PUBLICA

/**
 * Traduce una dirección lógica a física usando la tabla de segmentos del contexto.
 *
 * @param contexto    contexto del proceso (tiene la tabla de segmentos)
 * @param num_segmento  id del segmento a buscar
 * @param desplazamiento offset dentro del segmento
 * @param tamanio     cantidad de bytes a leer/escribir
 * @param resultado   array donde se escriben las traducciones (puede ser >1 si abarca varios sticks)
 * @param cant_resultado cantidad de traducciones generadas
 * @return true si la traducción fue exitosa, false si hay SEG_FAULT
 */
bool mmu_traducir(t_contexto* contexto,
                  uint32_t num_segmento,
                  uint32_t desplazamiento,
                  uint32_t tamanio,
                  t_traduccion_fisica* resultado,
                  int* cant_resultado,
                  uint32_t* dir_fisica_global_out);

/**
 * Convierte una direc logica "plana" (como la que viene en SI/DI)
 * a nro de segmento + desplazamiento, usando TAM_MAX_SEGMENTO del config.
 */
void mmu_descomponer_dir_logica(uint32_t dir_logica,
                                uint32_t* num_segmento_out,
                                uint32_t* desplazamiento_out);

/**
 * Funcion completa: toma dir_logica + tam, traduce y devuelve
 * las traducciones fisicas listas para enviar al Memory Stick
 * Retorna false si hay SEG_FAULT.
 */
bool mmu_traducir_dir_logica(t_contexto* contexto,
                             uint32_t dir_logica,
                             uint32_t tamanio,
                             t_traduccion_fisica* resultado,
                             int* cant_resultado,
                             uint32_t* dir_fisica_global_out);

#endif 