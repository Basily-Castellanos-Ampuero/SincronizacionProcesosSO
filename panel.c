/*
 * Implementacion del panel (ver panel.h). Usa secuencias ANSI para los
 * colores y para redibujar la pantalla en el mismo lugar.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "panel.h"
#include "tiempos.h"

#define RESET      "\033[0m"
#define NEGRITA    "\033[1m"
#define TENUE      "\033[2m"
#define ROJO       "\033[1;31m"
#define VERDE      "\033[32m"
#define AMARILLO   "\033[33m"
#define AZUL       "\033[34m"
#define MAGENTA    "\033[35m"
#define CIAN       "\033[36m"
#define FONDO_ROJO "\033[1;97;41m"

#define ANCHO_BARRA_ZONA  20
#define ANCHO_BARRA_TOTAL 40
#define MAX_EVENTOS_VISIBLES 8

static int con_color;
static int es_terminal;
static const char *titulo_panel;
static int titulo_alerta;

static char pantalla[1 << 18];
static size_t usado;

/* Datos que se recalculan en cada foto */
static int cuantos[MAX_ESPACIOS];     /* autos que ocupan cada espacio */
static int ultimo[MAX_ESPACIOS];      /* uno de ellos, para dibujar su letra */
static int reservado[MAX_ESPACIOS];   /* auto que va en camino a ese espacio */

static const char *c(const char *codigo)
{
    return con_color ? codigo : "";
}

/* Agrega texto al buffer de la pantalla */
static void pon(const char *fmt, ...)
{
    if (usado >= sizeof pantalla - 1)
        return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(pantalla + usado, sizeof pantalla - usado, fmt, ap);
    va_end(ap);
    if (n > 0)
        usado += (size_t)n;
    if (usado > sizeof pantalla - 1)
        usado = sizeof pantalla - 1;
}

static void repetir(const char *s, int n)
{
    while (n-- > 0)
        pon("%s", s);
}

/* Termina la linea borrando lo que quedo del dibujo anterior */
static void fin_linea(void)
{
    pon("%s\033[K\n", c(RESET));
}

/* Ancho visible de un texto UTF-8 (cuenta caracteres, no bytes) */
static int ancho(const char *s)
{
    int n = 0;
    for (; *s; s++)
        if ((*s & 0xC0) != 0x80)
            n++;
    return n;
}

static void rellenar(const char *s, int columnas)
{
    pon("%s", s);
    repetir(" ", columnas - ancho(s));
}

static void etiqueta(const char *s)
{
    pon("  %s", c(NEGRITA));
    rellenar(s, 12);
    pon("%s", c(RESET));
}

static const char *color_evento(int tipo)
{
    switch (tipo) {
    case EV_LLEGA:  return c(CIAN);
    case EV_ESPERA: return c(AMARILLO);
    case EV_ENTRA:  return c(VERDE);
    case EV_SALE:   return c(AZUL);
    default:        return c(ROJO);
    }
}

static const char *color_zona(int tipo)
{
    switch (tipo) {
    case A_NIVEL:     return c(VERDE);
    case SUBTERRANEO: return c(CIAN);
    default:          return c(MAGENTA);
    }
}

static const char *nombre_tipo(int tipo)
{
    switch (tipo) {
    case A_NIVEL:     return "a nivel";
    case SUBTERRANEO: return "subterráneo";
    default:          return "superior";
    }
}

/* Barra de ocupacion: verde, amarilla si esta llena, roja si se paso */
static void barra(int valor, int maximo, int largo)
{
    int llenos = maximo > 0 ? (int)((long)valor * largo / maximo) : 0;
    if (valor > 0 && llenos == 0)
        llenos = 1;
    if (llenos > largo)
        llenos = largo;
    const char *col = valor > maximo ? c(ROJO) : valor == maximo ? c(AMARILLO) : c(VERDE);
    pon("%s", col);
    repetir("█", llenos);
    pon("%s", c(TENUE));
    repetir("░", largo - llenos);
    pon("%s", c(RESET));
}

const char *hora_real(double t, char *buf)
{
    long s = (long)(t * ESCALA);
    snprintf(buf, 32, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
    return buf;
}

void registro_iniciar(Registro *r)
{
    clock_gettime(CLOCK_MONOTONIC, &r->inicio);
}

double registro_tiempo(const Registro *r)
{
    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    return (ahora.tv_sec - r->inicio.tv_sec) +
           (ahora.tv_nsec - r->inicio.tv_nsec) / 1e9;
}

void registrar(Registro *r, int tipo, const char *fmt, ...)
{
    unsigned i = __atomic_fetch_add(&r->total, 1, __ATOMIC_SEQ_CST);
    Evento *e = &r->ev[i % MAX_EVENTOS];
    __atomic_store_n(&e->seq, 0, __ATOMIC_RELEASE);
    e->t = registro_tiempo(r);
    e->tipo = tipo;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->texto, sizeof e->texto, fmt, ap);
    va_end(ap);
    __atomic_store_n(&e->seq, i + 1, __ATOMIC_RELEASE);
}

int panel_iniciar(const char *titulo, int alerta)
{
    titulo_panel = titulo;
    titulo_alerta = alerta;
    es_terminal = isatty(STDOUT_FILENO);
    con_color = es_terminal && getenv("NO_COLOR") == NULL;
    if (es_terminal)
        printf("\033[?25l\033[2J");            /* oculta el cursor y limpia */
    else
        printf("=== %s ===\n\n", titulo);
    fflush(stdout);
    return es_terminal;
}

void panel_terminar(void)
{
    if (es_terminal)
        printf("\033[?25h");                    /* vuelve a mostrar el cursor */
    fflush(stdout);
}

/* Encabezado de una zona y su matriz de espacios (un caracter por espacio,
 * en bloques de 10) */
static void dibujar_zona(const Foto *f, int z, int por_fila, int ocupando, int con_matriz)
{
    const Zona *zn = &ZONAS[z];
    int inicio = zona_inicio(z);
    char num[16];

    pon("  %s%d%s ", c(TENUE), z + 1, c(RESET));
    pon("%s", c(NEGRITA));
    rellenar(zn->nombre, 19);
    pon("%s%s", c(RESET), color_zona(zn->tipo));
    rellenar(nombre_tipo(zn->tipo), 13);
    pon("%s", c(RESET));
    barra(ocupando, zn->capacidad, ANCHO_BARRA_ZONA);
    snprintf(num, sizeof num, "%d/%d", ocupando, zn->capacidad);
    pon("  %-9s", num);
    if (ocupando > zn->capacidad)
        pon(" %s¡SOBRECUPO!%s", c(ROJO), c(RESET));
    if (f->con_semaforos)
        pon(" %ssem = %d", c(TENUE), f->sem_zona[z]);
    else if (f->contador_zona[z] != ocupando)
        pon(" %scontador = %d ✘", c(ROJO), f->contador_zona[z]);
    fin_linea();
    if (!con_matriz)
        return;

    const char *actual = NULL;
    for (int k = 0; k < zn->capacidad; k++) {
        if (k % por_fila == 0) {
            if (k > 0)
                fin_linea();
            pon("    ");
            actual = NULL;
        } else if (k % 10 == 0) {
            pon("%s ", c(RESET));
            actual = NULL;
        }
        int e = inicio + k;
        const char *col;
        if (cuantos[e] >= 2)
            col = c(FONDO_ROJO);
        else if (cuantos[e] == 1)
            col = color_zona(zn->tipo);
        else
            col = c(TENUE);
        if (col != actual) {
            pon("%s%s", c(RESET), col);
            actual = col;
        }
        if (cuantos[e] >= 1)
            pon("%c", LETRA(ultimo[e]));
        else if (reservado[e])
            pon("%c", tolower((unsigned char)LETRA(reservado[e])));
        else
            pon("·");
    }
    fin_linea();
}

static void carriles(const int *carril, int n)
{
    for (int i = 0; i < n; i++) {
        if (carril[i])
            pon("%s[%c]%s", c(NEGRITA VERDE), LETRA(carril[i]), c(RESET));
        else
            pon("%s[·]%s", c(TENUE), c(RESET));
    }
}

void panel_dibujar(const Foto *f, const Registro *r)
{
    struct winsize ws;
    int cols = 100, filas = 50;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        cols = ws.ws_col;
        filas = ws.ws_row;
    }
    int bloques = (cols - 3) / 11;
    if (bloques < 2)
        bloques = 2;
    if (bloques > 12)
        bloques = 12;
    int por_fila = bloques * 10;

    /* Quien ocupa cada espacio, segun el estado de cada auto */
    int total = total_espacios();
    memset(cuantos, 0, sizeof(int) * total);
    memset(reservado, 0, sizeof(int) * total);
    int n[SE_FUE + 1] = {0};
    int ocupando_zona[NUM_ZONAS] = {0};
    int ocupando = 0;
    for (int id = 1; id <= f->autos; id++) {
        int e = f->estado[id], s = f->espacio_de[id];
        n[e]++;
        if (s < 0 || s >= total)
            continue;
        if (e == ESTACIONADO) {
            cuantos[s]++;
            ultimo[s] = id;
        } else if (e == BUSCANDO) {
            reservado[s] = id;
        } else {
            continue;
        }
        ocupando++;
        ocupando_zona[zona_de_espacio(s)]++;
    }

    /* Cuantos eventos entran en la pantalla */
    int lineas = 3 + 1 + 4 + 1 + 1 + 1 + 4 + 1 + 1 + 2 + NUM_ZONAS;
    int filas_matriz = 0;
    for (int z = 0; z < NUM_ZONAS; z++)
        filas_matriz += (ZONAS[z].capacidad + por_fila - 1) / por_fila;
    int con_matriz = lineas + filas_matriz < filas;   /* si no entra, modo compacto */
    if (con_matriz)
        lineas += filas_matriz;
    else
        lineas += 1;
    int ver_eventos = filas - lineas - 1;
    if (ver_eventos > MAX_EVENTOS_VISIBLES)
        ver_eventos = MAX_EVENTOS_VISIBLES;
    if (ver_eventos < 0)
        ver_eventos = 0;

    int largo_linea = por_fila + bloques + 3;
    if (largo_linea < 72)
        largo_linea = 72;
    char reloj[32];
    double t = registro_tiempo(r);

    usado = 0;
    pon("\033[H");

    /* Encabezado */
    pon("%s", c(TENUE)); repetir("━", largo_linea); fin_linea();
    pon("  %s%s%s", c(titulo_alerta ? ROJO : NEGRITA), titulo_panel, c(RESET));
    pon("%s   simulación %4.1f s · tiempo real %s · escala 1:%d",
        c(TENUE), t, hora_real(t, reloj), ESCALA);
    fin_linea();
    pon("%s", c(TENUE)); repetir("━", largo_linea); fin_linea();
    fin_linea();

    /* Accesos */
    etiqueta("ENTRADA");
    if (f->con_semaforos) {
        carriles(f->carril_entrada, CARRILES_ENTRADA);
        pon("   cola: %s%d%s", n[ESPERA_ENTRADA] ? c(AMARILLO) : "", n[ESPERA_ENTRADA], c(RESET));
    } else {
        pon("%s%d auto(s) cruzando a la vez%s", n[ENTRANDO] > CARRILES_ENTRADA ? c(ROJO) : "",
            n[ENTRANDO], c(RESET));
        if (n[ENTRANDO] > CARRILES_ENTRADA)
            pon("  %s¡solo hay %d barreras!", c(ROJO), CARRILES_ENTRADA);
    }
    fin_linea();

    etiqueta("SALIDA");
    if (f->con_semaforos) {
        carriles(f->carril_salida, CARRILES_SALIDA);
        pon("      cola: %s%d%s", n[ESPERA_SALIDA] ? c(AMARILLO) : "", n[ESPERA_SALIDA], c(RESET));
    } else {
        pon("%s%d auto(s) cruzando a la vez%s", n[SALIENDO] > CARRILES_SALIDA ? c(ROJO) : "",
            n[SALIENDO], c(RESET));
        if (n[SALIENDO] > CARRILES_SALIDA)
            pon("  %s¡solo hay %d barreras!", c(ROJO), CARRILES_SALIDA);
    }
    fin_linea();

    etiqueta("AFUERA");
    pon("por llegar %d   ·   esperando lugar %s%d%s", n[EN_CAMINO],
        n[ESPERA_ESPACIO] ? c(AMARILLO) : "", n[ESPERA_ESPACIO], c(RESET));
    if (n[ESPERA_ESPACIO])
        pon("  %s(centro comercial lleno)", c(AMARILLO));
    fin_linea();

    etiqueta("CIRCULANDO");
    pon("hacia su lugar %d   ·   hacia la salida %d", n[BUSCANDO], n[YENDO_SALIDA]);
    fin_linea();
    fin_linea();

    /* Zonas, de la mas facil a la mas dificil */
    pon("  %sZONAS — de la de acceso más fácil a la más difícil%s", c(NEGRITA), c(RESET));
    fin_linea();
    for (int z = 0; z < NUM_ZONAS; z++)
        dibujar_zona(f, z, por_fila, ocupando_zona[z], con_matriz);
    if (!con_matriz) {
        pon("  %sMatriz oculta: la terminal tiene %d filas y se necesitan %d. "
            "Agrande la ventana o reduzca el zoom (Ctrl -).",
            c(AMARILLO), filas, lineas + filas_matriz - 1);
        fin_linea();
    }
    fin_linea();

    /* Totales */
    etiqueta("OCUPACIÓN");
    barra(ocupando, total, ANCHO_BARRA_TOTAL);
    pon("  %d/%d  (%d%%)", ocupando, total, ocupando * 100 / total);
    if (ocupando > total)
        pon("  %s¡SOBRECUPO!", c(ROJO));
    fin_linea();

    etiqueta("CONTADOR");
    pon("ocupados = %d", f->contador_total);
    if (f->contador_total != ocupando)
        pon("   %s✘ no coincide: hay %d", c(ROJO), ocupando);
    else
        pon("   %s✔ coincide", c(VERDE));
    fin_linea();

    etiqueta("SEMÁFOROS");
    if (f->con_semaforos)
        pon("espacios = %s%d%s   entrada = %d   salida = %d   %s(+ \"sem\" de cada zona)",
            f->sem_espacios ? "" : c(AMARILLO), f->sem_espacios, c(RESET),
            f->sem_entrada, f->sem_salida, c(TENUE));
    else
        pon("%sninguno: nadie controla el acceso", c(ROJO));
    fin_linea();

    etiqueta("ATENDIDOS");
    pon("%d de %d", n[SE_FUE], f->autos);
    fin_linea();
    fin_linea();

    /* Ultimos eventos */
    pon("%s  ── Últimos eventos ", c(TENUE));
    repetir("─", largo_linea - 21);
    fin_linea();
    unsigned hechos = __atomic_load_n(&r->total, __ATOMIC_ACQUIRE);
    unsigned listos[MAX_EVENTOS_VISIBLES];
    int k = 0;
    for (unsigned i = hechos; i-- > 0 && k < ver_eventos && hechos - i <= 64; )
        if (__atomic_load_n(&r->ev[i % MAX_EVENTOS].seq, __ATOMIC_ACQUIRE) == i + 1)
            listos[k++] = i;
    for (int j = k - 1; j >= 0; j--) {
        const Evento *e = &r->ev[listos[j] % MAX_EVENTOS];
        pon("  %s%s%s  %s%s", c(TENUE), hora_real(e->t, reloj), c(RESET),
            color_evento(e->tipo), e->texto);
        fin_linea();
    }
    for (int j = k; j < ver_eventos; j++)
        fin_linea();

    /* Leyenda */
    pon("  %sA-Z%s auto (la letra se repite cada 26)   %sa-z%s va en camino a ese lugar   "
        "%s·%s libre   %sX%s 2 o más autos en un lugar",
        c(NEGRITA), c(RESET), c(TENUE), c(RESET), c(TENUE), c(RESET), c(FONDO_ROJO), c(RESET));
    fin_linea();
    pon("  %sverde%s a nivel   %scian%s subterráneo   %smagenta%s superior   "
        "%ssem%s = lugares libres según el semáforo de la zona",
        c(VERDE), c(RESET), c(CIAN), c(RESET), c(MAGENTA), c(RESET), c(TENUE), c(RESET));
    fin_linea();
    pon("\033[J");

    if (write(STDOUT_FILENO, pantalla, usado) < 0)
        perror("write");
}

void panel_volcar(const Registro *r)
{
    static unsigned siguiente = 0;
    char reloj[32];
    unsigned hechos = __atomic_load_n(&r->total, __ATOMIC_ACQUIRE);
    if (hechos - siguiente > MAX_EVENTOS)            /* ya se sobrescribieron */
        siguiente = hechos - MAX_EVENTOS;
    while (siguiente < hechos) {
        const Evento *e = &r->ev[siguiente % MAX_EVENTOS];
        unsigned seq = __atomic_load_n(&e->seq, __ATOMIC_ACQUIRE);
        if (seq == siguiente + 1)
            printf("[t=%6.2fs | %s] %s\n", e->t, hora_real(e->t, reloj), e->texto);
        else if (seq <= siguiente)                   /* todavia se esta escribiendo */
            break;
        siguiente++;
    }
    fflush(stdout);
}

void panel_resumen(void)
{
    printf("\n  %s RESUMEN %s\n", c("\033[1;7m"), c(RESET));
}

/* Escribe s en stdout completando con espacios hasta "columnas" */
static void imprimir_relleno(const char *s, int columnas)
{
    printf("%s%*s", s, columnas - ancho(s) > 0 ? columnas - ancho(s) : 0, "");
}

void panel_tabla_zonas(const int *maximo, const double *primer_auto, const double *lleno_desde)
{
    char reloj[32], txt[32];
    printf("\n  %s", c(NEGRITA));
    imprimir_relleno("ZONA", 22);
    imprimir_relleno("TIPO", 14);
    imprimir_relleno("CAPACIDAD", 11);
    imprimir_relleno("MÁX. USADO", 12);
    imprimir_relleno("PRIMER AUTO", 13);
    printf("SE LLENÓ%s\n", c(RESET));
    for (int z = 0; z < NUM_ZONAS; z++) {
        printf("  %d ", z + 1);
        imprimir_relleno(ZONAS[z].nombre, 20);
        printf("%s", color_zona(ZONAS[z].tipo));
        imprimir_relleno(nombre_tipo(ZONAS[z].tipo), 14);
        printf("%s%-11d", c(RESET), ZONAS[z].capacidad);
        snprintf(txt, sizeof txt, "%d", maximo[z]);
        printf("%s", maximo[z] > ZONAS[z].capacidad ? c(ROJO) : "");
        imprimir_relleno(txt, 12);
        printf("%s", c(RESET));
        imprimir_relleno(primer_auto[z] < 0 ? "nunca" : hora_real(primer_auto[z], reloj), 13);
        printf("%s\n", lleno_desde[z] < 0 ? "nunca" : hora_real(lleno_desde[z], reloj));
    }
    printf("  %s(horas en tiempo real desde que abre el estacionamiento)%s\n\n",
           c(TENUE), c(RESET));
}

void panel_resultado(int ok, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("  %s%s%s ", ok ? c(VERDE) : c(ROJO), ok ? "✔" : "✘", c(RESET));
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}
