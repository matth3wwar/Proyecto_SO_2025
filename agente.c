/*******************************************************
* Pontificia Universidad Javeriana
* Autores: Mateo David Guerra y Ángel Daniel García Santana
* Fecha: Noviembre 2025
* Materia: Sistemas Operativos
* Proyecto: Sistema de Reservas - Agente
* Tema: Uso de Named Pipes, Threads y programación concurrente
*******************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

#define BUFFER_SIZE 512
#define MAX_FAMILIA 50
#define MAX_AGENTE 50

/* Estructura mensajes */
typedef struct {
	// "REGISTRO" o "RESERVA"
	char tipo[20];
	char nombre_agente[MAX_AGENTE];
	char pipe_respuesta[100];
	char familia[MAX_FAMILIA];
	int hora_solicitada;
	int num_personas;
} MensajeAgente;

/* Variables globales */
volatile int running = 1;
char pipe_respuesta_agente[100];

/* Manejar señal de terminación */
void manejar_senal(int sig) {
	running = 0;
	printf("\nAgente terminando...\n");
}

/* Función para enviar mensaje al controlador */
int enviar_mensaje(const char* pipe_controlador, MensajeAgente* mensaje) {
	int fd = open(pipe_controlador, O_WRONLY);
	if (fd == -1) {
		if (errno != ENOENT) {
			perror("Error abriendo pipe del controlador");
		}
		return -1;
	}

	ssize_t bytes_escritos = write(fd, mensaje, sizeof(MensajeAgente));
	close(fd);

	if (bytes_escritos != sizeof(MensajeAgente)) {
		fprintf(stderr, "Error: No se pudo enviar mensaje completo\n");
		return -1;
	}

	return 0;
}

/* Función para recibir respuesta del controlador */
int recibir_respuesta(char* buffer, size_t buffer_size) {
	int fd = open(pipe_respuesta_agente, O_RDONLY);
	if (fd == -1) {
		perror("Error abriendo pipe de respuesta");
		return -1;
	}

	ssize_t bytes_leidos = read(fd, buffer, buffer_size - 1);
	close(fd);

	if (bytes_leidos > 0) {
		buffer[bytes_leidos] = '\0';
		return 0;
	}

	return -1;
}

int main(int argc, char *argv[]) {
	char nombre_agente[MAX_AGENTE] = "";
	char archivo_solicitudes[100] = "";
	char pipe_controlador[100] = "";
	int hora_actual = 0;

	// Configurar manejador de señales
	signal(SIGINT, manejar_senal);
	signal(SIGTERM, manejar_senal);

	// Parseo argumentos
	int i = 1;
	while (i < argc) {
		if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			strncpy(nombre_agente, argv[i + 1], sizeof(nombre_agente) - 1);
			i += 2;
		} else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
			strncpy(archivo_solicitudes, argv[i + 1], sizeof(archivo_solicitudes) - 1);
			i += 2;
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			strncpy(pipe_controlador, argv[i + 1], sizeof(pipe_controlador) - 1);
			i += 2;
		} else {
			fprintf(stderr, "Uso: %s -s nombre_agente -a archivo_solicitudes -p pipe_controlador\n\n", argv[0]);
			fprintf(stderr, "Ejemplo: %s -s AgenteA -a solicitudes.csv -p /tmp/pipe_controlador\n", argv[0]);
			return 1;
		}
	}

	// Validación parámetros
	if (strlen(nombre_agente) == 0 || strlen(archivo_solicitudes) == 0 || strlen(pipe_controlador) == 0) {
		fprintf(stderr, "Error: Faltan parámetros requeridos\n");
		fprintf(stderr, "Uso: %s -s nombre_agente -a archivo_solicitudes -p pipe_controlador\n", argv[0]);
		return 1;
	}

	printf("=== INICIANDO AGENTE DE RESERVA ===\n");

	printf("Nombre agente: %s\n", nombre_agente);
	printf("Archivo solicitudes: %s\n", archivo_solicitudes);
	printf("Pipe controlador: %s\n", pipe_controlador);

	// Crear pipe de que comunica unicamente con este agente
	snprintf(pipe_respuesta_agente, sizeof(pipe_respuesta_agente), "/tmp/respuesta_%s_%d", nombre_agente, getpid());

	if (mkfifo(pipe_respuesta_agente, 0666) == -1 && errno != EEXIST) {
		perror("Error creando pipe de respuesta");
		return 1;
	}

	printf("Pipe de respuesta creado: %s\n", pipe_respuesta_agente);

	/* REGISTRAR AGENTE */
	MensajeAgente registro;
	strncpy(registro.tipo, "REGISTRO", sizeof(registro.tipo));
	strncpy(registro.nombre_agente, nombre_agente, sizeof(registro.nombre_agente));
	strncpy(registro.pipe_respuesta, pipe_respuesta_agente, sizeof(registro.pipe_respuesta));

	printf("Registrando agente con controlador...\n");
	if (enviar_mensaje(pipe_controlador, &registro) == -1) {
		fprintf(stderr, "Error: No se pudo registrar con el controlador\n");
		unlink(pipe_respuesta_agente);
		return 1;
	}

	/* RECIBIR HORA ACTUAL */
	char buffer[BUFFER_SIZE];
	if (recibir_respuesta(buffer, sizeof(buffer)) == 0) {
		hora_actual = atoi(buffer);
		printf("Hora actual recibida del controlador: %d\n", hora_actual);
	} else {
		fprintf(stderr, "Error: No se pudo recibir hora actual del controlador\n");
		unlink(pipe_respuesta_agente);
		return 1;
	}

	/* PROCESAMIENTO ARCHIVO DE SOLICITUDES */
	FILE *archivo = fopen(archivo_solicitudes, "r");
	if (!archivo) {
		perror("Error abriendo archivo de solicitudes");
		unlink(pipe_respuesta_agente);
		return 1;
	}

	char linea[BUFFER_SIZE];
	int num_solicitud = 0;

	printf("\n=== INICIANDO PROCESAMIENTO DE SOLICITUDES ===\n");

	while (fgets(linea, sizeof(linea), archivo) && running) {
		// Limpiar línea
		linea[strcspn(linea, "\n")] = 0;

		// Saltar líneas vacías
		if (strlen(linea) == 0) continue;

		// Parsear línea CSV con el formato [familia,hora,personas]
		char familia[MAX_FAMILIA];
		int hora_solicitada, num_personas;

		if (sscanf(linea, "%[^,],%d,%d", familia, &hora_solicitada, &num_personas) != 3) {
			fprintf(stderr, "Error: Formato inválido en línea: %s\n", linea);
			continue;
		}

		num_solicitud++;

		//validación hora solicitada vs HORA ACTUALvs hora actual */
		if (hora_solicitada < hora_actual) {
			printf("SOLICITUD %d: Familia %s - RECHAZADA (hora %d ya pasó, hora actual: %d)\n", num_solicitud, familia, hora_solicitada, hora_actual);
			sleep(2);
			continue;
		}

		/* preparación y envío del código de reserva */
		MensajeAgente solicitud;

		strncpy(solicitud.tipo, "RESERVA", sizeof(solicitud.tipo));
		strncpy(solicitud.nombre_agente, nombre_agente, sizeof(solicitud.nombre_agente));
		strncpy(solicitud.pipe_respuesta, pipe_respuesta_agente, sizeof(solicitud.pipe_respuesta));
		strncpy(solicitud.familia, familia, sizeof(solicitud.familia));
		solicitud.hora_solicitada = hora_solicitada;
		solicitud.num_personas = num_personas;

		printf("SOLICITUD %d: Familia %s, Hora %d, Personas %d -> Enviando...\n", num_solicitud, familia, hora_solicitada, num_personas);

		if (enviar_mensaje(pipe_controlador, &solicitud) == -1) {
			fprintf(stderr, "Error enviando solicitud %d\n", num_solicitud);
			continue;
		}

		// Esperar para luego mostrar la respuesta de lo recibido
		if (recibir_respuesta(buffer, sizeof(buffer)) == 0) {
			printf("RESPUESTA %d: %s\n", num_solicitud, buffer);
		} else {
			printf("RESPUESTA %d: Error recibiendo respuesta\n", num_solicitud);
		}

		// El código está diseñado para esperar 2 segundos entre cada solicitud
		sleep(2);
	}

	fclose(archivo);

	/* FINALIZACIÓN */
	printf("\n=== FINALIZANDO AGENTE ===\n");
	printf("Agente %s termina. Total solicitudes procesadas: %d\n", nombre_agente, num_solicitud);

	// Limpiar pipe antes de cerrar el código
	unlink(pipe_respuesta_agente);

	return 0;
}
