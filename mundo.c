/*
 * mundo.c
 * Inicializacion del mundo, items, mapa; funciones de log y estado.
 */

#include "mmo_server.h"
#include <stdarg.h>

/* Nombres de items posibles */
static const char *nombres_items[] = {
    "Pocion", "Gema", "Moneda", "Escudo", "Espada",
    "Anillo", "Pergamino", "Llave", "Corona", "Elixir"
};
static const int num_nombres = 10;

/* ============================================================
 * mundo_init: Inicializa TODAS las estructuras y primitivas
 * ============================================================ */
void mundo_init(MundoMMO *mundo) {
    memset(mundo, 0, sizeof(MundoMMO));
    mundo->servidor_activo = true;

    srand((unsigned)time(NULL));

    /* --- Inicializar mapa vacio --- */
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            mundo->mapa[y][x] = CELL_EMPTY;
            pthread_mutex_init(&mundo->mapa_mutex[y][x], NULL);
        }
    }

    /* --- Colocar items iniciales --- */
    mundo->total_items = MAX_ITEMS;
    for (int i = 0; i < MAX_ITEMS; i++) {
        int ix, iy;
        do {
            ix = rand() % MAP_WIDTH;
            iy = rand() % MAP_HEIGHT;
        } while (mundo->mapa[iy][ix] != CELL_EMPTY);

        mundo->items[i].x = ix;
        mundo->items[i].y = iy;
        snprintf(mundo->items[i].nombre, 32, "%s", nombres_items[rand() % num_nombres]);
        mundo->items[i].activo = true;
        pthread_mutex_init(&mundo->items[i].mutex, NULL);
        mundo->mapa[iy][ix] = CELL_ITEM;
    }
    pthread_mutex_init(&mundo->items_count_mutex, NULL);

    /* --- Inicializar chat --- */
    mundo->chat_write_idx = 0;
    mundo->chat_read_idx = 0;
    mundo->chat_count = 0;
    pthread_mutex_init(&mundo->chat_mutex, NULL);
    pthread_cond_init(&mundo->chat_cond, NULL);

    /* --- Semaforo de cupo (valor inicial = MAX_PLAYERS) --- */
    sem_init(&mundo->cupo_semaforo, 0, MAX_PLAYERS);
    mundo->jugadores_conectados = 0;
    pthread_mutex_init(&mundo->conexion_mutex, NULL);

    /* --- Jugadores (no conectados aun) --- */
    for (int i = 0; i < TOTAL_PLAYERS; i++) {
        mundo->jugadores[i].id = i + 1;
        mundo->jugadores[i].x = -1;
        mundo->jugadores[i].y = -1;
        mundo->jugadores[i].items_recogidos = 0;
        mundo->jugadores[i].conectado = false;
        mundo->jugadores[i].activo = false;
    }

    /* --- Mutex de log y stats --- */
    pthread_mutex_init(&mundo->log_mutex, NULL);
    pthread_mutex_init(&mundo->stats_mutex, NULL);
}

/* ============================================================
 * mundo_destroy: Destruye TODAS las primitivas de sincronizacion
 * ============================================================ */
void mundo_destroy(MundoMMO *mundo) {
    /* Mutex del mapa */
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            pthread_mutex_destroy(&mundo->mapa_mutex[y][x]);

    /* Mutex de items */
    for (int i = 0; i < mundo->total_items; i++)
        pthread_mutex_destroy(&mundo->items[i].mutex);
    pthread_mutex_destroy(&mundo->items_count_mutex);

    /* Chat */
    pthread_mutex_destroy(&mundo->chat_mutex);
    pthread_cond_destroy(&mundo->chat_cond);

    /* Conexion */
    sem_destroy(&mundo->cupo_semaforo);
    pthread_mutex_destroy(&mundo->conexion_mutex);

    /* Log y stats */
    pthread_mutex_destroy(&mundo->log_mutex);
    pthread_mutex_destroy(&mundo->stats_mutex);

    printf("\n%s[CLEANUP] Todas las primitivas destruidas. Memoria liberada.%s\n",
           C_MAGENTA, C_RESET);
}

/* ============================================================
 * Funciones de LOG con mutex para evitar entrelazado
 * ============================================================ */
void log_servidor(MundoMMO *mundo, const char *fmt, ...) {
    pthread_mutex_lock(&mundo->log_mutex);
    printf("%s[SERVER]%s ", C_GREEN, C_RESET);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&mundo->log_mutex);
}

void log_jugador(MundoMMO *mundo, int id, const char *fmt, ...) {
    pthread_mutex_lock(&mundo->log_mutex);
    printf("%s[JUGADOR %02d]%s ", C_CYAN, id, C_RESET);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&mundo->log_mutex);
}

void log_mundo(MundoMMO *mundo, const char *fmt, ...) {
    pthread_mutex_lock(&mundo->log_mutex);
    printf("%s[WORLD]%s ", C_YELLOW, C_RESET);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&mundo->log_mutex);
}

void log_chat(MundoMMO *mundo, const char *fmt, ...) {
    pthread_mutex_lock(&mundo->log_mutex);
    printf("%s[CHAT]%s ", C_MAGENTA, C_RESET);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&mundo->log_mutex);
}

/* ============================================================
 * imprimir_mapa: Muestra el mapa en consola
 * ============================================================ */
void imprimir_mapa(MundoMMO *mundo) {
    pthread_mutex_lock(&mundo->log_mutex);
    printf("\n%s=== MAPA DEL MUNDO ===%s\n", C_WHITE, C_RESET);
    printf("   ");
    for (int x = 0; x < MAP_WIDTH; x++) printf("%d ", x);
    printf("\n");

    for (int y = 0; y < MAP_HEIGHT; y++) {
        printf("%2d ", y);
        for (int x = 0; x < MAP_WIDTH; x++) {
            char c = mundo->mapa[y][x];
            if (c == CELL_PLAYER)
                printf("%s%c%s ", C_CYAN, c, C_RESET);
            else if (c == CELL_ITEM)
                printf("%s%c%s ", C_YELLOW, c, C_RESET);
            else
                printf("%c ", c);
        }
        printf("\n");
    }
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&mundo->log_mutex);
}

/* ============================================================
 * imprimir_estado_servidor: Resumen de estado global
 * ============================================================ */
void imprimir_estado_servidor(MundoMMO *mundo) {
    int items_activos = 0;
    for (int i = 0; i < mundo->total_items; i++)
        if (mundo->items[i].activo) items_activos++;

    pthread_mutex_lock(&mundo->log_mutex);
    printf("\n%s╔══════════════════════════════════════════╗%s\n", C_WHITE, C_RESET);
    printf("%s║        ESTADO DEL SERVIDOR MMO           ║%s\n", C_WHITE, C_RESET);
    printf("%s╠══════════════════════════════════════════╣%s\n", C_WHITE, C_RESET);
    printf("%s║%s Jugadores: %d/%d   Items en suelo: %2d     %s║%s\n",
           C_WHITE, C_GREEN, mundo->jugadores_conectados, MAX_PLAYERS,
           items_activos, C_WHITE, C_RESET);
    printf("%s║%s Movimientos: %3d  Recolecciones: %3d     %s║%s\n",
           C_WHITE, C_CYAN, mundo->total_movimientos,
           mundo->total_items_recogidos, C_WHITE, C_RESET);
    printf("%s║%s Mensajes chat: %3d                      %s║%s\n",
           C_WHITE, C_MAGENTA, mundo->total_mensajes_chat, C_WHITE, C_RESET);
    printf("%s║%s Race conditions evitadas: %3d            %s║%s\n",
           C_WHITE, C_RED, mundo->total_race_conditions_evitadas, C_WHITE, C_RESET);
    printf("%s╚══════════════════════════════════════════╝%s\n\n", C_WHITE, C_RESET);
    fflush(stdout);
    pthread_mutex_unlock(&mundo->log_mutex);
}
