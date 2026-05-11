/*
 * mmo_server.h
 * Simulador de Servidor MMO - Proyecto Final Sistemas Operativos
 * UDLAP Primavera 2026
 *
 * Estructuras de datos, constantes y prototipos compartidos.
 */

#ifndef MMO_SERVER_H
#define MMO_SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

/* ======================== CONSTANTES ======================== */
#define MAP_WIDTH        10
#define MAP_HEIGHT       10
#define MAX_PLAYERS      5      /* Cupo maximo del servidor (semaforo) */
#define TOTAL_PLAYERS    8      /* Jugadores que intentaran conectarse  */
#define MAX_ITEMS        6      /* Items iniciales en el mapa          */
#define CHAT_BUFFER_SIZE 20     /* Tamano del buffer circular de chat  */
#define SIM_DURATION_SEC 12     /* Duracion de la simulacion en seg    */
#define RESPAWN_INTERVAL 4      /* Segundos entre respawn de objetos   */
#define NUM_WORLD_THREADS 1     /* Hilos de entorno (respawn)          */

/* Tipos de celda en el mapa */
#define CELL_EMPTY   '.'
#define CELL_PLAYER  'P'
#define CELL_ITEM    '*'

/* Colores ANSI para la bitacora */
#define C_RESET   "\033[0m"
#define C_RED     "\033[1;31m"
#define C_GREEN   "\033[1;32m"
#define C_YELLOW  "\033[1;33m"
#define C_BLUE    "\033[1;34m"
#define C_MAGENTA "\033[1;35m"
#define C_CYAN    "\033[1;36m"
#define C_WHITE   "\033[1;37m"

/* ======================== ESTRUCTURAS ======================== */

/* Representa un item en el suelo */
typedef struct {
    int x, y;
    char nombre[32];
    bool activo;               /* true = esta en el mapa */
    pthread_mutex_t mutex;     /* Mutex individual por item */
} Item;

/* Representa un jugador conectado */
typedef struct {
    int id;
    int x, y;                  /* Posicion actual en el mapa */
    int items_recogidos;
    bool conectado;
    bool activo;               /* Hilo sigue ejecutandose */
} Jugador;

/* Mensaje del chat */
typedef struct {
    int jugador_id;
    char texto[128];
} MensajeChat;

/* Estado global del mundo */
typedef struct {
    /* --- Mapa (recurso compartido #1) --- */
    char mapa[MAP_HEIGHT][MAP_WIDTH];
    pthread_mutex_t mapa_mutex[MAP_HEIGHT][MAP_WIDTH]; /* Mutex por casilla */

    /* --- Items / Loot (recurso compartido #2) --- */
    Item items[MAX_ITEMS * 3]; /* Espacio para items originales + respawns */
    int total_items;
    pthread_mutex_t items_count_mutex;

    /* --- Chat (recurso compartido #3) --- */
    MensajeChat chat_buffer[CHAT_BUFFER_SIZE];
    int chat_write_idx;
    int chat_read_idx;
    int chat_count;
    pthread_mutex_t chat_mutex;
    pthread_cond_t  chat_cond;    /* Variable de condicion para chat */

    /* --- Cupo del servidor (recurso compartido #4) --- */
    sem_t cupo_semaforo;          /* Semaforo contador */
    int jugadores_conectados;
    pthread_mutex_t conexion_mutex;

    /* --- Jugadores --- */
    Jugador jugadores[TOTAL_PLAYERS];

    /* --- Control de simulacion --- */
    volatile bool servidor_activo;
    pthread_mutex_t log_mutex;    /* Para serializar salida a consola */

    /* --- Estadisticas --- */
    int total_movimientos;
    int total_items_recogidos;
    int total_mensajes_chat;
    int total_intentos_conexion;
    int total_conexiones_rechazadas;
    int total_race_conditions_evitadas;
    pthread_mutex_t stats_mutex;

} MundoMMO;

/* ======================== PROTOTIPOS ======================== */

/* Inicializacion y limpieza */
void mundo_init(MundoMMO *mundo);
void mundo_destroy(MundoMMO *mundo);

/* Funciones de log */
void log_servidor(MundoMMO *mundo, const char *fmt, ...);
void log_jugador(MundoMMO *mundo, int id, const char *fmt, ...);
void log_mundo(MundoMMO *mundo, const char *fmt, ...);
void log_chat(MundoMMO *mundo, const char *fmt, ...);

/* Impresion del mapa */
void imprimir_mapa(MundoMMO *mundo);
void imprimir_estado_servidor(MundoMMO *mundo);

/* Hilos */
void *hilo_jugador(void *arg);
void *hilo_mundo_respawn(void *arg);
void *hilo_chat_broadcast(void *arg);
void *hilo_game_loop(void *arg);

#endif /* MMO_SERVER_H */
