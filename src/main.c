/*
 * main.c
 * Punto de entrada del Simulador de Servidor MMO
 *
 * Orquesta la creacion de todos los hilos y el apagado limpio.
 *
 * Compilar: gcc -o mmo_server src/main.c src/mundo.c src/jugador.c src/entorno.c -lpthread
 * Ejecutar: ./mmo_server
 */

#include "mmo_server.h"

/* Estructura para pasar args al hilo jugador (definida en jugador.c) */
typedef struct {
    MundoMMO *mundo;
    int jugador_idx;
} JugadorArgs;

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    MundoMMO mundo;
    pthread_t hilos_jugadores[TOTAL_PLAYERS];
    pthread_t hilo_respawn[NUM_WORLD_THREADS];
    pthread_t hilo_chat;
    pthread_t hilo_gameloop;

    printf("\n");
    printf("%s╔══════════════════════════════════════════════════════════╗%s\n", C_WHITE, C_RESET);
    printf("%s║    SIMULADOR DE SERVIDOR MMO - SISTEMAS OPERATIVOS     ║%s\n", C_WHITE, C_RESET);
    printf("%s║    UDLAP - Primavera 2026                              ║%s\n", C_WHITE, C_RESET);
    printf("%s║                                                        ║%s\n", C_WHITE, C_RESET);
    printf("%s║    Cupo maximo: %d jugadores                            ║%s\n", C_WHITE, MAX_PLAYERS, C_RESET);
    printf("%s║    Jugadores totales: %d (algunos esperaran)            ║%s\n", C_WHITE, TOTAL_PLAYERS, C_RESET);
    printf("%s║    Mapa: %dx%d casillas                                 ║%s\n", C_WHITE, MAP_WIDTH, MAP_HEIGHT, C_RESET);
    printf("%s║    Items iniciales: %d                                  ║%s\n", C_WHITE, MAX_ITEMS, C_RESET);
    printf("%s║    Duracion: %d segundos                                ║%s\n", C_WHITE, SIM_DURATION_SEC, C_RESET);
    printf("%s╚══════════════════════════════════════════════════════════╝%s\n\n", C_WHITE, C_RESET);

    /* ======================== FASE 1: INICIALIZACION ======================== */
    log_servidor(&mundo, "Fase 1: Inicializando estructuras y primitivas...");
    mundo_init(&mundo);
    log_servidor(&mundo, "Mundo creado. Mapa %dx%d con %d items.", MAP_WIDTH, MAP_HEIGHT, MAX_ITEMS);
    log_servidor(&mundo, "Semaforo de cupo inicializado con valor %d.", MAX_PLAYERS);

    imprimir_mapa(&mundo);

    /* ======================== FASE 2: LANZAR HILOS ======================== */
    log_servidor(&mundo, "Fase 2: Lanzando hilos de entorno y jugadores...");

    /* Hilo de game loop */
    pthread_create(&hilo_gameloop, NULL, hilo_game_loop, &mundo);

    /* Hilo de broadcast/chat */
    pthread_create(&hilo_chat, NULL, hilo_chat_broadcast, &mundo);

    /* Hilos de entorno (respawn) */
    for (int i = 0; i < NUM_WORLD_THREADS; i++) {
        pthread_create(&hilo_respawn[i], NULL, hilo_mundo_respawn, &mundo);
    }

    /* Hilos de jugadores */
    for (int i = 0; i < TOTAL_PLAYERS; i++) {
        JugadorArgs *args = malloc(sizeof(JugadorArgs));
        args->mundo = &mundo;
        args->jugador_idx = i;
        pthread_create(&hilos_jugadores[i], NULL, hilo_jugador, args);
    }

    log_servidor(&mundo, "Todos los hilos lanzados. Simulacion en curso...");
    log_servidor(&mundo, "Nota: %d jugadores competiran por %d slots de conexion.",
                 TOTAL_PLAYERS, MAX_PLAYERS);

    /* ======================== SIMULACION ======================== */
    sleep(SIM_DURATION_SEC);

    /* ======================== FASE 4: APAGADO LIMPIO ======================== */
    log_servidor(&mundo, "");
    log_servidor(&mundo, "======================================");
    log_servidor(&mundo, "  INICIANDO PROTOCOLO DE APAGADO...");
    log_servidor(&mundo, "======================================");

    mundo.servidor_activo = false;

    /* Despertar al hilo de chat si esta dormido en cond_wait */
    pthread_mutex_lock(&mundo.chat_mutex);
    pthread_cond_broadcast(&mundo.chat_cond);
    pthread_mutex_unlock(&mundo.chat_mutex);

    /* Liberar semaforo para hilos bloqueados en sem_wait */
    for (int i = 0; i < TOTAL_PLAYERS; i++) {
        sem_post(&mundo.cupo_semaforo);
    }

    /* Esperar terminacion de todos los hilos con pthread_join() */
    log_servidor(&mundo, "Esperando terminacion de hilos de jugadores (pthread_join)...");
    for (int i = 0; i < TOTAL_PLAYERS; i++) {
        pthread_join(hilos_jugadores[i], NULL);
    }
    log_servidor(&mundo, "Todos los hilos de jugadores terminados.");

    log_servidor(&mundo, "Esperando terminacion de hilos de entorno...");
    for (int i = 0; i < NUM_WORLD_THREADS; i++) {
        pthread_join(hilo_respawn[i], NULL);
    }
    log_servidor(&mundo, "Hilos de entorno terminados.");

    pthread_join(hilo_chat, NULL);
    log_servidor(&mundo, "Hilo de chat terminado.");

    pthread_join(hilo_gameloop, NULL);
    log_servidor(&mundo, "Hilo de game loop terminado.");

    /* ======================== RESUMEN FINAL ======================== */
    printf("\n");
    printf("%s╔══════════════════════════════════════════════════════════╗%s\n", C_WHITE, C_RESET);
    printf("%s║              RESUMEN FINAL DE SIMULACION                ║%s\n", C_WHITE, C_RESET);
    printf("%s╠══════════════════════════════════════════════════════════╣%s\n", C_WHITE, C_RESET);
    printf("%s║%s  Intentos de conexion:          %3d                     %s║%s\n",
           C_WHITE, C_CYAN, mundo.total_intentos_conexion, C_WHITE, C_RESET);
    printf("%s║%s  Conexiones rechazadas (espera): %2d                     %s║%s\n",
           C_WHITE, C_RED, mundo.total_conexiones_rechazadas, C_WHITE, C_RESET);
    printf("%s║%s  Movimientos totales:           %3d                     %s║%s\n",
           C_WHITE, C_GREEN, mundo.total_movimientos, C_WHITE, C_RESET);
    printf("%s║%s  Items recogidos:               %3d                     %s║%s\n",
           C_WHITE, C_YELLOW, mundo.total_items_recogidos, C_WHITE, C_RESET);
    printf("%s║%s  Mensajes de chat:              %3d                     %s║%s\n",
           C_WHITE, C_MAGENTA, mundo.total_mensajes_chat, C_WHITE, C_RESET);
    printf("%s║%s  Race conditions evitadas:      %3d                     %s║%s\n",
           C_WHITE, C_RED, mundo.total_race_conditions_evitadas, C_WHITE, C_RESET);
    printf("%s╠══════════════════════════════════════════════════════════╣%s\n", C_WHITE, C_RESET);

    /* Stats por jugador */
    printf("%s║  JUGADOR  │ ITEMS │ ESTADO                            ║%s\n", C_WHITE, C_RESET);
    printf("%s║───────────┼───────┼───────────────────────────────────║%s\n", C_WHITE, C_RESET);
    for (int i = 0; i < TOTAL_PLAYERS; i++) {
        printf("%s║%s  JUG %02d   │  %3d  │ %s                               %s║%s\n",
               C_WHITE, C_CYAN,
               mundo.jugadores[i].id,
               mundo.jugadores[i].items_recogidos,
               mundo.jugadores[i].conectado ? "Conectado  " : "Desconectado",
               C_WHITE, C_RESET);
    }

    printf("%s╚══════════════════════════════════════════════════════════╝%s\n\n", C_WHITE, C_RESET);

    /* ======================== DESTRUCCION DE PRIMITIVAS ======================== */
    log_servidor(&mundo, "Fase 4: Destruyendo primitivas de sincronizacion...");
    mundo_destroy(&mundo);

    log_servidor(&mundo, "Simulacion finalizada correctamente. Sin memory leaks.");
    printf("\n");

    return 0;
}
