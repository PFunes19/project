# Simulación de Servidor MMO Concurrente

Proyecto Final - Sistemas Operativos
Universidad de las Américas Puebla
Primavera 2026

## Dependencias

Para compilar y ejecutar el proyecto se necesita:

- Linux o WSL
- GCC
- Biblioteca pthread

Para verificar que GCC está instalado:

```bash
gcc --version
```

Si no está instalado en Ubuntu o WSL:

```bash
sudo apt update
sudo apt install build-essential
```

## Instrucciones de compilación

Compilar con:

```bash
gcc -Wall -Wextra -std=gnu11 -o mmo_server src/main.c src/mundo.c src/jugador.c src/entorno.c -lpthread
```


## Instrucciones de ejecución

Para ejecutar el programa:

```bash
./mmo_server
```

Para guardar la salida de la ejecución en un archivo log:

```bash
mkdir -p logs
./mmo_server | tee logs/ejecucion_general.log
```

## Resultados esperados

Al ejecutar el programa, se espera observar en consola una simulación de un servidor MMO concurrente.

El sistema debe mostrar:

- Inicio del servidor y configuración inicial.
- Jugadores intentando conectarse.
- Jugadores esperando cuando el servidor está lleno.
- Uso de `sem_wait()` y `sem_post()` para controlar el cupo del servidor.
- Movimiento de jugadores en el mapa compartido.
- Uso de mutex para proteger casillas del mapa e ítems.
- Recolección de ítems.
- Mensajes de chat procesados con variable de condición.
- Respawn de ítems por el hilo de entorno.
- Resumen final con métricas de la simulación.

Las métricas finales incluyen:

```text
Intentos de conexión
Conexiones rechazadas o en espera
Movimientos totales
Ítems recogidos
Mensajes de chat
Condiciones de carrera evitadas
```
