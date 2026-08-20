#ifndef PLANIFICADOR_CORTO_PLAZO_H_
#define PLANIFICADOR_CORTO_PLAZO_H_

#include "globalesKernelScheduler.h"

typedef enum {
	ALGORITMO_FIFO,
	ALGORITMO_RR,
	ALGORITMO_CMN,
	ALGORITMO_DESCONOCIDO
} t_algoritmo_corto_plazo;

void planificadorCortoPlazo(void);
void* _planificador_corto_plazo_thread(void* arg);
void iniciar_planificador_corto_plazo(void);

#endif
