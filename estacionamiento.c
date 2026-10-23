/*
 * Sincronizacion de procesos: simulacion de un estacionamiento.
 *
 * El padre (administrador) crea un estacionamiento con M espacios y lanza
 * N procesos hijo (autos) con fork(). Cada auto:
 *   llega -> espera un espacio libre -> cruza la barrera de entrada ->
 *   se estaciona un tiempo -> cruza la barrera de salida -> se va.
 *
 * Recursos compartidos (en memoria compartida entre procesos):
 *   espacios     : semaforo CONTADOR, cuantos espacios quedan libres (inicia en M)
 *   barrera      : semaforo BINARIO, solo un auto cruza la barrera a la vez
 *   mutex_estado : semaforo BINARIO, protege el arreglo de plazas y los contadores
 *
 * Uso: ./estacionamiento [autos] [espacios]   (por defecto 10 autos y 3 espacios)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_ESPACIOS 20
#define MAX_AUTOS    100

typedef struct {
    sem_t espacios;
    sem_t barrera;
    sem_t mutex_estado;

    int num_espacios;
    int plaza[MAX_ESPACIOS];     /* numero de auto en cada espacio, 0 = libre */
    int ocupados;
    int max_ocupados;            /* maximo de autos dentro al mismo tiempo */
    int atendidos;
    struct timespec inicio;
} Estacionamiento;

static Estacionamiento *est;     /* apunta a la memoria compartida */

static double segundos(void)
{
    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    return (ahora.tv_sec - est->inicio.tv_sec) +
           (ahora.tv_nsec - est->inicio.tv_nsec) / 1e9;
}

/* Imprime una linea completa con un solo write() para que no se mezcle
 * con la salida de los otros procesos. */
static void registrar(const char *fmt, ...)
{
    char linea[512];
    int n = snprintf(linea, sizeof linea, "[t=%5.2fs] ", segundos());
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(linea + n, sizeof linea - n, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof linea - 2)
        n = sizeof linea - 2;
    linea[n++] = '\n';
    if (write(STDOUT_FILENO, linea, n) < 0)
        perror("write");
}

/* Dibuja el estado de los espacios, por ejemplo: [A3 ][ - ][A7 ] */
static const char *dibujar(char *buf, size_t tam)
{
    size_t n = 0;
    for (int i = 0; i < est->num_espacios && n < tam; i++) {
        if (est->plaza[i])
            n += snprintf(buf + n, tam - n, "[A%-2d]", est->plaza[i]);
        else
            n += snprintf(buf + n, tam - n, "[ - ]");
    }
    return buf;
}

/* Codigo que ejecuta cada proceso hijo */
static void auto_proceso(int id)
{
    char mapa[160];
    srand(getpid() ^ time(NULL));
    usleep((id - 1) * 300000 + rand() % 200000);     /* llegadas escalonadas */

    registrar("Auto %2d (PID %d) LLEGA", id, getpid());

    /* 1. Reservar un espacio. Si el contador esta en 0 el proceso se
     *    bloquea aqui (sin consumir CPU) hasta que otro auto salga. */
    if (sem_trywait(&est->espacios) == -1) {
        registrar("Auto %2d ESPERA: estacionamiento lleno", id);
        sem_wait(&est->espacios);
    }

    /* 2. Cruzar la barrera de entrada: solo un auto a la vez */
    sem_wait(&est->barrera);
    registrar("Auto %2d cruza la barrera de ENTRADA", id);
    usleep(200000);

    /* 3. Ocupar un espacio concreto (seccion critica sobre los datos) */
    sem_wait(&est->mutex_estado);
    int mi_plaza = -1;
    for (int i = 0; i < est->num_espacios; i++) {
        if (est->plaza[i] == 0) {
            mi_plaza = i;
            break;
        }
    }
    if (mi_plaza == -1) {                 /* imposible gracias al semaforo contador */
        registrar("Auto %2d ERROR: no hay plaza libre", id);
        sem_post(&est->mutex_estado);
        sem_post(&est->barrera);
        exit(EXIT_FAILURE);
    }
    est->plaza[mi_plaza] = id;
    est->ocupados++;
    if (est->ocupados > est->max_ocupados)
        est->max_ocupados = est->ocupados;
    registrar("Auto %2d ESTACIONA en espacio %d   ocupados %d/%d  %s",
              id, mi_plaza + 1, est->ocupados, est->num_espacios,
              dibujar(mapa, sizeof mapa));
    sem_post(&est->mutex_estado);
    sem_post(&est->barrera);

    /* 4. Permanecer estacionado (fuera de toda seccion critica) */
    usleep(1000000 + rand() % 2000000);

    /* 5. Cruzar la barrera de salida y liberar la plaza */
    sem_wait(&est->barrera);
    sem_wait(&est->mutex_estado);
    est->plaza[mi_plaza] = 0;
    est->ocupados--;
    est->atendidos++;
    registrar("Auto %2d SALE del espacio %d       ocupados %d/%d  %s",
              id, mi_plaza + 1, est->ocupados, est->num_espacios,
              dibujar(mapa, sizeof mapa));
    sem_post(&est->mutex_estado);
    usleep(200000);
    sem_post(&est->barrera);

    /* 6. Devolver el espacio: despierta a un auto que este esperando */
    sem_post(&est->espacios);
}

int main(int argc, char *argv[])
{
    int autos    = (argc > 1) ? atoi(argv[1]) : 10;
    int espacios = (argc > 2) ? atoi(argv[2]) : 3;

    if (autos < 1 || autos > MAX_AUTOS || espacios < 1 || espacios > MAX_ESPACIOS) {
        fprintf(stderr, "Uso: %s [autos 1-%d] [espacios 1-%d]\n",
                argv[0], MAX_AUTOS, MAX_ESPACIOS);
        return EXIT_FAILURE;
    }

    /* Memoria compartida: la heredan los hijos despues de fork() */
    est = mmap(NULL, sizeof *est, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (est == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }

    /* pshared = 1: el semaforo se comparte entre procesos */
    if (sem_init(&est->espacios, 1, espacios) == -1 ||
        sem_init(&est->barrera, 1, 1) == -1 ||
        sem_init(&est->mutex_estado, 1, 1) == -1) {
        perror("sem_init");
        return EXIT_FAILURE;
    }
    est->num_espacios = espacios;
    clock_gettime(CLOCK_MONOTONIC, &est->inicio);

    printf("=== Estacionamiento CON sincronizacion (semaforos) ===\n");
    printf("Administrador PID %d | %d autos | %d espacios\n\n",
           getpid(), autos, espacios);
    fflush(stdout);

    int creados = 0;
    for (int i = 1; i <= autos; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            break;
        }
        if (pid == 0) {                              /* HIJO */
            auto_proceso(i);
            exit(EXIT_SUCCESS);
        }
        creados++;
    }

    /* PADRE: espera a que todos los autos terminen */
    int fallidos = 0;
    for (int i = 0; i < creados; i++) {
        int estado;
        if (wait(&estado) > 0 && (!WIFEXITED(estado) || WEXITSTATUS(estado) != 0))
            fallidos++;
    }

    printf("\n=== Resumen ===\n");
    printf("Autos atendidos         : %d de %d\n", est->atendidos, creados);
    printf("Maximo dentro a la vez  : %d (capacidad %d)\n", est->max_ocupados, espacios);
    printf("Ocupados al final       : %d\n", est->ocupados);

    int ok = est->max_ocupados <= espacios && est->ocupados == 0 &&
             est->atendidos == creados && fallidos == 0;
    printf("Resultado               : %s\n", ok
           ? "OK, nunca se supero la capacidad ni se compartio un espacio"
           : "ERROR en la simulacion");

    sem_destroy(&est->espacios);
    sem_destroy(&est->barrera);
    sem_destroy(&est->mutex_estado);
    munmap(est, sizeof *est);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
