#include "memory_conexiones.h"
#include <commons/log.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

extern t_log* logger;

#define MAX_CPUS          1000
#define MAX_MEMORY_STICKS 1000

typedef struct {
    int socket;
    int cpu_id;
} t_cpu_entry;

typedef struct {
    int             socket;
    uint32_t        tamanio;
    char*           ip;
    char*           puerto;
    int             outbound_socket;
    pthread_mutex_t outbound_lock;
    uint32_t        base_fisica;
} t_memory_stick_entry;

typedef struct {
    int                  scheduler_socket;
    t_cpu_entry          cpus[MAX_CPUS];
    int                  cpu_count;
    t_memory_stick_entry sticks[MAX_MEMORY_STICKS];
    int                  stick_count;
    int                  swap_socket;
    pthread_mutex_t      lock;
} t_conexiones_activas;

static t_conexiones_activas active = {
    .scheduler_socket = -1,
    .cpu_count        = 0,
    .stick_count      = 0,
    .swap_socket      = -1,
};

void conexiones_init(void)
{
    pthread_mutex_init(&active.lock, NULL);
}

// -----------------------------------------------------------------------------
// Scheduler
// -----------------------------------------------------------------------------

void conexiones_set_scheduler_socket(int s)
{
    pthread_mutex_lock(&active.lock);
    active.scheduler_socket = s;
    pthread_mutex_unlock(&active.lock);
}

int conexiones_get_scheduler_socket(void)
{
    pthread_mutex_lock(&active.lock);
    int s = active.scheduler_socket;
    pthread_mutex_unlock(&active.lock);
    return s;
}

// -----------------------------------------------------------------------------
// Swap
// -----------------------------------------------------------------------------

void conexiones_set_swap_socket(int s)
{
    pthread_mutex_lock(&active.lock);
    active.swap_socket = s;
    pthread_mutex_unlock(&active.lock);
}

int conexiones_get_swap_socket(void)
{
    pthread_mutex_lock(&active.lock);
    int s = active.swap_socket;
    pthread_mutex_unlock(&active.lock);
    return s;
}

// -----------------------------------------------------------------------------
// CPUs
// -----------------------------------------------------------------------------

int conexiones_agregar_cpu(int socket, int cpu_id)
{
    pthread_mutex_lock(&active.lock);
    if (active.cpu_count >= MAX_CPUS) {
        pthread_mutex_unlock(&active.lock);
        return -1;
    }
    int idx = active.cpu_count;
    active.cpus[active.cpu_count++] = (t_cpu_entry){ socket, cpu_id };
    pthread_mutex_unlock(&active.lock);
    return idx;
}

void conexiones_invalidar_cpu_por_id(int cpu_id)
{
    pthread_mutex_lock(&active.lock);
    for (int i = 0; i < active.cpu_count; i++) {
        if (active.cpus[i].cpu_id == cpu_id) {
            active.cpus[i].socket = -1;
            break;
        }
    }
    pthread_mutex_unlock(&active.lock);
}

int conexiones_cantidad_cpus(void)
{
    pthread_mutex_lock(&active.lock);
    int n = active.cpu_count;
    pthread_mutex_unlock(&active.lock);
    return n;
}

int conexiones_cpu_socket_en(int idx)
{
    pthread_mutex_lock(&active.lock);
    int s = (idx >= 0 && idx < active.cpu_count) ? active.cpus[idx].socket : -1;
    pthread_mutex_unlock(&active.lock);
    return s;
}

int conexiones_cpu_id_en(int idx)
{
    pthread_mutex_lock(&active.lock);
    int id = (idx >= 0 && idx < active.cpu_count) ? active.cpus[idx].cpu_id : -1;
    pthread_mutex_unlock(&active.lock);
    return id;
}

// -----------------------------------------------------------------------------
// Memory Sticks
// -----------------------------------------------------------------------------

int conexiones_agregar_stick(int socket, uint32_t tamanio,
                             char* ip, char* puerto, uint32_t* out_base_fisica)
{
    pthread_mutex_lock(&active.lock);
    if (active.stick_count >= MAX_MEMORY_STICKS) {
        pthread_mutex_unlock(&active.lock);
        return -1;
    }

    uint32_t base_fisica = 0;
    for (int i = 0; i < active.stick_count; i++)
        base_fisica += active.sticks[i].tamanio;

    int idx = active.stick_count;
    t_memory_stick_entry* stick = &active.sticks[active.stick_count++];
    stick->socket          = socket;
    stick->tamanio         = tamanio;
    stick->ip              = ip;
    stick->puerto          = puerto;
    stick->base_fisica     = base_fisica;
    stick->outbound_socket = -1;

    if (out_base_fisica) *out_base_fisica = base_fisica;

    pthread_mutex_unlock(&active.lock);
    return idx;
}

int conexiones_cantidad_sticks(void)
{
    pthread_mutex_lock(&active.lock);
    int n = active.stick_count;
    pthread_mutex_unlock(&active.lock);
    return n;
}

int conexiones_stick_outbound_socket(int idx)
{
    pthread_mutex_lock(&active.lock);
    int s = (idx >= 0 && idx < active.stick_count) ? active.sticks[idx].outbound_socket : -1;
    pthread_mutex_unlock(&active.lock);
    return s;
}

void conexiones_stick_outbound_lock(int idx)
{
    if (idx < 0 || idx >= active.stick_count) return;
    pthread_mutex_lock(&active.sticks[idx].outbound_lock);
}

void conexiones_stick_outbound_unlock(int idx)
{
    if (idx < 0 || idx >= active.stick_count) return;
    pthread_mutex_unlock(&active.sticks[idx].outbound_lock);
}

void conexiones_stick_set_outbound_socket(int idx, int sock)
{
    pthread_mutex_lock(&active.lock);
    if (idx >= 0 && idx < active.stick_count) {
        active.sticks[idx].outbound_socket = sock;
        pthread_mutex_init(&active.sticks[idx].outbound_lock, NULL);
    }
    pthread_mutex_unlock(&active.lock);
}

void conexiones_stick_invalidate_por_indice(int idx, char** out_ip, char** out_puerto)
{
    pthread_mutex_lock(&active.lock);
    if (idx >= 0 && idx < active.stick_count) {
        if (out_ip)     *out_ip     = active.sticks[idx].ip;
        if (out_puerto) *out_puerto = active.sticks[idx].puerto;
        active.sticks[idx].socket = -1;
    } else {
        if (out_ip)     *out_ip     = NULL;
        if (out_puerto) *out_puerto = NULL;
    }
    pthread_mutex_unlock(&active.lock);
}

uint32_t conexiones_stick_base_fisica(int idx)
{
    pthread_mutex_lock(&active.lock);
    uint32_t b = (idx >= 0 && idx < active.stick_count) ? active.sticks[idx].base_fisica : 0;
    pthread_mutex_unlock(&active.lock);
    return b;
}

uint32_t conexiones_stick_tamanio(int idx)
{
    pthread_mutex_lock(&active.lock);
    uint32_t t = (idx >= 0 && idx < active.stick_count) ? active.sticks[idx].tamanio : 0;
    pthread_mutex_unlock(&active.lock);
    return t;
}

const char* conexiones_stick_ip(int idx)
{
    if (idx < 0 || idx >= active.stick_count) return NULL;
    return active.sticks[idx].ip;
}

const char* conexiones_stick_puerto(int idx)
{
    if (idx < 0 || idx >= active.stick_count) return NULL;
    return active.sticks[idx].puerto;
}

uint32_t conexiones_tamanio_total_ram(void)
{
    pthread_mutex_lock(&active.lock);
    uint32_t total = 0;
    for (int i = 0; i < active.stick_count; i++)
        total += active.sticks[i].tamanio;
    pthread_mutex_unlock(&active.lock);
    return total;
}

int conexiones_buscar_stick_por_dir(uint32_t dir_fisica)
{
    pthread_mutex_lock(&active.lock);
    int found = -1;
    for (int i = 0; i < active.stick_count; i++) {
        t_memory_stick_entry* s = &active.sticks[i];
        if (s->outbound_socket >= 0 &&
            dir_fisica >= s->base_fisica &&
            dir_fisica < s->base_fisica + s->tamanio) {
            found = i;
            break;
        }
    }
    pthread_mutex_unlock(&active.lock);
    return found;
}
