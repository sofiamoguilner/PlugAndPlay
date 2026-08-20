#ifndef CPU_CICLO_H_
#define CPU_CICLO_H_

#include "cpu_types.h"
#include <stdbool.h>

// ciclo de instrucción completo
void               ejecutar_ciclo_instruccion(int pid);

// pasos del ciclo
t_instruccion*     fetch_instruccion(t_contexto* contexto);
void               decode_instruccion(char* linea_instruccion, t_instruccion* instruccion);
t_resultado_execute execute_instruccion(t_contexto* contexto, t_instruccion* instruccion);
bool               check_interrupt(void);

// registros
uint32_t obtener_valor_registro(t_registros* reg, const char* nombre_reg);
void     asignar_valor_registro(t_registros* reg, const char* nombre_reg, uint32_t valor);

// limpieza
void liberar_instruccion(t_instruccion* instr);
void liberar_contexto(t_contexto* contexto);

#endif // CPU_CICLO_H_
