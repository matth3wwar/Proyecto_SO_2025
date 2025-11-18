/**************
* Pontificia Universidad Javeriana
* Autores: Mateo David Guerra y Ángel Daniel García Santana
* Fecha: 17 Noviembre 2025
* Materia: Sistemas Operativos
* Proyecto: Sistema de Reservas - Cliente
***************/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>

#define BUFFER_SIZE 256
#define RESPUESTA_SIZE 512

// Estructura para mensajes al servidor
typedef struct {
    char nombre_agente[50];
    char pipe_respuesta[100];
    char familia[50];
    int hora_solicitada;
    int num_personas;
    char tipo_mensaje[20]; // "REGISTRO" o "RESERVA"
} MensajeAgente;

// Variables globales
int fd_envio;
int fd_respuesta;
char nombre_agente[50];
char pipe_respuesta[100];

// Función para manejar señales de terminación
void manejar_senal(int sig) {
    printf("\nAgente %s terminado por señal.\n", nombre_agente);
    close(fd_envio);
    close(fd_respuesta);
    unlink(pipe_respuesta);
    exit(0);
}

// Función para registrar el agente con el controlador
int registrar_agente(char* pipe_controlador) {
    // Crear pipe único para respuestas
    snprintf(pipe_respuesta, sizeof(pipe_respuesta), "/tmp/agente_%s_%d", nombre_agente, getpid());
    
    // Crear el pipe nominal para respuestas
    if (mkfifo(pipe_respuesta, 0666) == -1) {
        perror("Error creando pipe de respuesta");
        return -1;
    }

    // Abrir pipe hacia el controlador (escritura)
    fd_envio = open(pipe_controlador, O_WRONLY);
    if (fd_envio == -1) {
        perror("Error abriendo pipe del controlador");
        unlink(pipe_respuesta);
        return -1;
    }

    // Preparar mensaje de registro
    MensajeAgente registro;
    strncpy(registro.nombre_agente, nombre_agente, sizeof(registro.nombre_agente));
    strncpy(registro.pipe_respuesta, pipe_respuesta, sizeof(registro.pipe_respuesta));
    strncpy(registro.tipo_mensaje, "REGISTRO", sizeof(registro.tipo_mensaje));
    
    // Enviar registro
    if (write(fd_envio, &registro, sizeof(registro)) == -1) {
        perror("Error enviando registro al controlador");
        close(fd_envio);
        unlink(pipe_respuesta);
        return -1;
    }

    printf("Agente %s registrado. Esperando hora actual...\n", nombre_agente);
    return 0;
}

// Función para recibir la hora actual del controlador
int recibir_hora_actual() {
    // Abrir pipe de respuesta (lectura)
    fd_respuesta = open(pipe_respuesta, O_RDONLY);
    if (fd_respuesta == -1) {
        perror("Error abriendo pipe de respuesta");
        return -1;
    }

    int hora_actual;
    if (read(fd_respuesta, &hora_actual, sizeof(hora_actual)) == -1) {
        perror("Error recibiendo hora actual");
        close(fd_respuesta);
        return -1;
    }

    printf("Hora actual del sistema: %d\n", hora_actual);
    close(fd_respuesta);
    return hora_actual;
}

// Función para procesar archivo de solicitudes
void procesar_solicitudes(char* archivo_solicitudes, int hora_actual) {
    FILE* archivo = fopen(archivo_solicitudes, "r");
    if (!archivo) {
        perror("Error abriendo archivo de solicitudes");
        return;
    }

    char linea[BUFFER_SIZE];
    char familia[50];
    int hora_solicitada, num_personas;
    
    printf("Iniciando procesamiento de solicitudes...\n");

    while (fgets(linea, sizeof(linea), archivo)) {
        // Parsear línea CSV
        if (sscanf(linea, "%[^,],%d,%d", familia, &hora_solicitada, &num_personas) != 3) {
            printf("Error: Formato inválido en línea: %s", linea);
            continue;
        }

        // Validar hora solicitada
        if (hora_solicitada < hora_actual) {
            printf("SOLICITUD: Familia %s, Hora %d, Personas %d -> RECHAZADA (hora anterior a actual)\n", 
                   familia, hora_solicitada, num_personas);
            sleep(2);
            continue;
        }

        // Preparar mensaje de reserva
        MensajeAgente solicitud;
        strncpy(solicitud.nombre_agente, nombre_agente, sizeof(solicitud.nombre_agente));
        strncpy(solicitud.pipe_respuesta, pipe_respuesta, sizeof(solicitud.pipe_respuesta));
        strncpy(solicitud.familia, familia, sizeof(solicitud.familia));
        strncpy(solicitud.tipo_mensaje, "RESERVA", sizeof(solicitud.tipo_mensaje));
        solicitud.hora_solicitada = hora_solicitada;
        solicitud.num_personas = num_personas;

        // Enviar solicitud
        if (write(fd_envio, &solicitud, sizeof(solicitud)) == -1) {
            perror("Error enviando solicitud");
            break;
        }

        printf("SOLICITUD ENVIADA: Familia %s, Hora %d, Personas %d\n", 
               familia, hora_solicitada, num_personas);

        // Esperar respuesta
        fd_respuesta = open(pipe_respuesta, O_RDONLY);
        if (fd_respuesta == -1) {
            perror("Error abriendo pipe para respuesta");
            break;
        }

        char respuesta[RESPUESTA_SIZE];
        int bytes_leidos = read(fd_respuesta, respuesta, sizeof(respuesta)-1);
        if (bytes_leidos > 0) {
            respuesta[bytes_leidos] = '\0';
            printf("RESPUESTA: %s\n", respuesta);
        } else {
            printf("Error recibiendo respuesta\n");
        }

        close(fd_respuesta);
        sleep(2); // Esperar 2 segundos entre solicitudes
    }

    fclose(archivo);
}

int main(int argc, char *argv[]) {
    char* archivo_solicitudes = NULL;
    char* pipe_controlador = NULL;
    int opt;

    // Configurar manejador de señales
    signal(SIGINT, manejar_senal);
    signal(SIGTERM, manejar_senal);

    // Parsear argumentos
    while ((opt = getopt(argc, argv, "s:a:p:")) != -1) {
        switch (opt) {
            case 's':
                strncpy(nombre_agente, optarg, sizeof(nombre_agente)-1);
                break;
            case 'a':
                archivo_solicitudes = optarg;
                break;
            case 'p':
                pipe_controlador = optarg;
                break;
            default:
                fprintf(stderr, "Uso: %s -s nombre_agente -a archivo_solicitudes -p pipe_controlador\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Validar parámetros requeridos
    if (!nombre_agente[0] || !archivo_solicitudes || !pipe_controlador) {
        fprintf(stderr, "Error: Faltan parámetros requeridos\n");
        fprintf(stderr, "Uso: %s -s nombre_agente -a archivo_solicitudes -p pipe_controlador\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    printf("Iniciando Agente de Reserva: %s\n", nombre_agente);
    printf("Archivo de solicitudes: %s\n", archivo_solicitudes);
    printf("Pipe del controlador: %s\n", pipe_controlador);

    // Registrar agente con el controlador
    if (registrar_agente(pipe_controlador) == -1) {
        exit(EXIT_FAILURE);
    }

    // Recibir hora actual del sistema
    int hora_actual = recibir_hora_actual();
    if (hora_actual == -1) {
        close(fd_envio);
        unlink(pipe_respuesta);
        exit(EXIT_FAILURE);
    }

    // Procesar archivo de solicitudes
    procesar_solicitudes(archivo_solicitudes, hora_actual);

    // Limpieza final
    printf("Agente %s termina.\n", nombre_agente);
    close(fd_envio);
    unlink(pipe_respuesta);

    return 0;
}
