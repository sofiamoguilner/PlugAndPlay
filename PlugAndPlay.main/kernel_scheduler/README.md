# Kernel Scheduler

Módulo que actúa como el núcleo planificador del sistema operativo simulado.
Se encarga de gestionar el ciclo de vida de los procesos, administrar las colas
de planificación y coordinar la comunicación con los demás módulos.

---

## Compilación y ejecución

```bash
cd kernel_scheduler
make
./bin/kernel_scheduler kernel_scheduler.conf
```

Para levantar el entorno completo (Kernel Memory, CPUs, IO, SWAP y Scheduler) y
correr una prueba, usá el launcher del repo:

```bash
# desde la raíz del repo
bash scripts/run_all.sh PLANI_PRE_0.prc     # o PCP.prc / PLANI_MEM.prc / PMP.prc / PHP.prc
```

Al terminar, verificá los Resultados Esperados de la prueba con:

```bash
./scripts/check_prueba.sh
```

Ver `scripts/pruebas/README.md` para los presets de config y tamaños de Memory Stick
de cada prueba final.

---

## Configuración (`kernel_scheduler.conf`)

| Parámetro              | Descripción                                    | Valor por defecto |
|------------------------|------------------------------------------------|-------------------|
| `LOG_LEVEL`            | Nivel de log (`INFO`, `DEBUG`, `WARNING`, `ERROR`) | `INFO`        |
| `PLANIFICATION_ALGORITHM` | Algoritmo de planificación (`RR`, etc.)   | `RR`              |
| `QUEUE_PREEMPTION`     | Habilitar desalojo (`YES` / `NO`)              | `YES`             |
| `RR_QUANTUM`           | Quantum de tiempo para Round Robin             | `3`               |
| `SUSPENSION_TIMEOUT`   | Timeout para suspender un proceso (segundos)   | `30`              |
| `PUERTO_ESCUCHA`       | Puerto donde escucha CPUs e I/O                | `8001`            |
| `MEMORY_IP`            | IP del módulo Kernel Memory                    | `127.0.0.1`       |
| `MEMORY_PORT`          | Puerto del módulo Kernel Memory                | `8000`            |

---

## Arquitectura de threads

Al iniciarse, el módulo levanta los siguientes threads:

| Thread | Función | Descripción |
|--------|---------|-------------|
| Long-term scheduler | `_planificador_largo_plazo_thread` | Mueve procesos de NEW → READY y los despacha a la CPU |
| BSOD monitor | `monitor_bsod_thread` | Detecta señales de corrupción de memoria |
| Scheduler listener | `scheduler_listener_thread` | Acepta conexiones entrantes de CPUs e I/O |
| CPU receptor (×N) | `scheduler_cpu_receptor_thread` | Un thread por CPU conectada; recibe mensajes de la CPU |

---

## Ciclo de vida de un proceso

```
                 crear_proceso()
                       │
                       ▼
                     [NEW]  ◄── cola_procesos_new
                       │
          planificador largo plazo
                       │
                       ▼
                    [READY] ◄── cola_procesos_ready
                       │
              enviar_proceso_a_cpu()
                       │
                       ▼
                    [EXEC]  ──► CPU ejecuta
                       │
          ┌────────────┼────────────┐
          ▼            ▼            ▼
       [BLOCK]      [READY]     [EXIT]
      (syscall      (quantum     (EXIT
       I/O etc.)    agotado)    instrucción)
```

### Estados (`t_estado`)

| Estado       | Descripción |
|--------------|-------------|
| `NEW`        | Proceso recién creado, pendiente de admisión |
| `READY`      | Listo para ejecutar, esperando CPU disponible |
| `EXEC`       | Ejecutándose en una CPU |
| `BLOCK`      | Bloqueado esperando I/O u otro recurso |
| `SUSP_READY` | Suspendido pero listo para retomar |
| `SUSP_BLOCK` | Suspendido y bloqueado |
| `EXIT_STATE` | Finalizado |

---

## Estructura PCB (`t_pcb`)

```c
typedef struct {
    int       pid;
    int       prioridad;                // Prioridad estática
    int       prioridad_dinamica;       // Prioridad dinámica (modificable)
    t_estado  estado;
    uint32_t  quantum_restante;
    time_t    tiempo_llegada;
    time_t    tiempo_inicio_ejecucion;
    uint32_t  tiempo_total_cpu;
    uint32_t  conteo_context_switch;
} t_pcb;
```

---

## Planificadores

### Largo plazo (`planificadorLargoPlazo.c`)

- Espera en `sem_new` a que haya procesos en la cola NEW.
- Desencola de `cola_procesos_new`, cambia el estado a READY.
- Encola en `cola_procesos_ready` y postea `sem_ready`.
- Llama a `enviar_proceso_a_cpu()` para despachar si hay CPU libre.

### Corto plazo (`planificadorCortoPlazo.c`)

> **Estado:** pendiente de implementación.

---

## Comunicación con otros módulos

### Kernel Memory (`conectar_a_kernel_memory`)

Conexión de salida. El scheduler se conecta al iniciar.

| Mensaje enviado | Función | Descripción |
|----------------|---------|-------------|
| `HANDSHAKE_SCHEDULER_A_MEMORY` | `enviar_handshake_scheduler_a_memory()` | Identificación inicial |
| `OBTENER_ESPACIO_LIBRE` | `enviar_obtener_espacio_libre()` | Consulta espacio disponible antes de crear proceso |
| `CREAR_PROCESO` | `enviar_crear_proceso(pid, path)` | Notifica creación de proceso con su script |

| Mensaje recibido | Descripción |
|-----------------|-------------|
| `MEMORIA_CORRUPTA_BSOD` | Señal de corrupción — dispara BSOD |

### CPU (`iniciar_servidor_scheduler`)

Servidor en `PUERTO_ESCUCHA`. Las CPUs se conectan al scheduler.

| Mensaje enviado | Función | Descripción |
|----------------|---------|-------------|
| `EJECUTAR_PROCESO` | `enviar_pid(socket, pid)` | Despacha un PID a la CPU para que lo ejecute |

| Mensaje recibido | Descripción |
|-----------------|-------------|
| `HANDSHAKE_CPU` | Registro de nueva CPU con su ID |
| `DEVOLVER_PROCESO` | CPU terminó su turno (quantum, syscall, EXIT) |

### I/O

Las interfaces I/O se conectan al scheduler. Registro básico implementado;
lógica de atención pendiente.

---

## Sincronización

| Recurso | Mecanismo | Protege |
|---------|-----------|---------|
| `cola_procesos_new` | `mutex_new` + `sem_new` | Cola NEW |
| `cola_procesos_ready` | `mutex_ready` + `sem_ready` | Cola READY |
| `cpus` | `mutex_cpus` | Lista de CPUs conectadas |
| `active.lock` | `pthread_mutex_t` | Tabla de conexiones activas |
| `sistema_corrupto` | `mutex_bsod` | Flag de BSOD |

---

## BSOD

Si Kernel Memory envía `MEMORIA_CORRUPTA_BSOD`:

1. `sistema_corrupto = 1`
2. Se loguea el evento.
3. Se destruyen todas las colas de procesos.
4. Se llama a `exit(EXIT_FAILURE)`.

---

## Archivos fuente

| Archivo | Responsabilidad |
|---------|----------------|
| `src/main.c` | Inicialización y loop principal |
| `src/scheduler_config.c/h` | Carga de configuración |
| `src/globalesKernelScheduler.c/h` | Variables globales, colas, semáforos |
| `src/crearProceso.c/h` | Creación de procesos y comunicación con Memory |
| `src/planificadorLargoPlazo.c/h` | Planificador de largo plazo |
| `src/planificadorCortoPlazo.c/h` | Planificador de corto plazo (pendiente) |
| `src/scheduler_conexiones.c/h` | Servidor de conexiones (CPUs, I/O) |
| `src/utils_kernel_scheduler.c/h` | Handshakes, despacho a CPU, threads receptores |
