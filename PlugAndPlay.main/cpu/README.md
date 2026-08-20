# Módulo CPU

## Estado actual (2026-04-23)

El módulo CPU está **funcional** en su flujo principal. Se conecta al Kernel Scheduler y al Kernel Memory, recibe procesos, ejecuta el ciclo de instrucción completo y devuelve el proceso al Scheduler indicando el motivo de desalojo.

---

## Estructura de archivos

```
src/
├── main.c              — Punto de entrada: inicializa config, logger, conexiones y loop principal
├── cpu_types.h         — Tipos compartidos (t_instruccion, t_conexiones_cpu, t_resultado_execute, etc.)
├── cpu_config.c/h      — Carga de configuración y logger
├── cpu_ciclo.c/h       — Ciclo Fetch → Decode → Execute → Check Interrupt
├── cpu_scheduler.c/h   — Conexión con Kernel Scheduler, listener de EJECUTAR_PROCESO e INTERRUPCION
└── cpu_memory.c/h      — Conexión con Kernel Memory y Memory Sticks, listener de respuestas
```

---

## Flujo de ejecución

```
main()
 └─ esperar_pid_del_scheduler()   ← sem_wait, desbloquea cuando Scheduler manda EJECUTAR_PROCESO
     └─ ejecutar_ciclo_instruccion(pid)
         ├─ solicitar_contexto_a_memory()
         └─ loop:
             ├─ FETCH   → enviar_obtener_instruccion() → Memory responde con la línea
             ├─ DECODE  → parsea nombre + parámetros, clasifica (syscall / requiere MMU / normal)
             ├─ EXECUTE → ejecuta la instrucción, devuelve: CONTINUAR / SYSCALL / EXIT_PROCESO / ERROR
             └─ CHECK_INTERRUPT → si hay interrupción pendiente, guarda contexto y desaloja (MOTIVO_QUANTUM)
```

### Motivos de desalojo enviados al Scheduler

| Motivo          | Cuando ocurre                                      |
|-----------------|----------------------------------------------------|
| `MOTIVO_QUANTUM`  | Se recibió una interrupción del Scheduler           |
| `MOTIVO_SYSCALL`  | La instrucción ejecutada es una syscall            |
| `MOTIVO_EXIT`     | Se ejecutó la instrucción `EXIT`                   |
| `MOTIVO_ERROR`    | Error en FETCH, DECODE o EXECUTE                   |

---

## Instrucciones implementadas

### Instrucciones CPU (ejecutadas localmente)

| Instrucción | Descripción                                           |
|-------------|-------------------------------------------------------|
| `NOOP`      | Sin efecto, avanza PC                                 |
| `SET`       | Asigna un valor literal a un registro (o al PC)       |
| `SUM`       | Suma registro + registro o registro + literal         |
| `SUB`       | Resta registro - registro o registro - literal        |
| `JNZ`       | Salta al PC indicado si el registro no es cero        |

### Instrucciones pendientes de MMU (detectadas pero no procesadas aún)

| Instrucción | Descripción                          |
|-------------|--------------------------------------|
| `MOV_IN`    | Leer desde memoria al registro       |
| `MOV_OUT`   | Escribir desde registro a memoria    |
| `COPY_MEM`  | Copiar entre posiciones de memoria   |

Estas instrucciones son reconocidas y marcadas con `requiere_mmu = true`, pero la lógica de traducción de direcciones vía MMU **aún no está implementada**.

### Syscalls (detectadas, devueltas al Scheduler)

`SLEEP`, `STDIN`, `STDOUT`, `INIT_PROC`, `EXIT`, `MEM_ALLOC`, `MEM_FREE`, `MUTEX_CREATE`, `MUTEX_LOCK`, `MUTEX_UNLOCK`

---

## Registros disponibles

| 8 bits          | 32 bits             |
|-----------------|---------------------|
| AX, BX, CX, DX | EAX, EBX, ECX, EDX  |
| SI, DI          |                     |

---

## Conexiones y threading

- **Kernel Memory** → conexión persistente + thread listener (`cpu_memory_listener_thread`)
  - Respuestas sincronizadas con `sem_t respuesta_lista` + `pthread_mutex_t respuesta_lock`
  - Soporte para hasta 16 Memory Sticks (conectados dinámicamente via `NUEVA_MEMORIA_DISPONIBLE`)
- **Kernel Scheduler** → conexión persistente + thread listener (`cpu_scheduler_listener_thread`)
  - Despacho de procesos via `sem_t sem_proceso_listo`
  - Interrupciones protegidas con `pthread_mutex_t interrupcion_lock`

---

## Configuración (`cpu.conf`)

```
LOG_LEVEL=INFO
SCHEDULER_IP=127.0.0.1
SCHEDULER_PORT=8001
MEMORY_IP=127.0.0.1
MEMORY_PORT=8000
```

> **Nota:** El `CPU_ID` y el path al config están hardcodeados en `main.c` (el parsing por `argv` está comentado). Hay que descomentarlo antes de la entrega.

---

## Pendiente / TODOs

- [ ] Implementar MMU: traducción de direcciones para `MOV_IN`, `MOV_OUT`, `COPY_MEM`
- [ ] Descomentar y validar el parsing de argumentos (`argv[1]`, `argv[2]`) en `main.c`
- [ ] Manejo del caso donde Memory Stick responde a lecturas/escrituras (socket directo a MS)
- [ ] El semáforo `respuesta_lista` es compartido entre instrucción, contexto y ACK de actualización — si llegan respuestas fuera de orden puede haber race condition
