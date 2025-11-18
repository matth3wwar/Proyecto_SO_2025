/****************************************************
* Pontificia Universidad Javeriana
* Autores: Mateo David Guerra y Ángel Daniel García Santana
* Fecha: Noviembre 2025
* Materia: Sistemas Operativos
* Proyecto: Sistema de Reservas - Cliente
* Tema: Implementación Agente de Reserva
*****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define MAX_LINEA 512

typedef struct {
    char nombre[64];
    char pipe_receptor[256];
    char pipe_respuesta[256];
    int num_solicitudes;
    int demora_seg;        // demora entre solicitudes
    int max_personas;
    int hora_min;
    int hora_max;
} ArgsAgente;

/* Hilo lector de la respuesta: abre el pipe_respuesta en O_RDONLY y lee líneas. */
static void *hilo_lector_respuestas(void *v) {
    ArgsAgente *args = (ArgsAgente*) v;
    // Abrir pipe de respuesta (bloqueante hasta que haya quien escriba)
    int fd = open(args->pipe_respuesta, O_RDONLY);
    if (fd < 0) {
        printf("[Agente %s] Error abriendo pipe de respuesta '%s'\n", args->nombre, args->pipe_respuesta);
        return (void*) -1;
    }

    FILE *fp = fdopen(fd, "r");
    if (!fp) {
        printf("[Agente %s] fdopen fallo\n", args->nombre);
        close(fd);
        return (void*) -1;
    }

    char linea[MAX_LINEA];
    while (fgets(linea, sizeof(linea), fp) != NULL) {
        // Trim final newline
        size_t L = strlen(linea);
        if (L > 0 && linea[L-1] == '\n') linea[L-1] = '\0';
        printf("[Agente %s] Mensaje recibido en %s: %s\n", args->nombre, args->pipe_respuesta, linea);
    }

    // Si llegamos aquí es porque el escritor cerró o hubo EOF
    fclose(fp);
    return NULL;
}

/* Función para enviar una línea al pipe receptor (abre, escribe y cierra). */
static int enviar_linea_pipe_receptor(const char *pipe_receptor, const char *linea) {
    // Abrir para escritura. Si no hay lector (controlador), open bloqueará hasta que exista uno.
    int fd = open(pipe_receptor, O_WRONLY);
    if (fd < 0) {
        printf("[Agente] Error abriendo pipe receptor '%s'\n", pipe_receptor);
        return -1;
    }

    size_t len = strlen(linea);
    ssize_t w = write(fd, linea, len);
    if (w != (ssize_t)len) {
        printf("[Agente] Error al escribir en pipe receptor\n");
        close(fd);
        return -1;
    }

    // Asegurar newline final (el caller puede ya tenerlo)
    // close para indicar fin de escritura (el receptor recibirá la línea)
    close(fd);
    return 0;
}

/* Genera un entero aleatorio en [a,b] */
static int rand_range(int a, int b) {
    if (b < a) return a;
    return (rand() % (b - a + 1)) + a;
}

int main(int argc, char *argv[]) {
    ArgsAgente args;
    memset(&args, 0, sizeof(args));
    strncpy(args.nombre, "AgenteX", sizeof(args.nombre)-1);
    strncpy(args.pipe_receptor, "PIPE_RECEPTOR", sizeof(args.pipe_receptor)-1);
    args.num_solicitudes = 3;
    args.demora_seg = 2;
    args.max_personas = 6;
    args.hora_min = 7;
    args.hora_max = 19;

    // Parseo opcional de argumentos
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i+1 < argc) {
            strncpy(args.nombre, argv[++i], sizeof(args.nombre)-1);
        } else if (strcmp(argv[i], "-p") == 0 && i+1 < argc) {
            strncpy(args.pipe_receptor, argv[++i], sizeof(args.pipe_receptor)-1);
        } else if (strcmp(argv[i], "-r") == 0 && i+1 < argc) {
            args.num_solicitudes = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i+1 < argc) {
            args.demora_seg = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--maxp") == 0 && i+1 < argc) {
            args.max_personas = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--hmin") == 0 && i+1 < argc) {
            args.hora_min = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--hmax") == 0 && i+1 < argc) {
            args.hora_max = atoi(argv[++i]);
        } else {
            printf("Uso: %s [-n nombre] [-p pipeReceptor] [-r numReq] [-d demoraSeg] [--maxp N] [--hmin H] [--hmax H]\n", argv[0]);
            return 0;
        }
    }

    srand((unsigned int)(time(NULL) ^ getpid()));

    // Crear pipe de respuesta propio
    pid_t pid = getpid();
    snprintf(args.pipe_respuesta, sizeof(args.pipe_respuesta), "PIPE_RESP_%d", (int)pid);

    // Si ya existe, intentar borrarlo y recrear
    unlink(args.pipe_respuesta);
    if (mkfifo(args.pipe_respuesta, 0666) < 0) {
        if (errno != EEXIST) {
            printf("[Agente %s] No se pudo crear pipe de respuesta '%s'\n", args.nombre, args.pipe_respuesta);
            return 1;
        }
    }

    printf("[Agente %s] Pipe de respuesta creado: %s\n", args.nombre, args.pipe_respuesta);

    // Arrancar hilo lector de respuestas
    pthread_t hilo_lector;
    if (pthread_create(&hilo_lector, NULL, hilo_lector_respuestas, &args) != 0) {
        printf("[Agente %s] Error creando hilo lector\n", args.nombre);
        unlink(args.pipe_respuesta);
        return 1;
    }

    // Preparar mensaje de registro: "REG,NOMBRE,PIPE_RESPUESTA\n"
    char linea[MAX_LINEA];
    snprintf(linea, sizeof(linea), "REG,%s,%s\n", args.nombre, args.pipe_respuesta);

    printf("[Agente %s] Registrándose en %s ...\n", args.nombre, args.pipe_receptor);
    if (enviar_linea_pipe_receptor(args.pipe_receptor, linea) != 0) {
        printf("[Agente %s] Falló el registro (no se pudo escribir en %s)\n", args.nombre, args.pipe_receptor);
        // intentar limpiar y salir
        // signal al lector cerrar: para cerrar lector, abrir el pipe_respuesta en O_WRONLY y cerrar
        int fdtemp = open(args.pipe_respuesta, O_WRONLY | O_NONBLOCK);
        if (fdtemp >= 0) close(fdtemp);
        pthread_join(hilo_lector, NULL);
        unlink(args.pipe_respuesta);
        return 1;
    }
    printf("[Agente %s] Registro enviado.\n", args.nombre);

    /* Enviar solicitudes */
    for (int i = 0; i < args.num_solicitudes; i++) {
        // Generar datos de la solicitud
        char familia[64];
        snprintf(familia, sizeof(familia), "Familia_%d_%d", (int)pid, i+1);

        int hora = rand_range(args.hora_min, args.hora_max);
        int personas = rand_range(1, args.max_personas);

        snprintf(linea, sizeof(linea), "REQ,%s,%d,%d\n", familia, hora, personas);
        printf("[Agente %s] Enviando solicitud %d/%d: %s", args.nombre, i+1, args.num_solicitudes, linea);

        if (enviar_linea_pipe_receptor(args.pipe_receptor, linea) != 0) {
            printf("[Agente %s] Error enviando solicitud %d\n", args.nombre, i+1);
        } else {
            printf("[Agente %s] Solicitud enviada.\n", args.nombre);
        }

        sleep(args.demora_seg);
    }

    /* Esperar un poco para recibir respuestas, luego terminar */
    printf("[Agente %s] Enviadas todas las solicitudes. Esperando respuestas (5s)...\n", args.nombre);
    sleep(5);

    // Cerrar hilo lector: para forzar EOF en lector, abrir el pipe_respuesta para escritura y cerrarlo
    // (esto hará que el fd en lector reciba EOF después de cerrar)
    int fds = open(args.pipe_respuesta, O_WRONLY | O_NONBLOCK);
    if (fds >= 0) close(fds);

    pthread_join(hilo_lector, NULL);

    // Borrar pipe de respuesta
    unlink(args.pipe_respuesta);
    printf("[Agente %s] Finalizado. Pipe de respuesta eliminado: %s\n", args.nombre, args.pipe_respuesta);
    return 0;
}

