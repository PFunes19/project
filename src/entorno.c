/*
 * entorno.c
 * Hilos de entorno (World Threads): respawn de items y broadcast de chat.
 */

#include "mmo_server.h"

/* Nombres de items para respawn */
static const char *items_respawn[] = {
    "Pocion+", "Gema+", "Moneda+", "Escudo+", "Elixir+"
};

/* ============================================================
 * hilo_mundo_respawn: Reaparicion periodica de items
 *
 * Hilo en segundo plano que cada RESPAWN_INTERVAL segundos
 * coloca un nuevo item en una casilla vacia del mapa.
 *
 * SECCION CRITICA: Escritura en la matriz del mapa + lista de items
 * PRIMITIVA: Mutex por casilla del mapa
 * ============================================================ */
void *hilo_mundo_respawn(void *arg) {
    MundoMMO *mundo = (MundoMMO *)arg;

    log_mundo(mundo, "World Thread iniciado - Respawn cada %d segundos", RESPAWN_INTERVAL);

    while (mundo->servidor_activo) {
        sleep(RESPAWN_INTERVAL);

        if (!mundo->servidor_activo) break;

        log_mundo(mundo, "%sTiempo de respawn alcanzado%s - Calculando coordenadas...",
                  C_YELLOW, C_RESET);

        /* Buscar un slot libre para el nuevo item */
        int item_idx = -1;
        pthread_mutex_lock(&mundo->items_count_mutex);
        if (mundo->total_items < MAX_ITEMS * 3) {
            item_idx = mundo->total_items;
            mundo->total_items++;
        }
        pthread_mutex_unlock(&mundo->items_count_mutex);

        if (item_idx < 0) {
            log_mundo(mundo, "No hay espacio para mas items en el arreglo.");
            continue;
        }

        /* Buscar casilla vacia aleatoria */
        int intentos = 0;
        bool colocado = false;

        while (intentos < 50 && !colocado && mundo->servidor_activo) {
            int rx = rand() % MAP_WIDTH;
            int ry = rand() % MAP_HEIGHT;

            log_mundo(mundo,
                      "Intentando casilla (%d,%d) - %sEsperando Mutex casilla...%s",
                      rx, ry, C_YELLOW, C_RESET);

            pthread_mutex_lock(&mundo->mapa_mutex[ry][rx]);
            /* ---- INICIO SECCION CRITICA ---- */

            if (mundo->mapa[ry][rx] == CELL_EMPTY) {
                /* Casilla vacia - colocar item */
                Item *nuevo = &mundo->items[item_idx];
                nuevo->x = rx;
                nuevo->y = ry;
                snprintf(nuevo->nombre, 32, "%s", items_respawn[rand() % 5]);
                nuevo->activo = true;
                pthread_mutex_init(&nuevo->mutex, NULL);

                mundo->mapa[ry][rx] = CELL_ITEM;
                colocado = true;

                log_mundo(mundo,
                          "%sMutex adquirido%s - '%s' colocado en (%d,%d) - %sLiberando Mutex.%s",
                          C_GREEN, C_RESET, nuevo->nombre, rx, ry, C_GREEN, C_RESET);
            } else {
                log_mundo(mundo,
                          "Casilla (%d,%d) ocupada - %sLiberando Mutex.%s Reintentando...",
                          rx, ry, C_GREEN, C_RESET);
            }

            /* ---- FIN SECCION CRITICA ---- */
            pthread_mutex_unlock(&mundo->mapa_mutex[ry][rx]);
            intentos++;
        }

        if (!colocado) {
            log_mundo(mundo, "No se encontro casilla vacia para respawn.");
        }
    }

    log_mundo(mundo, "World Thread terminado.");
    pthread_exit(NULL);
}

/* ============================================================
 * hilo_chat_broadcast: Procesa y distribuye mensajes de chat
 *
 * Usa pthread_cond_wait() para dormir hasta que un jugador
 * envie un mensaje con pthread_cond_signal().
 * EVITA BUSY-WAITING.
 *
 * SECCION CRITICA: Lectura del buffer de chat compartido
 * PRIMITIVA: Mutex + Variable de Condicion
 * ============================================================ */
void *hilo_chat_broadcast(void *arg) {
    MundoMMO *mundo = (MundoMMO *)arg;

    log_chat(mundo, "Hilo de Broadcast/Chat iniciado - Esperando mensajes...");

    while (mundo->servidor_activo) {
        pthread_mutex_lock(&mundo->chat_mutex);

        /*
         * VARIABLE DE CONDICION: pthread_cond_wait()
         * El hilo DUERME aqui sin consumir CPU hasta que un jugador
         * haga pthread_cond_signal() al enviar un mensaje.
         * Esto evita el busy-waiting.
         */
        while (mundo->chat_read_idx == mundo->chat_write_idx &&
               mundo->servidor_activo) {
            log_chat(mundo, "%spthread_cond_wait()%s - Hilo dormido, esperando signal...",
                     C_YELLOW, C_RESET);
            pthread_cond_wait(&mundo->chat_cond, &mundo->chat_mutex);
            log_chat(mundo, "%sVariable de condicion: DESPIERTO%s - Signal recibido!",
                     C_GREEN, C_RESET);
        }

        if (!mundo->servidor_activo) {
            pthread_mutex_unlock(&mundo->chat_mutex);
            break;
        }

        /* Procesar el mensaje */
        MensajeChat *msg = &mundo->chat_buffer[mundo->chat_read_idx];

        log_chat(mundo,
                 "Procesando mensaje del Jugador %02d: \"%s\"",
                 msg->jugador_id, msg->texto);

        /* "Distribuir" a todos los jugadores conectados */
        int destinatarios = 0;
        for (int i = 0; i < TOTAL_PLAYERS; i++) {
            if (mundo->jugadores[i].conectado &&
                mundo->jugadores[i].id != msg->jugador_id) {
                destinatarios++;
            }
        }

        log_chat(mundo,
                 "Mensaje distribuido a %d jugadores conectados.",
                 destinatarios);

        mundo->chat_read_idx = (mundo->chat_read_idx + 1) % CHAT_BUFFER_SIZE;

        pthread_mutex_unlock(&mundo->chat_mutex);
    }

    log_chat(mundo, "Hilo de Broadcast/Chat terminado.");
    pthread_exit(NULL);
}

/* ============================================================
 * hilo_game_loop: Tick del servidor (imprime estado periodico)
 * ============================================================ */
void *hilo_game_loop(void *arg) {
    MundoMMO *mundo = (MundoMMO *)arg;
    int tick = 0;

    while (mundo->servidor_activo) {
        sleep(3);
        if (!mundo->servidor_activo) break;

        tick++;
        log_servidor(mundo, "=== TICK %d ===", tick);
        imprimir_estado_servidor(mundo);
        imprimir_mapa(mundo);
    }

    pthread_exit(NULL);
}
