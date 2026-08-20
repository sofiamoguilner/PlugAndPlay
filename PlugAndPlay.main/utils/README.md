# TP 2026 1C — La Serpiente Shnissugah

Simulador de sistema operativo con múltiples módulos que se comunican via TCP. Implementa scheduling de procesos, gestión de memoria, ejecución de instrucciones y operaciones de I/O.

---

## Dependencias

Requiere la biblioteca [so-commons-library] de la cátedra:

```bash
git clone https://github.com/sisoputnfrba/so-commons-library
cd so-commons-library
make debug
make install
```

---

## Arquitectura general

```
                         ┌─────────────────────┐
                         │   Kernel Memory      │
                         │   (server :8000)     │
                         └──────────┬──────────┘
               ┌──────────┬─────────┼──────────┬──────────┐
               ▼          ▼         ▼          ▼          ▼
        Kernel Sched.   CPU(s)   Memory    Swap        I/O
        (server :8001)           Stick
```

Todos los módulos se conectan a **Kernel Memory** (servidor central). El Kernel Scheduler además actúa como servidor para los CPUs y módulos de I/O.

### Orden de inicio

1. **Kernel Memory** (primero — servidor central)
2. **Memory Stick** (se registra en Memory)
3. **Swap** (se registra en Memory)
4. **Kernel Scheduler** (se conecta a Memory, levanta su propio servidor)
5. **I/O** (se conecta al Scheduler)
6. **CPU(s)** (último — se conectan al Scheduler y a Memory)

```bash
./run.sh    # inicia todos los módulos en orden
./kill.sh   # termina todos los módulos
```

---

## Módulos

### Kernel Scheduler (`/kernel_scheduler`)

Orquestador de procesos. Implementa el ciclo de vida completo:

```
NEW → READY → EXEC → BLOCK → EXIT
              ↕          ↕
         SUSP_READY  SUSP_BLOCK
```

**Responsabilidades:**
- Mantiene colas de procesos por estado (`cola_procesos_new`, `cola_procesos_ready`)
- Planificador de largo plazo: admisión de procesos (NEW → READY)
- Despacho de procesos a CPUs disponibles
- Gestión de I/O: bloquea procesos y los desbloquea al finalizar
- Detección de BSOD (corrupción de memoria) enviada por Kernel Memory

**Algoritmo de scheduling:** configurable via `PLANIFICATION_ALGORITHM` (actualmente solo RR). El quantum se configura con `RR_QUANTUM`.

**Configuración (`kernel_scheduler.conf`):**
```
LOG_LEVEL=INFO
PLANIFICATION_ALGORITHM=RR
QUEUE_PREEMPTION=YES
RR_QUANTUM=3
SUSPENSION_TIMEOUT=30
PUERTO_ESCUCHA=8001
MEMORY_IP=127.0.0.1
MEMORY_PORT=8000
```

---

### Kernel Memory (`/kernel_memory`)

Servidor central de memoria. Todos los módulos se conectan a él.

**Responsabilidades:**
- Almacena los scripts de instrucciones de cada proceso (`{pid}.script`)
- Responde pedidos de fetch del CPU (instrucción por PID + PC)
- Notifica a todos los CPUs cuando se conecta un nuevo Memory Stick
- Coordina con el módulo Swap
- Emite señal BSOD si detecta corrupción de memoria

> **MOCK:** La gestión física de memoria está mockeada. `OBTENER_ESPACIO_LIBRE` siempre devuelve el valor de `TOTAL_MEMORY` del config. Las operaciones de lectura/escritura en memoria descartan los datos (solo loguean). No hay segmentación real ni compactación.

> **MOCK:** El contexto de proceso no se persiste. `GUARDAR_CONTEXTO` loguea pero descarta; `OBTENER_CONTEXTO` devuelve contexto vacío.

**Configuración (`kernel_memory.conf`):**
```
LOG_LEVEL=INFO
TOTAL_MEMORY=4096
SEGMENT_MAX_SIZE=1024
ALLOCATION_STRATEGY=FIRST_FIT   # existe pero no se aplica (mock)
INSTRUCTION_DELAY=10            # ms — configurado pero sin efecto todavía
COMPACTION_DELAY=50             # ms — configurado pero sin efecto todavía
SCRIPTS_BASEPATH=/ruta/a/scripts
PUERTO_ESCUCHA=8000
```

> **HARDCODEADO:** `SCRIPTS_BASEPATH` en el config apunta a `/home/utnso/tp-2026-1c-La-Serpiente-Shnissugah/kernel_scheduler/src`. Hay que cambiar esto al deployar en otra máquina.

---

### CPU (`/cpu`)

Unidad de ejecución. Implementa el ciclo fetch-decode-execute.

**Registros disponibles:**
- 8-bit: `AX`, `BX`, `CX`, `DX`
- 32-bit: `EAX`, `EBX`, `ECX`, `EDX`, `SI`, `DI`
- `PC` (Program Counter)

**Instrucciones implementadas:**
| Instrucción | Estado | Descripción |
|-------------|--------|-------------|
| `NOOP` | OK | No hace nada |
| `SET reg valor` | OK | Asigna valor al registro |
| `SUM reg1 reg2/val` | OK | Suma |
| `SUB reg1 reg2/val` | OK | Resta |
| `JNZ reg etiqueta` | OK | Salto condicional (si reg != 0) |
| `EXIT` | OK | Termina el proceso |
| `SLEEP ms` | OK | Bloquea proceso por tiempo |
| `STDIN` | OK | Simula lectura de teclado |
| `STDOUT` | OK | Simula salida por consola |
| `INIT_PROC` | OK | Crea proceso hijo |
| `MEM_ALLOC` | OK | Solicita memoria al kernel |
| `MEM_FREE` | OK | Libera memoria |
| `MUTEX_CREATE/LOCK/UNLOCK/DESTROY` | OK | Sincronización |
| `MOV_IN` | **PENDIENTE** | Requiere MMU — no implementado |
| `MOV_OUT` | **PENDIENTE** | Requiere MMU — no implementado |
| `COPY_MEM` | **PENDIENTE** | Requiere MMU — no implementado |

> **DEBUG:** `check_interrupt()` siempre retorna `false` — la preempción por quantum no está activa todavía.

**Configuración (`cpu.conf`):**
```
LOG_LEVEL=INFO
SCHEDULER_IP=127.0.0.1
SCHEDULER_PORT=8001
MEMORY_IP=127.0.0.1
MEMORY_PORT=8000
```

---

### Memory Stick (`/memory_stick`)

Almacenamiento secundario accesible desde los CPUs.

**Flujo:** al iniciar, envía un `HANDSHAKE_MEMORY_STICK` a Kernel Memory con su tamaño, IP y puerto. Kernel Memory notifica a todos los CPUs conectados. Los CPUs pueden luego conectarse directamente.

> **MOCK:** El registro y handshake funcionan, pero las operaciones de lectura/escritura de bloques no están implementadas.

**Configuración (`memory_stick.conf`):**
```
LOG_LEVEL=INFO
MEMORY_IP=127.0.0.1
MEMORY_PORT=8000
MEMORY_SIZE=4096
STICK_IP=127.0.0.1
STICK_PORT=9000
```

---

### Swap (`/swap`)

Almacenamiento de paginación en disco.

**Responsabilidades:**
- Gestiona un archivo de swap en disco (`SWAP_FILE_PATH`)
- Implementa operaciones de lectura/escritura por bloque
- Se conecta a Kernel Memory al iniciar

> **PENDIENTE:** El módulo Swap se conecta y registra correctamente, pero no está integrado al flujo de CPU/MMU porque la MMU no está implementada.

**Configuración (`swap.conf`):**
```
LOG_LEVEL=INFO
MEMORY_IP=127.0.0.1
MEMORY_PORT=8000
SWAP_FILE_PATH=/tmp/swap.bin
SWAP_FILE_SIZE=4194304          # 4 MB
BLOCK_SIZE=4096
```

---

### I/O (`/io`)

Manejador de operaciones de entrada/salida.

**Operaciones:**
- `IO_SLEEP ms` — bloquea el proceso durante `ms` milisegundos (usa `usleep`)
- `IO_STDIN` — simula lectura de teclado (lee de stdin del módulo)
- `IO_STDOUT` — simula salida por consola (loguea el valor)

Al finalizar, notifica al Scheduler con `FIN_IO` para que el proceso vuelva a READY.

Se pueden levantar múltiples instancias, cada una con un nombre distinto (e.g., `"SLEEP"`, `"KEYBOARD"`).

**Configuración (`io.conf`):**
```
LOG_LEVEL=INFO
SCHEDULER_IP=127.0.0.1
SCHEDULER_PORT=8001
```

---

### Utils (`/utils`)

Biblioteca compartida, enlazada estáticamente por todos los módulos.

**Componentes:**
- **`serializacion.c`** — protocolo binario: `[opcode][size][payload]`
- **`mensajes.c`** — API de alto nivel para enviar/recibir mensajes
- **`sockets.c`** — helpers TCP: crear servidor, conectar cliente, aceptar conexiones
- **`utils.h`** — opcodes del protocolo, structs compartidos (`t_registros`, `t_contexto`, `t_segmento`)

---

## Formato de scripts de proceso

Los procesos se definen como archivos de texto con una instrucción por línea:

```
# archivo: {SCRIPTS_BASEPATH}/{pid}.script
SET AX 5
SUM AX 3
JNZ AX -1
EXIT
```

El CPU usa el `PC` (program counter) como número de línea (0-indexed) para hacer fetch de instrucciones a Kernel Memory.

---

## Protocolo de comunicación

Todos los mensajes siguen el formato:
```
[ opcode : enum (4 bytes) ][ size : size_t ][ payload : variable ]
```

**Mensajes principales:**
| Emisor → Receptor | Opcode | Payload |
|-------------------|--------|---------|
| Scheduler → Memory | `CREAR_PROCESO` | PID, ruta del script |
| Scheduler → Memory | `OBTENER_ESPACIO_LIBRE` | — |
| Scheduler → CPU | `EJECUTAR_PROCESO` | PID |
| CPU → Memory | `OBTENER_INSTRUCCION` | PID, PC |
| CPU → Memory | `OBTENER_CONTEXTO` | PID |
| CPU → Memory | `ACTUALIZAR_CONTEXTO` | Contexto completo (registros + segmentos) |
| CPU → Memory | `LECTURA_MEMORIA` | PID, dirección, tamaño |
| CPU → Memory | `ESCRITURA_MEMORIA` | PID, dirección, tamaño, datos |
| Memory → CPU | `NUEVA_MEMORIA_DISPONIBLE` | Tamaño, IP, puerto (Memory Stick) |
| Memory → Scheduler | `MEMORIA_CORRUPTA_BSOD` | — |
| I/O → Scheduler | `FIN_IO` | PID |

---

## Estado de implementación

| Funcionalidad | Estado |
|---------------|--------|
| Protocolo TCP y serialización | Completo |
| Handshakes entre módulos | Completo |
| Creación de procesos (PCB) | Completo |
| Planificador largo plazo (admisión) | Completo |
| Dispatch de CPU | Funcional |
| Ciclo fetch-decode-execute | Completo (instrucciones básicas) |
| Instrucciones aritméticas y de control | Completo |
| Syscalls (SLEEP, STDIN, STDOUT, EXIT) | Completo |
| Syscalls de memoria (MEM_ALLOC, MEM_FREE) | Parcial (sin MMU) |
| I/O bloqueante y desbloqueo | Completo |
| BSOD (detección de corrupción) | Completo |
| Gestión real de memoria (segmentación) | **MOCK** |
| MMU (traducción virtual → físico) | **NO implementado** |
| MOV_IN / MOV_OUT / COPY_MEM | **NO implementado** |
| Persistencia de contexto en Memory | **MOCK** |
| Memory Stick (almacenamiento real) | **MOCK** |
| Swap integrado al flujo de CPU | **PENDIENTE** |
| Planificador corto plazo (preempción) | **PENDIENTE** |
| Quantum enforcement (RR) | **PENDIENTE** |
| INSTRUCTION_DELAY / COMPACTION_DELAY | Configurados, sin efecto |

---

## Cosas hardcodeadas / de debug

- **Rutas de config** en los `main.c` de cada módulo tienen la ruta absoluta hardcodeada como valor por defecto si no se pasa argumento por CLI (apuntan a `/home/utnso/...`).

- **`SCRIPTS_BASEPATH`** en `kernel_memory.conf` apunta a la máquina de desarrollo. Cambiarlo al deployar.

- **Script de prueba** en `kernel_scheduler/src/procesoTest.txt` — proceso hardcodeado para testing inicial.

- **Planificador corto plazo en largo plazo** (`planificadorLargoPlazo.c`): el despacho al CPU está temporalmente en el planificador de largo plazo, con el comentario `// esto iria en el planificador corto plazo, pero lo dejo aca para debug`.

- **`check_interrupt()`** siempre retorna `false` — stub para preempción (en `cpu_ciclo.c`).

- **Límites mágicos:** `MAX_MEMORY_STICKS = 16` (utils.h), `MAX_CPUS = 10` (memory_conexiones.h).

- **Retry en CPU** (`utils_cpu.c`): espera 2000ms entre reintentos de conexión, loguea cada 10 intentos fallidos.

- **Polling BSOD** (`planificadorLargoPlazo.c`): detecta BSOD con polling cada 100ms.

---

## Compilación

Cada módulo tiene su propio `Makefile`:

```bash
cd kernel_scheduler && make         # build release
cd kernel_scheduler && make debug   # build con -g y -DDEBUG

# Lo mismo para: kernel_memory, cpu, memory_stick, swap, io, utils
```

Los binarios quedan en `bin/`. Todos los módulos linkean contra `-lutils -lcommons -lpthread`.

## Deploy

```bash
git clone https://github.com/sisoputnfrba/so-deploy.git
cd so-deploy
./deploy.sh -r=release -p=utils -p=kernel_scheduler -p=kernel_memory -p=cpu -p=memory_stick -p=swap -p=io "tp-2026-1c-La-Serpiente-Shnissugah"
```

## Guías útiles

- [Cómo interpretar errores de compilación](https://docs.utnso.com.ar/primeros-pasos/primer-proyecto-c#errores-de-compilacion)
- [Cómo utilizar el debugger](https://docs.utnso.com.ar/guias/herramientas/debugger)
- [Guía de despliegue de TP](https://docs.utnso.com.ar/guias/herramientas/deploy)

[so-commons-library]: https://github.com/sisoputnfrba/so-commons-library
[so-deploy]: https://github.com/sisoputnfrba/so-deploy
