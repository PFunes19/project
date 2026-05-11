/*
 * jugador.c
 * Hilo de jugador: conexion (semaforo), movimiento (mutex por casilla),
 * recoleccion de items (mutex por item), envio de chat (cond variable).
 */

#include "mmo_server.h"

/* Argumento para el hilo del jugador */
typedef struct {
    MundoMMO *mundo;
    int jugador_idx;
} JugadorArgs;

/* Mensajes predefinidos para el chat */
static const char *mensajes_chat[] = {
    "Hola a todos!",
    "Alguien quiere hacer equipo?",
    "Encontre una gema!",
    "Cuidado con la zona norte",
    "GG bien jugado",
    "Necesito ayuda",
    "Vamos al centro del mapa",
    "Adios, me desconecto"
};
static const int num_mensajes = 8;

/* ============================================================
 * enviar_chat: Escribe mensaje en el buffer usando cond variable
 *
 * SECCION CRITICA: Buffer de chat compartido
 * PRIMITIVA: Mutex + Variable de Condicion (pthread_cond_signal)
 * ============================================================ */
static void enviar_chat(MundoMMO *mundo, int jugador_id, const char *texto) {
    log_jugador(mundo, jugador_id,
                "Intentando enviar mensaje al chat - %sEsperando Mutex de chat...%s",
                C_YELLOW, C_RESET);

    pthread_mutex_lock(&mundo->chat_mutex);

    log_jugador(mundo, jugador_id,
                "%sMutex de chat adquirido%s - Escribiendo mensaje...",
                C_GREEN, C_RESET);

    /* Escribir en el buffer circular */
    MensajeChat *msg = &mundo->chat_buffer[mundo->chat_write_idx];
    msg->jugador_id = jugador_id;
    snprintf(msg->texto, 128, "%s", texto);

    mundo->chat_write_idx = (mundo->chat_write_idx + 1) % CHAT_BUFFER_SIZE;
    if (mundo->chat_count < CHAT_BUFFER_SIZE)
        mundo->chat_count++;

    /* Senalar al hilo de broadcast que hay un nuevo mensaje */
    pthread_cond_signal(&mundo->chat_cond);

    log_jugador(mundo, jugador_id,
                "Mensaje enviado - %sLiberando Mutex de chat. Signal enviado.%s",
                C_GREEN, C_RESET);

    pthread_mutex_unlock(&mundo->chat_mutex);

    pthread_mutex_lock(&mundo->stats_mutex);
    mundo->total_mensajes_chat++;
    pthread_mutex_unlock(&mundo->stats_mutex);
}

/* ============================================================
 * intentar_recoger_item: Busca items cercanos e intenta recogerlos
 *
 * SECCION CRITICA: Validacion y extraccion de item
 * PRIMITIVA: Mutex individual por item
 *
 * >>> AQUI SE PREVIENE LA RACE CONDITION <<<
 * Sin el mutex, dos hilos podrian leer que el item esta activo
 * y ambos lo recogerían (clonacion de recursos).
 * ============================================================ */
static void intentar_recoger_item(MundoMMO *mundo, Jugador *j) {
    for (int i = 0; i < mundo->total_items; i++) {
        Item *item = &mundo->items[i];

        /* Verificacion rapida sin lock (puede dar falso positivo, nunca falso negativo peligroso) */
        if (!item->activo) continue;
        if (item->x != j->x || item->y != j->y) continue;

        /* Item potencialmente recolectable - entrar a seccion critica */
        log_jugador(mundo, j->id,
                    "Intentando recoger '%s' en (%d,%d) - %sEsperando Mutex del item...%s",
                    item->nombre, item->x, item->y, C_YELLOW, C_RESET);

        pthread_mutex_lock(&item->mutex);
        /* ---- INICIO SECCION CRITICA: Recoleccion de item ---- */

        log_jugador(mundo, j->id,
                    "%sMutex del item adquirido%s - Validando existencia de '%s'...",
                    C_GREEN, C_RESET, item->nombre);

        /*
         * SEGUNDA VALIDACION (dentro del mutex):
         * Esto es lo que previene la race condition.
         * Otro hilo pudo haber recogido el item mientras esperabamos el mutex.
         */
        if (item->activo && item->x == j->x && item->y == j->y) {
            /* El item sigue aqui - recogerlo */
            item->activo = false;

            /* Actualizar el mapa (necesitamos el mutex de la casilla tambien) */
            pthread_mutex_lock(&mundo->mapa_mutex[item->y][item->x]);
            mundo->mapa[item->y][item->x] = CELL_PLAYER; /* El jugador sigue ahi */
            pthread_mutex_unlock(&mundo->mapa_mutex[item->y][item->x]);

            j->items_recogidos++;

            log_jugador(mundo, j->id,
                        "%s** ITEM RECOGIDO **%s '%s' - Total items: %d",
                        C_GREEN, C_RESET, item->nombre, j->items_recogidos);

            pthread_mutex_lock(&mundo->stats_mutex);
            mundo->total_items_recogidos++;
            pthread_mutex_unlock(&mundo->stats_mutex);
        } else {
            /*
             * RACE CONDITION EVITADA:
             * Otro jugador ya recogio este item mientras esperabamos.
             * Sin mutex, ambos lo habrian recogido (clonacion).
             */
            log_jugador(mundo, j->id,
                        "%s** RACE CONDITION EVITADA **%s "
                        "'%s' ya fue recogido por otro jugador.",
                        C_RED, C_RESET, item->nombre);

            pthread_mutex_lock(&mundo->stats_mutex);
            mundo->total_race_conditions_evitadas++;
            pthread_mutex_unlock(&mundo->stats_mutex);
        }

        log_jugador(mundo, j->id,
                    "Accion terminada - %sLiberando Mutex del item.%s",
                    C_GREEN, C_RESET);

        /* ---- FIN SECCION CRITICA ---- */
        pthread_mutex_unlock(&item->mutex);
        return; /* Solo intentar un item por turno */
    }
}

/* ============================================================
 * mover_jugador: Mueve al jugador a una casilla adyacente
 *
 * SECCION CRITICA: Lectura/escritura en la matriz del mapa
 * PRIMITIVA: Mutex por casilla (granularidad fina)
 * ============================================================ */
static void mover_jugador(MundoMMO *mundo, Jugador *j) {
    /* Calcular destino aleatorio adyacente */
    int dx[] = {0, 0, 1, -1};
    int dy[] = {1, -1, 0, 0};
    const char *dirs[] = {"abajo", "arriba", "derecha", "izquierda"};

    int dir = rand() % 4;
    int nx = j->x + dx[dir];
    int ny = j->y + dy[dir];

    /* Validar limites del mapa */
    if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT)
        return;

    log_jugador(mundo, j->id,
                "Movimiento %s (%d,%d)->(%d,%d) - %sSolicitando Mutex casilla destino...%s",
                dirs[dir], j->x, j->y, nx, ny, C_YELLOW, C_RESET);

    /*
     * Orden de bloqueo: siempre bloquear casilla con indice menor primero
     * para prevenir deadlocks entre jugadores que se mueven al mismo tiempo.
     */
    int old_idx = j->y * MAP_WIDTH + j->x;
    int new_idx = ny * MAP_WIDTH + nx;

    if (old_idx < new_idx) {
        pthread_mutex_lock(&mundo->mapa_mutex[j->y][j->x]);
        pthread_mutex_lock(&mundo->mapa_mutex[ny][nx]);
    } else {
        pthread_mutex_lock(&mundo->mapa_mutex[ny][nx]);
        pthread_mutex_lock(&mundo->mapa_mutex[j->y][j->x]);
    }

    /* ---- INICIO SECCION CRITICA: Movimiento ---- */
    log_jugador(mundo, j->id,
                "%sMutex casilla adquirido%s - Verificando casilla (%d,%d)...",
                C_GREEN, C_RESET, nx, ny);

    if (mundo->mapa[ny][nx] == CELL_EMPTY || mundo->mapa[ny][nx] == CELL_ITEM) {
        /* Casilla libre o con item - mover */
        bool habia_item = (mundo->mapa[ny][nx] == CELL_ITEM);

        mundo->mapa[j->y][j->x] = CELL_EMPTY;
        mundo->mapa[ny][nx] = CELL_PLAYER;

        int old_x = j->x, old_y = j->y;
        j->x = nx;
        j->y = ny;

        log_jugador(mundo, j->id,
                    "Posicion actualizada (%d,%d)->(%d,%d) - %sLiberando Mutex.%s",
                    old_x, old_y, nx, ny, C_GREEN, C_RESET);

        pthread_mutex_lock(&mundo->stats_mutex);
        mundo->total_movimientos++;
        pthread_mutex_unlock(&mundo->stats_mutex);

        /* Desbloquear antes de intentar recoger (evitar deadlock anidado) */
        if (old_idx < new_idx) {
            pthread_mutex_unlock(&mundo->mapa_mutex[ny][nx]);
            pthread_mutex_unlock(&mundo->mapa_mutex[j->y - dy[dir]][j->x - dx[dir]]);
        } else {
            pthread_mutex_unlock(&mundo->mapa_mutex[j->y - dy[dir]][j->x - dx[dir]]);
            pthread_mutex_unlock(&mundo->mapa_mutex[ny][nx]);
        }

        /* Si habia un item en la casilla destino, intentar recogerlo */
        if (habia_item) {
            intentar_recoger_item(mundo, j);
        }
        return;
    } else {
        log_jugador(mundo, j->id,
                    "Casilla (%d,%d) ocupada - %sLiberando Mutex. Movimiento cancelado.%s",
                    nx, ny, C_GREEN, C_RESET);
    }

    /* ---- FIN SECCION CRITICA ---- */
    if (old_idx < new_idx) {
        pthread_mutex_unlock(&mundo->mapa_mutex[ny][nx]);
        pthread_mutex_unlock(&mundo->mapa_mutex[j->y][j->x]);
    } else {
        pthread_mutex_unlock(&mundo->mapa_mutex[j->y][j->x]);
        pthread_mutex_unlock(&mundo->mapa_mutex[ny][nx]);
    }
}

/* ============================================================
 * hilo_jugador: Funcion principal del hilo de cada jugador
 *
 * FLUJO:
 * 1. sem_wait() en cupo_semaforo (SEMAFORO)
 * 2. Colocarse en mapa (MUTEX casilla)
 * 3. Loop: moverse, recoger items, enviar chat
 * 4. Desconexion: liberar casilla, sem_post()
 * ============================================================ */
void *hilo_jugador(void *arg) {
    JugadorArgs *jargs = (JugadorArgs *)arg;
    MundoMMO *mundo = jargs->mundo;
    int idx = jargs->jugador_idx;
    Jugador *j = &mundo->jugadores[idx];
    free(jargs); /* Liberar argumento dinamico */

    /* Retraso aleatorio para simular llegada escalonada */
    usleep((rand() % 2000000) + 500000); /* 0.5 - 2.5 segundos */

    if (!mundo->servidor_activo) {
        pthread_exit(NULL);
    }

    /* ============================================================
     * PASO 1: CONEXION CON SEMAFORO
     * sem_wait() decrementa el semaforo. Si es 0, el hilo se bloquea.
     * ============================================================ */
    log_jugador(mundo, j->id,
                "Solicita conexion al servidor - %ssem_wait() en cupo...%s",
                C_YELLOW, C_RESET);

    pthread_mutex_lock(&mundo->stats_mutex);
    mundo->total_intentos_conexion++;
    pthread_mutex_unlock(&mundo->stats_mutex);

    /* Intentar entrar (se bloquea si no hay cupo) */
    int sem_val;
    sem_getvalue(&mundo->cupo_semaforo, &sem_val);

    if (sem_val <= 0) {
        log_jugador(mundo, j->id,
                    "%sSERVIDOR LLENO%s - Entrando a sala de espera (bloqueado en sem_wait)...",
                    C_RED, C_RESET);

        pthread_mutex_lock(&mundo->stats_mutex);
        mundo->total_conexiones_rechazadas++;
        pthread_mutex_unlock(&mundo->stats_mutex);
    }

    sem_wait(&mundo->cupo_semaforo);

    if (!mundo->servidor_activo) {
        sem_post(&mundo->cupo_semaforo);
        pthread_exit(NULL);
    }

    /* Conexion exitosa */
    pthread_mutex_lock(&mundo->conexion_mutex);
    mundo->jugadores_conectados++;
    j->conectado = true;
    j->activo = true;
    pthread_mutex_unlock(&mundo->conexion_mutex);

    log_jugador(mundo, j->id,
                "%ssem_wait() exitoso - CONECTADO%s (Cupo: %d/%d)",
                C_GREEN, C_RESET, mundo->jugadores_conectados, MAX_PLAYERS);

    /* ============================================================
     * PASO 2: COLOCARSE EN EL MAPA (Mutex de casilla)
     * ============================================================ */
    int start_x, start_y;
    bool colocado = false;
    while (!colocado && mundo->servidor_activo) {
        start_x = rand() % MAP_WIDTH;
        start_y = rand() % MAP_HEIGHT;

        pthread_mutex_lock(&mundo->mapa_mutex[start_y][start_x]);
        if (mundo->mapa[start_y][start_x] == CELL_EMPTY) {
            mundo->mapa[start_y][start_x] = CELL_PLAYER;
            j->x = start_x;
            j->y = start_y;
            colocado = true;
            log_jugador(mundo, j->id,
                        "Spawn en posicion (%d,%d)", start_x, start_y);
        }
        pthread_mutex_unlock(&mundo->mapa_mutex[start_y][start_x]);
    }

    /* ============================================================
     * PASO 3: GAME LOOP DEL JUGADOR
     * Acciones aleatorias: moverse (70%), chat (20%), idle (10%)
     * ============================================================ */
    int acciones = 0;
    while (mundo->servidor_activo && acciones < 20) {
        int accion = rand() % 10;

        if (accion < 7) {
            /* Moverse (y posiblemente recoger item) */
            mover_jugador(mundo, j);
        } else if (accion < 9) {
            /* Enviar mensaje al chat */
            enviar_chat(mundo, j->id,
                        mensajes_chat[rand() % num_mensajes]);
        }
        /* else: idle */

        acciones++;
        usleep((rand() % 500000) + 200000); /* 0.2 - 0.7 segundos entre acciones */
    }

    /* ============================================================
     * PASO 4: DESCONEXION - Liberar recursos y sem_post()
     * ============================================================ */
    log_jugador(mundo, j->id,
                "Iniciando desconexion...");

    /* Liberar casilla del mapa */
    if (j->x >= 0 && j->y >= 0) {
        pthread_mutex_lock(&mundo->mapa_mutex[j->y][j->x]);
        mundo->mapa[j->y][j->x] = CELL_EMPTY;
        pthread_mutex_unlock(&mundo->mapa_mutex[j->y][j->x]);
    }

    pthread_mutex_lock(&mundo->conexion_mutex);
    mundo->jugadores_conectados--;
    j->conectado = false;
    j->activo = false;
    pthread_mutex_unlock(&mundo->conexion_mutex);

    /* sem_post() libera un slot para otro jugador */
    sem_post(&mundo->cupo_semaforo);

    log_jugador(mundo, j->id,
                "%ssem_post() - DESCONECTADO%s (Cupo: %d/%d) Items recogidos: %d",
                C_GREEN, C_RESET, mundo->jugadores_conectados, MAX_PLAYERS,
                j->items_recogidos);

    pthread_exit(NULL);
}
