#ifndef PLANIFICADOR_LARGO_PLAZO_H_
#define PLANIFICADOR_LARGO_PLAZO_H_

#include <commons/collections/queue.h>
#include <semaphore.h>
#include "globalesKernelScheduler.h"


extern t_queue* cola_procesos_new;


extern t_queue* cola_procesos_ready;


void planificadorLargoPlazo(void);

void* _planificador_largo_plazo_thread(void* arg);

void iniciar_planificador_largo_plazo(void);

void manejar_bsod(const char* motivo);

#endif
