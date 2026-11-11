/*
 * Zonas del estacionamiento del centro comercial.
 *
 * El orden del arreglo es el orden de PREFERENCIA de los conductores:
 * primero la zona de acceso mas facil y al final la mas complicada.
 * En ese orden se llenan y en ese orden las dibuja el panel.
 *
 * "recorrido" es lo que tarda un auto (en segundos reales) en ir desde la
 * barrera hasta la zona; cuanto mas dificil el acceso, mayor el recorrido.
 */
#ifndef ZONAS_H
#define ZONAS_H

enum { A_NIVEL, SUBTERRANEO, SUPERIOR };

typedef struct {
    const char *nombre;
    const char *detalle;      /* por que es mas o menos comoda */
    int tipo;
    int capacidad;
    int recorrido;            /* segundos reales desde la barrera */
} Zona;

static const Zona ZONAS[] = {
    { "Nivel 0 · Frente",  "junto a la entrada principal",  A_NIVEL,     200,  20 },
    { "Nivel 0 · Lateral", "al costado del edificio",       A_NIVEL,     250,  40 },
    { "Sótano 1",          "una rampa de bajada",           SUBTERRANEO, 350,  70 },
    { "Piso 1",            "una rampa en espiral",          SUPERIOR,    300,  90 },
    { "Sótano 2",          "dos rampas, poca luz",          SUBTERRANEO, 350, 120 },
    { "Piso 2 · Azotea",   "tres rampas, a la intemperie",  SUPERIOR,    250, 150 },
};

#define NUM_ZONAS ((int)(sizeof ZONAS / sizeof ZONAS[0]))

#define CARRILES_ENTRADA 4    /* barreras de entrada que funcionan a la vez */
#define CARRILES_SALIDA  3    /* barreras de salida */

#define MAX_ESPACIOS 3000     /* la suma de las capacidades no puede superarlo */
#define MAX_AUTOS    5000

/* Indice del primer espacio de la zona z dentro del arreglo de espacios */
static inline int zona_inicio(int z)
{
    int inicio = 0;
    for (int i = 0; i < z; i++)
        inicio += ZONAS[i].capacidad;
    return inicio;
}

static inline int total_espacios(void)
{
    return zona_inicio(NUM_ZONAS);
}

/* Zona a la que pertenece un espacio */
static inline int zona_de_espacio(int espacio)
{
    int z = 0;
    while (z < NUM_ZONAS - 1 && espacio >= zona_inicio(z + 1))
        z++;
    return z;
}

#endif
