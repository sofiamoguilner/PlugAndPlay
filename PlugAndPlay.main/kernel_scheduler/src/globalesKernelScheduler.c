#include "globalesKernelScheduler.h"
#include "scheduler_config.h"
#include "planificadorCortoPlazo.h"

#include <strings.h>
#include <string.h>
#include <stdlib.h>

t_queue* cola_procesos_new = NULL;
t_queue* cola_procesos_ready = NULL;

/* Colas multinivel por prioridad */
t_queue* colas_ready_por_prioridad[MAX_PRIORITY_LEVELS] = {0};
int cantidad_niveles_prioridad = 0;

pthread_mutex_t mutex_new = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_ready = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_colas_prioridad = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_cpus = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_bsod = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_memory_socket = PTHREAD_MUTEX_INITIALIZER;

t_list* todos_los_procesos = NULL;
pthread_mutex_t mutex_todos_los_procesos = PTHREAD_MUTEX_INITIALIZER;

t_list* lista_mutexes = NULL;
pthread_mutex_t mutex_lista_mutexes = PTHREAD_MUTEX_INITIALIZER;

sem_t sem_new;
sem_t sem_ready;
sem_t sem_cpu_disponible;

t_list* cpus = NULL;

volatile int sistema_corrupto = 0;
volatile int compactacion_en_progreso = 0;

bool string_igual_ignora_mayusculas(const char* izquierda, const char* derecha)
{
	return izquierda != NULL && derecha != NULL && strcasecmp(izquierda, derecha) == 0;
}

int obtener_algoritmo_global(void)
{
	if (config_scheduler == NULL || config_scheduler->planification_algorithm == NULL) {
		return ALGORITMO_DESCONOCIDO;
	}

	if (string_igual_ignora_mayusculas(config_scheduler->planification_algorithm, "FIFO")) {
		return ALGORITMO_FIFO;
	}

	if (string_igual_ignora_mayusculas(config_scheduler->planification_algorithm, "RR")) {
		return ALGORITMO_RR;
	}

	if (string_igual_ignora_mayusculas(config_scheduler->planification_algorithm, "CMN")) {
		return ALGORITMO_CMN;
	}

	return ALGORITMO_DESCONOCIDO;
}

int obtener_algoritmo_por_prioridad(int prioridad)
{
	if (config_scheduler == NULL || prioridad < 0 || prioridad >= config_scheduler->cantidad_colas) {
		return ALGORITMO_DESCONOCIDO;
	}

	if (config_scheduler->algoritmos_colas[prioridad] == NULL) {
		return ALGORITMO_DESCONOCIDO;
	}

	if (string_igual_ignora_mayusculas(config_scheduler->algoritmos_colas[prioridad], "FIFO")) {
		return ALGORITMO_FIFO;
	}

	if (string_igual_ignora_mayusculas(config_scheduler->algoritmos_colas[prioridad], "RR")) {
		return ALGORITMO_RR;
	}

	return ALGORITMO_DESCONOCIDO;
}

const char* algoritmo_a_string(int algoritmo)
{
	switch ((t_algoritmo_corto_plazo)algoritmo) {
	case ALGORITMO_FIFO:
		return "FIFO";
	case ALGORITMO_RR:
		return "RR";
	case ALGORITMO_CMN:
		return "CMN";
	default:
		return "DESCONOCIDO";
	}
}

bool queue_preemption_habilitada(void)
{
	return config_scheduler != NULL &&
		   config_scheduler->queue_preemption != NULL &&
		   string_igual_ignora_mayusculas(config_scheduler->queue_preemption, "TRUE");
}

t_pcb* pop_ready(void)
{
	t_pcb* proceso = NULL;

	pthread_mutex_lock(&mutex_ready);
	if (cola_procesos_ready != NULL && !queue_is_empty(cola_procesos_ready)) {
		proceso = queue_pop(cola_procesos_ready);
	}
	pthread_mutex_unlock(&mutex_ready);

	return proceso;
}

void inicializar_recursos_kernel_scheduler(void) {
	if (cola_procesos_new == NULL) {
		cola_procesos_new = queue_create();
	}

	if (cola_procesos_ready == NULL) {
		cola_procesos_ready = queue_create();
	}

	if (cpus == NULL) {
		cpus = list_create();
	}

	if (todos_los_procesos == NULL) {
		todos_los_procesos = list_create();
	}

	if (lista_mutexes == NULL) {
		lista_mutexes = list_create();
	}

	sem_init(&sem_new, 0, 0);
	sem_init(&sem_ready, 0, 0);
	sem_init(&sem_cpu_disponible, 0, 0);
}

void inicializar_colas_multinivel(int cantidad_colas, t_config_scheduler* config)
{
	if (cantidad_colas <= 0) {
		log_warning(logger, "No se especificó cantidad de colas, usando configuración por defecto");
		cantidad_niveles_prioridad = 0;
		return;
	}

	if (cantidad_colas > MAX_PRIORITY_LEVELS) {
		cantidad_colas = MAX_PRIORITY_LEVELS;
	}

	cantidad_niveles_prioridad = cantidad_colas;

	for (int i = 0; i < cantidad_colas; i++) {
		if (colas_ready_por_prioridad[i] == NULL) {
			colas_ready_por_prioridad[i] = queue_create();
		}
	}

	log_debug(logger, "Colas multinivel inicializadas: %d nivel(es) de prioridad",
		cantidad_colas);

	for (int i = 0; i < cantidad_colas; i++) {
		log_debug(logger, "  - Cola prioridad %d: algoritmo %s",
			i,
			(config != NULL && config->algoritmos_colas[i] != NULL)
				? config->algoritmos_colas[i]
				: "FIFO");
	}
}

/* =============================================================================
	HELPERS DE MUTEX Y PRIORIDADES
============================================================================= */

extern t_log* logger;
extern void encolar_en_ready_y_notificar(t_pcb* proceso);

const char* estado_a_string(t_estado estado) {
	switch (estado) {
		case NEW: return "NEW";
		case READY: return "READY";
		case EXEC: return "EXEC";
		case BLOCK: return "BLOCK";
		case SUSP_READY: return "SUSP_READY";
		case SUSP_BLOCK: return "SUSP_BLOCK";
		case EXIT_STATE: return "EXIT";
		default: return "DESCONOCIDO";
	}
}

t_pcb* buscar_pcb_por_pid(int pid) {
	t_pcb* resultado = NULL;
	pthread_mutex_lock(&mutex_todos_los_procesos);
	if (todos_los_procesos != NULL) {
		for (int i = 0; i < list_size(todos_los_procesos); i++) {
			t_pcb* p = list_get(todos_los_procesos, i);
			if (p->pid == pid) {
				resultado = p;
				break;
			}
		}
	}
	pthread_mutex_unlock(&mutex_todos_los_procesos);
	return resultado;
}

void registrar_pcb(t_pcb* pcb) {
	if (pcb == NULL) return;
	pthread_mutex_lock(&mutex_todos_los_procesos);
	if (todos_los_procesos != NULL) {
		list_add(todos_los_procesos, pcb);
	}
	pthread_mutex_unlock(&mutex_todos_los_procesos);
}

void eliminar_pcb(int pid) {
	pthread_mutex_lock(&mutex_todos_los_procesos);
	if (todos_los_procesos != NULL) {
		for (int i = 0; i < list_size(todos_los_procesos); i++) {
			t_pcb* p = list_get(todos_los_procesos, i);
			if (p->pid == pid) {
				list_remove(todos_los_procesos, i);
				/* Liberar la lista de segmentos del PMP antes de que el
				 * caller haga free(pcb). */
				if (p->segmentos_info != NULL) {
					list_destroy_and_destroy_elements(p->segmentos_info, free);
					p->segmentos_info = NULL;
				}
				break;
			}
		}
	}
	pthread_mutex_unlock(&mutex_todos_los_procesos);
}

t_mutex_sistema* buscar_mutex_por_nombre(const char* nombre) {
	if (nombre == NULL) return NULL;
	t_mutex_sistema* resultado = NULL;
	pthread_mutex_lock(&mutex_lista_mutexes);
	if (lista_mutexes != NULL) {
		for (int i = 0; i < list_size(lista_mutexes); i++) {
			t_mutex_sistema* m = list_get(lista_mutexes, i);
			if (m->nombre != NULL && strcmp(m->nombre, nombre) == 0) {
				resultado = m;
				break;
			}
		}
	}
	pthread_mutex_unlock(&mutex_lista_mutexes);
	return resultado;
}

void registrar_mutex(t_mutex_sistema* mut) {
	if (mut == NULL) return;
	pthread_mutex_lock(&mutex_lista_mutexes);
	if (lista_mutexes != NULL) {
		list_add(lista_mutexes, mut);
	}
	pthread_mutex_unlock(&mutex_lista_mutexes);
}

void liberar_mutexes_de_proceso(int pid) {
	pthread_mutex_lock(&mutex_lista_mutexes);
	if (lista_mutexes != NULL) {
		for (int i = 0; i < list_size(lista_mutexes); i++) {
			t_mutex_sistema* m = list_get(lista_mutexes, i);
			if (m->asignado_a_pid == pid) {
				log_info(logger, "## (%d) Libera el Mutex %s (por finalización de proceso)", pid, m->nombre);

				if (!queue_is_empty(m->cola_bloqueados)) {
					t_pcb* siguiente = queue_pop(m->cola_bloqueados);
					m->asignado_a_pid = siguiente->pid;
					siguiente->mutex_esperando = NULL;

					log_info(logger, "## (%d) Toma el Mutex %s", siguiente->pid, m->nombre);

					t_estado anterior = siguiente->estado;
					siguiente->estado = READY;
					log_info(logger, "## (%d) Pasa del estado %s al estado READY", siguiente->pid, estado_a_string(anterior));

					encolar_en_ready_y_notificar(siguiente);
					recalcular_prioridad_dinamica(siguiente);
				} else {
					m->asignado_a_pid = -1;
				}
			} else {
				t_queue* nueva_cola = queue_create();
				while (!queue_is_empty(m->cola_bloqueados)) {
					t_pcb* pcb_esperando = queue_pop(m->cola_bloqueados);
					if (pcb_esperando->pid != pid) {
						queue_push(nueva_cola, pcb_esperando);
					}
				}
				queue_destroy(m->cola_bloqueados);
				m->cola_bloqueados = nueva_cola;
			}
		}
	}
	pthread_mutex_unlock(&mutex_lista_mutexes);
}

/* =============================================================================
	HERENCIA DE PRIORIDADES
============================================================================= */

void reubicar_proceso_en_ready(t_pcb* pcb, int prioridad_anterior, int prioridad_nueva) {
	if (pcb->estado != READY || cantidad_niveles_prioridad <= 0) {
		return;
	}

	if (prioridad_anterior == prioridad_nueva) {
		return;
	}

	pthread_mutex_lock(&mutex_colas_prioridad);
	t_queue* cola_ant = colas_ready_por_prioridad[prioridad_anterior];
	if (cola_ant != NULL) {
		bool removido = false;
		for (int i = 0; i < list_size(cola_ant->elements); i++) {
			t_pcb* p = list_get(cola_ant->elements, i);
			if (p != NULL && p->pid == pcb->pid) {
				list_remove(cola_ant->elements, i);
				removido = true;
				break;
			}
		}

		if (removido) {
			t_queue* cola_nueva = colas_ready_por_prioridad[prioridad_nueva];
			if (cola_nueva != NULL) {
				queue_push(cola_nueva, pcb);
				log_debug(logger, "Proceso PID %d reubicado de READY prioridad %d a prioridad %d", pcb->pid, prioridad_anterior, prioridad_nueva);
			}
		}
	}
	pthread_mutex_unlock(&mutex_colas_prioridad);
}

void propagar_herencia_prioridad(t_pcb* proceso_bloqueado, int prioridad_a_heredar) {
	if (proceso_bloqueado == NULL || proceso_bloqueado->mutex_esperando == NULL) {
		return;
	}

	t_mutex_sistema* mut = (t_mutex_sistema*)proceso_bloqueado->mutex_esperando;
	if (mut->asignado_a_pid == -1) {
		return;
	}

	t_pcb* poseedor = buscar_pcb_por_pid(mut->asignado_a_pid);
	if (poseedor == NULL) {
		return;
	}

	if (prioridad_a_heredar < poseedor->prioridad_dinamica) {
		int anterior_prioridad = poseedor->prioridad_dinamica;
		poseedor->prioridad_dinamica = prioridad_a_heredar;

		log_info(logger, "## %d Cambio de prioridad: %d - %d", poseedor->pid, anterior_prioridad, poseedor->prioridad_dinamica);

		reubicar_proceso_en_ready(poseedor, anterior_prioridad, poseedor->prioridad_dinamica);

		if (poseedor->mutex_esperando != NULL) {
			propagar_herencia_prioridad(poseedor, prioridad_a_heredar);
		}
	}
}

void recalcular_prioridad_dinamica(t_pcb* proceso) {
	if (proceso == NULL) return;

	int max_prioridad = proceso->prioridad;

	pthread_mutex_lock(&mutex_lista_mutexes);
	if (lista_mutexes != NULL) {
		for (int i = 0; i < list_size(lista_mutexes); i++) {
			t_mutex_sistema* m = list_get(lista_mutexes, i);
			if (m->asignado_a_pid == proceso->pid) {
				for (int j = 0; j < list_size(m->cola_bloqueados->elements); j++) {
					t_pcb* bloqueado = list_get(m->cola_bloqueados->elements, j);
					if (bloqueado != NULL && bloqueado->prioridad_dinamica < max_prioridad) {
						max_prioridad = bloqueado->prioridad_dinamica;
					}
				}
			}
		}
	}
	pthread_mutex_unlock(&mutex_lista_mutexes);

	int anterior_prioridad = proceso->prioridad_dinamica;
	if (max_prioridad != anterior_prioridad) {
		proceso->prioridad_dinamica = max_prioridad;
		log_info(logger, "## %d Cambio de prioridad: %d - %d", proceso->pid, anterior_prioridad, proceso->prioridad_dinamica);

		reubicar_proceso_en_ready(proceso, anterior_prioridad, proceso->prioridad_dinamica);
	}
}