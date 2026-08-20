#include "planificadorCortoPlazo.h"

#include "scheduler_config.h"
#include "utils_kernel_scheduler.h"

#include <string.h>
#include <stdint.h>
#include <unistd.h>

static void reencolar_ready_final(t_pcb* proceso);
static void reencolar_ready_inicio(t_pcb* proceso);

static bool hay_cpu_libre(void)
{
	bool libre = false;

	pthread_mutex_lock(&scheduler_connections.lock);
	for (int i = 0; i < scheduler_connections.cpu_count; i++) {
		if (scheduler_connections.cpus[i].disponible && scheduler_connections.cpus[i].socket != -1) {
			libre = true;
			break;
		}
	}
	pthread_mutex_unlock(&scheduler_connections.lock);

	return libre;
}

static t_pcb* obtener_proximo_proceso_ready(t_algoritmo_corto_plazo algoritmo_global)
{
	if (algoritmo_global == ALGORITMO_CMN) {
		if (cantidad_niveles_prioridad <= 0) {
			return NULL;
		}

		return desencolar_proceso_ready();
	}

	return pop_ready();
}

static void reencolar_por_fallo_de_envio(t_pcb* proceso, t_algoritmo_corto_plazo algoritmo_global)
{
	if (proceso == NULL) {
		return;
	}

	if (algoritmo_global == ALGORITMO_CMN) {
		reencolar_proceso_ready_inicio(proceso, proceso->prioridad);
		return;
	}

	if (algoritmo_global == ALGORITMO_RR) {
		reencolar_ready_final(proceso);
		return;
	}

	reencolar_ready_inicio(proceso);
}

static uint32_t quantum_para_algoritmo(t_algoritmo_corto_plazo algoritmo, int prioridad)
{
	if (algoritmo == ALGORITMO_RR) {
		return (uint32_t)config_scheduler->rr_quantum;
	}

	if (algoritmo == ALGORITMO_CMN) {
		t_algoritmo_corto_plazo algoritmo_cola = (t_algoritmo_corto_plazo)obtener_algoritmo_por_prioridad(prioridad);
		if (algoritmo_cola == ALGORITMO_RR) {
			return (uint32_t)config_scheduler->rr_quantum;
		}
	}

	return 0;
}

static bool algoritmo_debe_usar_fifo(t_algoritmo_corto_plazo algoritmo, int prioridad)
{
	if (algoritmo == ALGORITMO_FIFO) {
		return true;
	}

	if (algoritmo == ALGORITMO_CMN) {
		return obtener_algoritmo_por_prioridad(prioridad) == ALGORITMO_FIFO;
	}

	return false;
}

static const char* algoritmo_corto_plazo_activo(void)
{
	if (config_scheduler == NULL) {
		return "DESCONOCIDO";
	}

	if (obtener_algoritmo_global() == ALGORITMO_CMN) {
		return "CMN";
	}

	return algoritmo_a_string(obtener_algoritmo_global());
}


static void reencolar_ready_final(t_pcb* proceso)
{
	pthread_mutex_lock(&mutex_ready);
	if (cola_procesos_ready != NULL) {
		queue_push(cola_procesos_ready, proceso);
	}
	pthread_mutex_unlock(&mutex_ready);
}

static void reencolar_ready_inicio(t_pcb* proceso)
{
	pthread_mutex_lock(&mutex_ready);
	if (cola_procesos_ready != NULL) {
		list_add_in_index(cola_procesos_ready->elements, 0, proceso);
	}
	pthread_mutex_unlock(&mutex_ready);
}

void planificadorCortoPlazo(void)
{
	log_debug(logger, "Planificador a Corto Plazo iniciado");
	t_algoritmo_corto_plazo algoritmo_global = (t_algoritmo_corto_plazo)obtener_algoritmo_global();

	if (algoritmo_global == ALGORITMO_DESCONOCIDO) {
		log_warning(logger,
			"PLANIFICATION_ALGORITHM invalido. Se utilizara FIFO por defecto");
		algoritmo_global = ALGORITMO_FIFO;
	}

	while (!sistema_corrupto) {
		log_debug(logger, " Esperando proceso en READY...");
		sem_wait(&sem_ready);

		if (sistema_corrupto) {
			break;
		}

		log_debug(logger, "Despertó de sem_ready. Verificando estado.");

		// Si hay compactación en progreso, esperar hasta que termine
		if (compactacion_en_progreso) {
			log_debug(logger, "Compactación en progreso - suspendiendo planificación");
			sem_post(&sem_ready);
			usleep(100000); // Esperar 100ms y reintentar
			continue;
		}

		log_debug(logger, "CPU libres: %s", hay_cpu_libre() ? "SI" : "NO");
		
		if (!hay_cpu_libre()) {
			log_debug(logger, "No hay CPU libre. Esperando a que se libere una CPU...");
			sem_post(&sem_ready);
			sem_wait(&sem_cpu_disponible);
			continue;
		}

		t_pcb* proceso = obtener_proximo_proceso_ready(algoritmo_global);

		if (proceso == NULL) {
			log_debug(logger, "No se pudo obtener un proceso READY para planificar");
			continue;
		}

		log_debug(logger, " Proceso desencolado: PID %d, Prioridad %d", proceso->pid, proceso->prioridad);
		uint32_t quantum = quantum_para_algoritmo(algoritmo_global, proceso->prioridad);
		bool fifo = algoritmo_debe_usar_fifo(algoritmo_global, proceso->prioridad);
		bool rr = !fifo && quantum > 0;

		if (algoritmo_global == ALGORITMO_CMN && cantidad_niveles_prioridad <= 0) {
			log_warning(logger,
				"CMN configurado sin colas multinivel. Se aplicara FIFO por defecto");
			fifo = true;
			rr = false;
			quantum = 0;
		}

		if (algoritmo_global == ALGORITMO_CMN &&
			(proceso->prioridad < 0 || proceso->prioridad >= cantidad_niveles_prioridad)) {
			log_warning(logger,
				"PID %d con prioridad %d fuera de rango. Se descarta de la planificacion",
				proceso->pid,
				proceso->prioridad);
			continue;
		}

		if (algoritmo_global == ALGORITMO_CMN && !fifo && quantum == 0) {
			rr = false;
		}

		if (algoritmo_global != ALGORITMO_CMN && !fifo && !rr) {
			if (algoritmo_global == ALGORITMO_RR) {
				rr = true;
				quantum = (uint32_t)config_scheduler->rr_quantum;
			} else {
				fifo = true;
			}
		}

		log_debug(logger, "Intentando enviar PID %d a CPU. Algoritmo: %s, Quantum: %u", 
				proceso->pid,
				fifo ? "FIFO" : "RR",
				quantum);

		int cpu_id_asignado = enviar_proceso_a_cpu(proceso);
		if (cpu_id_asignado < 0) {
			log_debug(logger, "[DIAG] enviar_proceso_a_cpu() falló! Recolando PID %d", proceso->pid);
			reencolar_por_fallo_de_envio(proceso, algoritmo_global);

			sem_post(&sem_ready);
			while (!sistema_corrupto && !hay_cpu_libre()) {
				sem_wait(&sem_cpu_disponible);
			}
			continue;
		}

		proceso->estado = EXEC;
		proceso->tiempo_inicio_ejecucion = time(NULL);
		proceso->conteo_context_switch++;
		proceso->quantum_restante = quantum;

		if (fifo) {
			log_info(logger, "## (%d) Pasa del estado READY al estado EXEC [FIFO]", proceso->pid);
		} else if (rr) {
			log_info(logger, "## (%d) Pasa del estado READY al estado EXEC [RR q=%u]", proceso->pid, proceso->quantum_restante);
			iniciar_quantum_timer(cpu_id_asignado, proceso->pid, quantum);
		} else {
			log_info(logger, "## (%d) Pasa del estado READY al estado EXEC [FIFO default]", proceso->pid);
		}
	}

	log_debug(logger, "Planificador a Corto Plazo terminado");
}

void* _planificador_corto_plazo_thread(void* arg)
{
	(void)arg;
	planificadorCortoPlazo();
	return NULL;
}

void iniciar_planificador_corto_plazo(void)
{
	log_debug(logger, "Algoritmo de planificacion a corto plazo activo: %s", algoritmo_corto_plazo_activo());
	if (queue_preemption_habilitada()) {
		log_debug(logger, "Desalojo entre colas habilitado por configuracion");
	}

	pthread_t thread_pcp;
	if (pthread_create(&thread_pcp, NULL, _planificador_corto_plazo_thread, NULL) != 0) {
		log_error(logger, "Error al crear thread del Planificador a Corto Plazo");
		return;
	}

	pthread_detach(thread_pcp);
	log_debug(logger, "Thread Planificador a Corto Plazo creado");
}
