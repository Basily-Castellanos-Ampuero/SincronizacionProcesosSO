/*
 * Sincronizacion de procesos: estacionamiento de un centro comercial.
 *
 * El estacionamiento tiene varias zonas (a nivel, subterraneas y
 * superiores, ver zonas.h) y varias barreras de entrada y de salida.
 * Los conductores siempre prefieren la zona de acceso mas facil, asi que
 * las zonas a nivel se llenan primero y las mas incomodas al final.
 *
 * Procesos:
 *   - Padre: representa la calle. Cuando llega cada auto crea su proceso
 *     con fork(), dibuja el panel y al final recoge a todos con waitpid().
 *   - Hijos: un proceso por auto (2000 por defecto).
 *
 * Recursos compartidos (en memoria compartida entre procesos):
 *   espacios     : semaforo CONTADOR, lugares libres en todo el centro comercial
 *   zona[z]      : semaforo CONTADOR por zona, lugares libres en esa zona
 *   entrada      : semaforo CONTADOR, barreras de entrada libres
 *   salida       : semaforo CONTADOR, barreras de salida libres
 *   mutex_estado : semaforo BINARIO, protege los espacios, los contadores,
 *                  los carriles y el estado de cada auto
 *
 * Uso: ./estacionamiento [autos]   (por defecto 2000)
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "zonas.h"
#include "panel.h"
#include "tiempos.h"

typedef struct {
    sem_t espacios;
    sem_t zona[NUM_ZONAS];
    sem_t entrada;
    sem_t salida;
    sem_t mutex_estado;

    int num_autos;
    int espacio[MAX_ESPACIOS];                /* auto en cada espacio, 0 = libre */
    int ocupados;
    int ocupados_zona[NUM_ZONAS];
    int carril_entrada[CARRILES_ENTRADA];     /* auto en cada barrera, 0 = libre */
    int carril_salida[CARRILES_SALIDA];
    int estado[MAX_AUTOS + 1];                /* que esta haciendo cada auto */
    int espacio_de[MAX_AUTOS + 1];            /* espacio de cada auto, -1 = ninguno */

    /* Datos para el resumen final */
    int atendidos;
    int max_zona[NUM_ZONAS];
    double primer_auto[NUM_ZONAS];            /* cuando se uso por primera vez */
    double lleno_desde[NUM_ZONAS];            /* cuando se lleno por primera vez */

    Registro registro;                        /* eventos que muestra el panel */
} CentroComercial;

static CentroComercial *cc;                   /* apunta a la memoria compartida */

static void cambiar_estado(int id, int nuevo)
{
    sem_wait(&cc->mutex_estado);
    cc->estado[id] = nuevo;
    sem_post(&cc->mutex_estado);
}

/* Ocupa una barrera libre. El semaforo (entrada o salida) ya garantiza
 * que hay una; aqui solo se anota cual, para dibujarla. */
static int tomar_carril(int *carril, int n, int id, int nuevo_estado)
{
    sem_wait(&cc->mutex_estado);
    int i = 0;
    while (i < n - 1 && carril[i] != 0)
        i++;
    carril[i] = id;
    cc->estado[id] = nuevo_estado;
    sem_post(&cc->mutex_estado);
    return i;
}

static void soltar_carril(int *carril, int i)
{
    sem_wait(&cc->mutex_estado);
    carril[i] = 0;
    sem_post(&cc->mutex_estado);
}

/* La zona de acceso mas facil que tenga lugar. El semaforo "espacios" ya
 * garantiza que hay lugar en ALGUNA zona; se prueban en orden de
 * preferencia con sem_trywait (no bloquea). Si otro auto gano el ultimo
 * lugar justo mientras se revisaba, se vuelve a revisar. */
static int elegir_zona(void)
{
    for (;;) {
        for (int z = 0; z < NUM_ZONAS; z++)
            if (sem_trywait(&cc->zona[z]) == 0)
                return z;
        sched_yield();
    }
}

/* Codigo que ejecuta cada proceso hijo */
static void auto_proceso(int id, double llegada)
{
    Registro *r = &cc->registro;
    char letra = LETRA(id);
    srand(getpid() ^ time(NULL));

    /* Esperar la hora exacta de llegada de este auto */
    double falta = llegada - registro_tiempo(r);
    if (falta > 0)
        usleep((useconds_t)(falta * 1e6));

    registrar(r, EV_LLEGA, "Auto %d [%c] llega (PID %d)", id, letra, getpid());

    /* 1. Reservar un lugar en el centro comercial. Si el contador esta en 0
     *    el proceso se bloquea aqui (sin consumir CPU) hasta que salga otro. */
    if (sem_trywait(&cc->espacios) == -1) {
        cambiar_estado(id, ESPERA_ESPACIO);
        registrar(r, EV_ESPERA, "Auto %d [%c] espera: centro comercial lleno", id, letra);
        sem_wait(&cc->espacios);
    }

    /* 2. Cruzar una barrera de entrada: solo CARRILES_ENTRADA autos a la vez */
    cambiar_estado(id, ESPERA_ENTRADA);
    sem_wait(&cc->entrada);
    int carril = tomar_carril(cc->carril_entrada, CARRILES_ENTRADA, id, ENTRANDO);
    usleep(variar(T_BARRERA));
    soltar_carril(cc->carril_entrada, carril);
    sem_post(&cc->entrada);

    /* 3. Elegir la zona mas comoda con lugar libre */
    int z = elegir_zona();
    int inicio = zona_inicio(z);

    /* 4. Tomar un espacio concreto de la zona (seccion critica) */
    sem_wait(&cc->mutex_estado);
    int mi_espacio = -1;
    for (int e = inicio; e < inicio + ZONAS[z].capacidad; e++) {
        if (cc->espacio[e] == 0) {
            mi_espacio = e;
            break;
        }
    }
    if (mi_espacio == -1) {               /* imposible gracias al semaforo de la zona */
        sem_post(&cc->mutex_estado);
        registrar(r, EV_ERROR, "Auto %d ERROR: no hay lugar en %s", id, ZONAS[z].nombre);
        exit(EXIT_FAILURE);
    }
    cc->espacio[mi_espacio] = id;
    cc->espacio_de[id] = mi_espacio;
    cc->estado[id] = BUSCANDO;
    cc->ocupados++;
    cc->ocupados_zona[z]++;
    if (cc->ocupados_zona[z] > cc->max_zona[z])
        cc->max_zona[z] = cc->ocupados_zona[z];
    if (cc->primer_auto[z] < 0)
        cc->primer_auto[z] = registro_tiempo(r);
    if (cc->ocupados_zona[z] == ZONAS[z].capacidad && cc->lleno_desde[z] < 0)
        cc->lleno_desde[z] = registro_tiempo(r);
    sem_post(&cc->mutex_estado);
    registrar(r, EV_ENTRA, "Auto %d [%c] va a %s, lugar %d",
              id, letra, ZONAS[z].nombre, mi_espacio - inicio + 1);

    /* 5. Manejar hasta la zona y estacionar */
    usleep(variar(ZONAS[z].recorrido + T_MANIOBRA));
    cambiar_estado(id, ESTACIONADO);

    /* 6. De compras (fuera de toda seccion critica) */
    usleep(entre(T_ESTADIA_MIN, T_ESTADIA_MAX));

    /* 7. Dejar el lugar */
    sem_wait(&cc->mutex_estado);
    cc->espacio[mi_espacio] = 0;
    cc->espacio_de[id] = -1;
    cc->estado[id] = YENDO_SALIDA;
    cc->ocupados--;
    cc->ocupados_zona[z]--;
    sem_post(&cc->mutex_estado);
    sem_post(&cc->zona[z]);               /* primero la zona ...                  */
    sem_post(&cc->espacios);              /* ... despues el total: despierta a uno */
    registrar(r, EV_SALE, "Auto %d [%c] deja %s", id, letra, ZONAS[z].nombre);

    /* 8. Manejar hasta la salida y cruzar una barrera de salida */
    usleep(variar(ZONAS[z].recorrido));
    cambiar_estado(id, ESPERA_SALIDA);
    sem_wait(&cc->salida);
    carril = tomar_carril(cc->carril_salida, CARRILES_SALIDA, id, SALIENDO);
    usleep(variar(T_BARRERA));
    sem_wait(&cc->mutex_estado);
    cc->carril_salida[carril] = 0;
    cc->estado[id] = SE_FUE;
    cc->atendidos++;
    sem_post(&cc->mutex_estado);
    sem_post(&cc->salida);
}

/* El padre copia el estado dentro de mutex_estado para dibujar una
 * foto coherente (nunca a mitad de un cambio). */
static void tomar_foto(Foto *f)
{
    f->autos = cc->num_autos;
    f->con_semaforos = 1;
    sem_getvalue(&cc->espacios, &f->sem_espacios);
    sem_getvalue(&cc->entrada, &f->sem_entrada);
    sem_getvalue(&cc->salida, &f->sem_salida);
    for (int z = 0; z < NUM_ZONAS; z++)
        sem_getvalue(&cc->zona[z], &f->sem_zona[z]);

    sem_wait(&cc->mutex_estado);
    for (int id = 1; id <= cc->num_autos; id++) {
        f->estado[id] = cc->estado[id];
        f->espacio_de[id] = cc->espacio_de[id];
    }
    for (int i = 0; i < CARRILES_ENTRADA; i++)
        f->carril_entrada[i] = cc->carril_entrada[i];
    for (int i = 0; i < CARRILES_SALIDA; i++)
        f->carril_salida[i] = cc->carril_salida[i];
    f->contador_total = cc->ocupados;
    for (int z = 0; z < NUM_ZONAS; z++)
        f->contador_zona[z] = cc->ocupados_zona[z];
    sem_post(&cc->mutex_estado);
}

static int comparar_tiempos(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void al_interrumpir(int sig)
{
    (void)sig;
    panel_terminar();
    _exit(130);
}

int main(int argc, char *argv[])
{
    int autos = (argc > 1) ? atoi(argv[1]) : 2000;
    int total = total_espacios();

    if (autos < 1 || autos > MAX_AUTOS) {
        fprintf(stderr, "Uso: %s [autos 1-%d]\n", argv[0], MAX_AUTOS);
        return EXIT_FAILURE;
    }
    if (total > MAX_ESPACIOS) {
        fprintf(stderr, "Las zonas suman %d espacios; el maximo es %d\n", total, MAX_ESPACIOS);
        return EXIT_FAILURE;
    }

    /* Memoria compartida: la heredan los hijos despues de fork() */
    cc = mmap(NULL, sizeof *cc, PROT_READ | PROT_WRITE,
              MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (cc == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }

    /* pshared = 1: los semaforos se comparten entre procesos */
    int fallo = sem_init(&cc->espacios, 1, total) == -1 ||
                sem_init(&cc->entrada, 1, CARRILES_ENTRADA) == -1 ||
                sem_init(&cc->salida, 1, CARRILES_SALIDA) == -1 ||
                sem_init(&cc->mutex_estado, 1, 1) == -1;
    for (int z = 0; z < NUM_ZONAS && !fallo; z++)
        fallo = sem_init(&cc->zona[z], 1, ZONAS[z].capacidad) == -1;
    if (fallo) {
        perror("sem_init");
        return EXIT_FAILURE;
    }

    cc->num_autos = autos;
    for (int id = 1; id <= autos; id++)
        cc->espacio_de[id] = -1;
    for (int z = 0; z < NUM_ZONAS; z++)
        cc->primer_auto[z] = cc->lleno_desde[z] = -1;

    /* Hora de llegada de cada auto, al azar dentro de T_LLEGADAS */
    double *llegada = malloc(sizeof(double) * autos);
    if (llegada == NULL) {
        perror("malloc");
        return EXIT_FAILURE;
    }
    srand(time(NULL));
    for (int i = 0; i < autos; i++)
        llegada[i] = rand() / (RAND_MAX + 1.0) * T_LLEGADAS / ESCALA;
    qsort(llegada, autos, sizeof(double), comparar_tiempos);

    registro_iniciar(&cc->registro);
    signal(SIGINT, al_interrumpir);
    int terminal = panel_iniciar("CENTRO COMERCIAL · CON SEMÁFOROS", 0);

    /* PADRE: crea cada auto cuando llega, dibuja el panel y recoge a los
     * que terminan */
    static Foto foto;
    int creados = 0, vivos = 0, fallidos = 0;
    for (;;) {
        double t = registro_tiempo(&cc->registro);
        while (creados < autos && llegada[creados] <= t + ANTICIPO) {
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");                     /* se reintenta en la siguiente vuelta */
                break;
            }
            if (pid == 0) {                          /* HIJO */
                signal(SIGINT, SIG_DFL);
                auto_proceso(creados + 1, llegada[creados]);
                exit(EXIT_SUCCESS);
            }
            creados++;
            vivos++;
        }

        int estado;
        while (waitpid(-1, &estado, WNOHANG) > 0) {
            vivos--;
            if (!WIFEXITED(estado) || WEXITSTATUS(estado) != 0)
                fallidos++;
        }

        if (terminal) {
            tomar_foto(&foto);
            panel_dibujar(&foto, &cc->registro);
        } else {
            panel_volcar(&cc->registro);
        }
        if (creados == autos && vivos == 0)
            break;
        usleep(50000);
    }
    panel_terminar();

    int zonas_ok = 1, contadores_ok = cc->ocupados == 0;
    for (int z = 0; z < NUM_ZONAS; z++) {
        if (cc->max_zona[z] > ZONAS[z].capacidad)
            zonas_ok = 0;
        if (cc->ocupados_zona[z] != 0)
            contadores_ok = 0;
    }
    int ok = cc->atendidos == creados && zonas_ok && contadores_ok && fallidos == 0;

    panel_resumen();
    panel_tabla_zonas(cc->max_zona, cc->primer_auto, cc->lleno_desde);
    panel_resultado(cc->atendidos == creados, "Autos atendidos: %d de %d", cc->atendidos, creados);
    panel_resultado(zonas_ok, "Ninguna zona superó su capacidad");
    panel_resultado(contadores_ok, "Contadores al final: ocupados = %d", cc->ocupados);
    panel_resultado(ok, ok ? "Nunca se superó la capacidad ni se compartió un espacio"
                           : "Error en la simulación");

    sem_destroy(&cc->espacios);
    sem_destroy(&cc->entrada);
    sem_destroy(&cc->salida);
    sem_destroy(&cc->mutex_estado);
    for (int z = 0; z < NUM_ZONAS; z++)
        sem_destroy(&cc->zona[z]);
    munmap(cc, sizeof *cc);
    free(llegada);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
