#include "memory_io.h"
#include "memory_conexiones.h"
#include <utils/serializacion.h>
#include <utils/mensajes.h>
#include <commons/log.h>
#include <stdlib.h>
#include <sys/socket.h>

extern t_log* logger;

void* memory_io_leer(int pid, uint32_t dir_fisica, uint32_t tamanio)
{
    uint8_t* resultado = malloc(tamanio);
    if (!resultado) return NULL;

    uint32_t restante   = tamanio;
    uint32_t dir_act    = dir_fisica;
    uint32_t offset_res = 0;

    while (restante > 0) {
        int idx = conexiones_buscar_stick_por_dir(dir_act);
        if (idx < 0) {
            log_error(logger, "leer_de_memory_sticks: dir_fisica=0x%X sin stick", dir_act);
            free(resultado);
            return NULL;
        }

        uint32_t base    = conexiones_stick_base_fisica(idx);
        uint32_t tam_st  = conexiones_stick_tamanio(idx);
        int      out_sock = conexiones_stick_outbound_socket(idx);

        uint32_t local_offset = dir_act - base;
        uint32_t chunk = tam_st - local_offset;
        if (chunk > restante) chunk = restante;

        conexiones_stick_outbound_lock(idx);

        t_paquete* p = crear_paquete(LECTURA_MEMORIA);
        agregar_a_paquete(p, &pid,          sizeof(pid));
        agregar_a_paquete(p, &local_offset, sizeof(local_offset));
        agregar_a_paquete(p, &chunk,        sizeof(chunk));
        enviar_paquete(p, out_sock);
        eliminar_paquete(p);

        op_code cod = recibir_codigo_operacion(out_sock);
        if ((int)cod == -1) {
            conexiones_stick_outbound_unlock(idx);
            log_error(logger, "Memory Stick desconectado durante lectura (dir=0x%X)", dir_act);
            int sched = conexiones_get_scheduler_socket();
            if (sched >= 0) enviar_memoria_corrupta(sched);
            free(resultado);
            return NULL;
        }

        validar_buffer(out_sock);
        ssize_t n = recv(out_sock, resultado + offset_res, chunk, MSG_WAITALL);
        conexiones_stick_outbound_unlock(idx);

        if (n != (ssize_t)chunk) {
            log_error(logger, "Lectura incompleta del stick (esperaba %u, recibio %zd)", chunk, n);
            int sched = conexiones_get_scheduler_socket();
            if (sched >= 0) enviar_memoria_corrupta(sched);
            free(resultado);
            return NULL;
        }

        dir_act    += chunk;
        offset_res += chunk;
        restante   -= chunk;
    }

    return resultado;
}

int memory_io_escribir(int pid, uint32_t dir_fisica, uint32_t tamanio, void* datos)
{
    uint32_t restante   = tamanio;
    uint32_t dir_act    = dir_fisica;
    uint32_t offset_src = 0;

    while (restante > 0) {
        int idx = conexiones_buscar_stick_por_dir(dir_act);
        if (idx < 0) {
            log_error(logger, "escribir_en_memory_sticks: dir_fisica=0x%X sin stick", dir_act);
            return -1;
        }

        uint32_t base    = conexiones_stick_base_fisica(idx);
        uint32_t tam_st  = conexiones_stick_tamanio(idx);
        int      out_sock = conexiones_stick_outbound_socket(idx);

        uint32_t local_offset = dir_act - base;
        uint32_t chunk = tam_st - local_offset;
        if (chunk > restante) chunk = restante;

        conexiones_stick_outbound_lock(idx);

        t_paquete* p = crear_paquete(ESCRITURA_MEMORIA);
        agregar_a_paquete(p, &pid,          sizeof(pid));
        agregar_a_paquete(p, &local_offset, sizeof(local_offset));
        agregar_a_paquete(p, &chunk,        sizeof(chunk));
        agregar_a_paquete(p, (uint8_t*)datos + offset_src, (int)chunk);
        enviar_paquete(p, out_sock);
        eliminar_paquete(p);

        op_code cod = recibir_codigo_operacion(out_sock);
        if ((int)cod == -1) {
            conexiones_stick_outbound_unlock(idx);
            log_error(logger, "Memory Stick desconectado durante escritura (dir=0x%X)", dir_act);
            int sched = conexiones_get_scheduler_socket();
            if (sched >= 0) enviar_memoria_corrupta(sched);
            return -1;
        }
        recibir_ok(out_sock);
        conexiones_stick_outbound_unlock(idx);

        dir_act    += chunk;
        offset_src += chunk;
        restante   -= chunk;
    }

    return 0;
}
