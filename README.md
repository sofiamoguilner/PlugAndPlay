# Plug & Pray — Distributed Operating System Simulator

*A university project simulating a distributed operating system in C: process scheduling, memory management, hot-pluggable memory devices, I/O, and swap, all communicating over the network as independent processes.*

[🇬🇧 English](#english) | [🇦🇷 Español](#español)

---

## English

### Overview

**Plug & Pray** is a simulated distributed operating system built from scratch in C for the Operating Systems course. Instead of a single-process simulation, the system is split into independent modules — each a real OS process, compiled and run on its own, potentially on a different machine or VM — that coordinate over the network to schedule simulated processes, manage memory, and handle I/O and swapping.

The name captures one of the project's core challenges: **Memory Stick modules (RAM) can be hot-plugged and hot-unplugged at runtime**, so the system has to keep working — reassigning, suspending, and recovering processes — even as the memory devices underneath it come and go.

### Architecture

| Module | Responsibility |
|---|---|
| **Kernel Scheduler** | The process scheduler. Boots the initial process (PID 0), runs a multithreaded server, and manages a 7-state process lifecycle across long-, medium-, and short-term scheduling. Detects memory corruption and can trigger a full system halt (BSOD). |
| **Kernel Memory** | The memory manager. Runs a multithreaded server that serves instruction/context requests from CPUs, coordinates memory allocation across all connected Memory Sticks, and manages SWAP. |
| **CPU** | Simulates a simplified real CPU instruction cycle (Fetch → Decode → Execute → Check Interrupt), executing instructions fetched from Kernel Memory. Each CPU instance runs independently with its own config and log. |
| **Memory Stick** | Simulates a RAM chip: a fixed-size memory space (allocated with `malloc`) that serves read/write requests from CPUs and Kernel Memory. Can be connected and disconnected while the system is running. |
| **IO** | Simulates I/O devices — STDIN, STDOUT, and SLEEP interfaces — attending requests dispatched by the Kernel Scheduler. |
| **SWAP** | Persists the memory of suspended processes to a block-based file on disk, freeing RAM until those processes are resumed. |

Each module is a standalone C program with its own Makefile, configuration file, and required structured logging (via the course's `so-commons-library`), designed so behavior can be tuned per test without recompiling.

### Key concepts applied

- Long-, medium-, and short-term process scheduling across a 7-state process model
- Multithreaded servers and custom network protocols for inter-process communication
- Dynamic memory management across pluggable/removable memory devices
- Simulated CPU instruction cycle (Fetch–Decode–Execute–Interrupt)
- Process suspension and swapping to disk
- Simulated I/O devices (STDIN, STDOUT, SLEEP)
- Configuration-driven, fully logged, testable distributed system design
- Iterative-incremental development across modules

### Tech stack

- **Language:** C
- **Build system:** Makefiles
- **Environment:** Linux, deployed and tested across multiple virtual machines to validate real distributed, multi-process behavior

### Notes

This project was developed as the group practical assignment for the Operating Systems course at university, evaluated through automated tests plus an individual oral defense (coloquio) connecting the implementation back to the theoretical concepts of the course.

---

## Español

### Descripción general

**Plug & Pray** es un sistema operativo distribuido simulado, desarrollado desde cero en C para la materia Sistemas Operativos. En lugar de una simulación de un solo proceso, el sistema se divide en módulos independientes — cada uno un proceso real del sistema operativo, compilado y ejecutado por su cuenta, potencialmente en una máquina o VM distinta — que se coordinan por red para planificar los procesos simulados, administrar memoria y manejar I/O y swap.

El nombre del proyecto refleja uno de sus desafíos centrales: **los módulos Memory Stick (RAM) pueden conectarse y desconectarse en caliente durante la ejecución**, por lo que el sistema tiene que seguir funcionando — reasignando, suspendiendo y recuperando procesos — incluso mientras los dispositivos de memoria subyacentes van y vienen.

### Arquitectura

| Módulo | Responsabilidad |
|---|---|
| **Kernel Scheduler** | El planificador de procesos. Inicializa el proceso inicial (PID 0), levanta un servidor multihilo y gestiona un modelo de 7 estados de procesos a través de planificación de largo, mediano y corto plazo. Detecta corrupción de memoria y puede disparar un apagado total del sistema (BSOD). |
| **Kernel Memory** | El administrador de memoria. Levanta un servidor multihilo que atiende peticiones de instrucciones/contextos de las CPUs, coordina la asignación de memoria entre todos los Memory Sticks conectados y gestiona el SWAP. |
| **CPU** | Simula un ciclo de instrucción de CPU real simplificado (Fetch → Decode → Execute → Check Interrupt), ejecutando instrucciones obtenidas del Kernel Memory. Cada instancia de CPU corre de forma independiente con su propia configuración y log. |
| **Memory Stick** | Simula un chip de RAM: un espacio de memoria de tamaño fijo (reservado con `malloc`) que atiende peticiones de lectura/escritura de las CPUs y del Kernel Memory. Puede conectarse y desconectarse con el sistema en ejecución. |
| **IO** | Simula dispositivos de entrada/salida — interfaces STDIN, STDOUT y SLEEP — atendiendo las peticiones despachadas por el Kernel Scheduler. |
| **SWAP** | Persiste en disco la memoria de los procesos suspendidos, en un archivo dividido en bloques, liberando RAM hasta que esos procesos se reanuden. |

Cada módulo es un programa en C independiente, con su propio Makefile, archivo de configuración y logging estructurado obligatorio (usando la `so-commons-library` provista por la cátedra), diseñado para poder ajustar el comportamiento en cada prueba sin recompilar.

### Conceptos clave aplicados

- Planificación de procesos de largo, mediano y corto plazo sobre un modelo de 7 estados
- Servidores multihilo y protocolos de red propios para comunicación entre procesos
- Administración dinámica de memoria sobre dispositivos de memoria conectables/desconectables
- Ciclo de instrucción de CPU simulado (Fetch–Decode–Execute–Interrupt)
- Suspensión de procesos y swap a disco
- Dispositivos de I/O simulados (STDIN, STDOUT, SLEEP)
- Diseño de sistema distribuido configurable, con logging y testeable
- Desarrollo iterativo-incremental por módulos

### Stack técnico

- **Lenguaje:** C
- **Sistema de build:** Makefiles
- **Entorno:** Linux, desplegado y probado en múltiples máquinas virtuales para validar el comportamiento distribuido real, multi-proceso

### Notas

Este proyecto fue desarrollado como trabajo práctico grupal de la materia Sistemas Operativos, evaluado mediante pruebas automatizadas y un coloquio individual que relaciona la implementación con los conceptos teóricos de la materia.
