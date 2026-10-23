# Sincronización de Procesos: Simulación de un Estacionamiento

Trabajo del curso de Sistemas Operativos. Se simula un estacionamiento con
**espacios limitados** y **una sola barrera** de entrada/salida. Cada auto es
un **proceso real** (creado con `fork()`) y todos compiten por los mismos
recursos. Se usan **semáforos POSIX** en **memoria compartida** para que dos
procesos nunca usen el mismo recurso al mismo tiempo.

Se incluyen dos programas:

| Programa | Qué demuestra |
|---|---|
| `sin_sincronizacion` | El problema: sin control, entran más autos que espacios, dos autos ocupan el mismo espacio y el contador queda mal (condición de carrera). |
| `estacionamiento` | La solución: con semáforos nunca se supera la capacidad, cada espacio lo usa un solo auto y el contador siempre es correcto. |

---

## 1. Escenario

- El estacionamiento tiene **M espacios** (por defecto 3).
- Llegan **N autos** (por defecto 10) en momentos distintos.
- La barrera de entrada/salida es **una sola**: solo un auto puede cruzarla a la vez.
- Si el estacionamiento está lleno, el auto **espera** (bloqueado) hasta que otro salga.
- Cada auto se queda estacionado un tiempo aleatorio (1 a 3 s) y luego se va.

## 2. Procesos que intervienen

```
                 ┌───────────────────────────────┐
                 │  PADRE (administrador)        │
                 │  - crea memoria compartida    │
                 │  - inicializa los semáforos   │
                 │  - lanza N hijos con fork()   │
                 │  - espera a todos con wait()  │
                 │  - imprime el resumen         │
                 └──────────────┬────────────────┘
          fork()     fork()     │     fork()          fork()
     ┌──────────┬──────────┬────┴─────┬──────────┐
     ▼          ▼          ▼          ▼          ▼
  Auto 1     Auto 2     Auto 3      ...       Auto N      (procesos hijo)
```

- **Padre**: no conduce ningún auto; solo prepara los recursos, crea los hijos,
  los espera (evitando procesos zombi) y verifica el resultado.
- **Hijos (autos)**: cada uno ejecuta la función `auto_proceso(id)` y termina.

Cada línea de la salida muestra el **PID** del proceso, así se comprueba que son
procesos distintos.

## 3. Recursos compartidos y sincronización

Los procesos no comparten memoria por defecto (después de `fork()` cada uno tiene
su copia). Por eso se crea una región con:

```c
mmap(NULL, tamaño, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
```

`MAP_SHARED` hace que los hijos vean **la misma** memoria que el padre. Ahí vive
la estructura `Estacionamiento`:

| Campo | Tipo | Valor inicial | Para qué sirve |
|---|---|---|---|
| `espacios` | semáforo **contador** | M | Cuenta los espacios libres. Si llega a 0, el auto que hace `sem_wait` se bloquea. |
| `barrera` | semáforo **binario** | 1 | Exclusión mutua: un solo auto cruza la barrera a la vez. |
| `mutex_estado` | semáforo **binario** | 1 | Protege los datos compartidos (`plaza[]`, `ocupados`, `max_ocupados`, `atendidos`). |
| `plaza[]` | arreglo de enteros | 0 | Qué auto está en cada espacio (0 = libre). |
| `ocupados` | entero | 0 | Cuántos autos hay dentro. |

Los semáforos se crean con `sem_init(&sem, 1, valor)`. El `1` (pshared) indica
que el semáforo se comparte **entre procesos** y no solo entre hilos.

Operaciones usadas:

- `sem_wait(&s)` (operación **P** / *wait*): si el valor es > 0 lo decrementa y
  sigue; si es 0, el proceso se **bloquea** hasta que alguien haga `sem_post`.
- `sem_post(&s)` (operación **V** / *signal*): incrementa el valor y despierta a
  un proceso bloqueado, si lo hay.
- `sem_trywait(&s)`: como `sem_wait` pero sin bloquear; se usa solo para poder
  imprimir "ESPERA" cuando el estacionamiento está lleno.

## 4. Flujo de cada auto (versión con semáforos)

```
   LLEGA
     │
     ▼
 sem_wait(espacios) ──── ¿hay espacio? ── no ──► queda BLOQUEADO (ESPERA)
     │ sí                                             │ (otro auto hace sem_post)
     ◄────────────────────────────────────────────────┘
     ▼
 sem_wait(barrera)          ┐
   cruza barrera ENTRADA    │
   sem_wait(mutex_estado)   │  SECCIÓN CRÍTICA
     busca plaza libre      │  (un solo auto a la vez)
     plaza[i] = id          │
     ocupados++             │
   sem_post(mutex_estado)   │
 sem_post(barrera)          ┘
     │
     ▼
 ESTACIONADO (1-3 s)        ← fuera de toda sección crítica, varios autos a la vez
     │
     ▼
 sem_wait(barrera)          ┐
   sem_wait(mutex_estado)   │
     plaza[i] = 0           │  SECCIÓN CRÍTICA
     ocupados--             │
   sem_post(mutex_estado)   │
   cruza barrera SALIDA     │
 sem_post(barrera)          ┘
     │
     ▼
 sem_post(espacios)         ← libera el espacio y despierta a un auto que espera
     │
     ▼
   TERMINA (exit)
```

**Orden fijo de los semáforos** (`espacios` → `barrera` → `mutex_estado`) en
todos los procesos: esto evita el **interbloqueo (deadlock)**.

## 5. Compilación y ejecución

Requisitos: Linux (o WSL), `gcc` y `make`.

```bash
make                 # compila los dos programas
make run-sin         # ejecuta la versión SIN sincronización (muestra el error)
make run             # ejecuta la versión CON semáforos (todo correcto)
make comparar        # ejecuta las dos seguidas
make clean           # borra los ejecutables
```

Con parámetros propios:

```bash
./estacionamiento [autos] [espacios]       # autos: 1-100, espacios: 1-20
./estacionamiento 20 5
./sin_sincronizacion 10 3
```

## 6. Cómo leer la salida

### Versión con semáforos

```
[t= 0.98s] Auto  3 ESTACIONA en espacio 3   ocupados 3/3  [A1 ][A2 ][A3 ]
[t= 1.36s] Auto  5 (PID 22207) LLEGA
[t= 1.36s] Auto  5 ESPERA: estacionamiento lleno
[t= 1.69s] Auto  2 SALE del espacio 2       ocupados 2/3  [A1 ][ - ][A3 ]
[t= 1.89s] Auto  4 cruza la barrera de ENTRADA
...
=== Resumen ===
Autos atendidos         : 10 de 10
Maximo dentro a la vez  : 3 (capacidad 3)
Ocupados al final       : 0
Resultado               : OK, nunca se supero la capacidad ni se compartio un espacio
```

- `[t=...]` tiempo desde el inicio.
- `[A1 ][ - ][A3 ]` dibujo del estacionamiento: qué auto está en cada espacio.
- Cuando está lleno (3/3), los autos que llegan **esperan**; en cuanto uno sale,
  entra el siguiente.
- Nunca aparecen dos autos en el mismo espacio ni más de 3 dentro.

### Versión sin sincronización

```
[t= 0.46s] Auto  8 ESTACIONA en espacio 3   contador=3  autos dentro=3/3
[t= 0.55s] Auto  5 CHOQUE: el espacio 1 ya lo ocupa el auto 1
[t= 0.56s] Auto  5 ESTACIONA en espacio 1   contador=4  autos dentro=4/3
...
=== Resumen ===
Maximo dentro a la vez  : 5 (capacidad 3)
Choques por un espacio  : 6
Contador al final       : -3 (deberia ser 0)
ERROR: se supero la capacidad del estacionamiento
ERROR: dos autos usaron el mismo espacio
ERROR: el contador quedo inconsistente (actualizaciones perdidas)
```

Los tres errores son consecuencia de la **condición de carrera**: varios procesos
leen y modifican los mismos datos al mismo tiempo sin coordinación. Como depende
del momento exacto en que corre cada proceso, los números cambian en cada
ejecución (si en alguna no se nota el error, basta con ejecutar otra vez).

> En `sin_sincronizacion.c` hay contadores atómicos (`dentro_real`, `max_real`,
> `choques`). Son solo un "medidor" para poder demostrar el error; no forman parte
> de la lógica del estacionamiento.

## 7. Estructura del proyecto

```
sincronizacionProcesos/
├── Makefile
├── README.md                 ← este documento
├── estacionamiento.c         ← solución con semáforos
├── sin_sincronizacion.c      ← versión con condición de carrera
└── docs/
    └── GUIA_EXPOSICION.md    ← teoría, guion de la demo y preguntas
```
