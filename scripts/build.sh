#!/bin/bash

# Script para compilar el proyecto final de Sistemas Operativos
# Simulación de Servidor MMO Concurrente

echo "Compilando proyecto..."

gcc -Wall -Wextra -std=gnu11 -o mmo_server src/main.c src/mundo.c src/jugador.c src/entorno.c -lpthread

if [ $? -eq 0 ]; then
    echo "Compilación exitosa."
    echo "Ejecutable generado: ./mmo_server"
else
    echo "Error durante la compilación."
    exit 1
fi
