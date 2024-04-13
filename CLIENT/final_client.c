#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <openssl/md5.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include <pthread.h>
#include <stdarg.h>
#include "manejoCache.h"

#define MAXIMUM_SIZE_RESPONSE 1024
#define PORT 8080
#define SOCKET_ADDRESS struct sockaddr
#define MAX_PATH_LENGTH 256
#define MAX_URL_LENGTH 256

long ttl_global = 0; // Variable global para almacenar el tiempo de vida (TTL) del caché.

struct FileNameAndExtension {
    char file_name[256];
    char file_extension[256];
};


struct pathAndRequest {
    char path[256];
    char server_url[256];
    char request[3000];
};

int menu() {
    int option;
    printf("Menu\n");
    printf("1. If you want to do a GET method.\n");
    printf("2. If you want to do a HEAD method.\n");
    printf("3. EXIT.\n");
    printf("Choose an option: ");
    scanf("%d", &option);
    return option;
}

int doTheConnectionWithTheProxy() {
    int socket_file_descriptor;
    struct sockaddr_in proxy_address;

    socket_file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_file_descriptor == -1) {
        printf("Socket creation failed, please verify the failure.\n");
        exit(EXIT_FAILURE);
    } else {
        printf("Socket was created successfully.\n");
    }
    bzero(&proxy_address, sizeof(proxy_address));

    proxy_address.sin_family = AF_INET;
    // proxy_address.sin_addr.s_addr = inet_addr("127.0.0.1");
    // proxy_address.sin_addr.s_addr = inet_addr("52.0.250.201");
    proxy_address.sin_addr.s_addr = inet_addr("44.202.61.119");
    proxy_address.sin_port = htons(PORT);

    if (connect(socket_file_descriptor, (SOCKET_ADDRESS *)&proxy_address, sizeof(proxy_address)) != 0) {
        printf("The client wasn't connected successfully to the proxy, please verify the failure.\n");
        exit(EXIT_FAILURE);
    } else {
        printf("The client was connected successfully to the proxy.\n");
    }
    return socket_file_descriptor;
}

struct FileNameAndExtension separateLastPart(const char *path) {
    struct FileNameAndExtension resultNameAndExtetion;

    char *last_slash = strrchr(path, '/');
    if (last_slash != NULL) {
        char *last_part = last_slash + 1;
        char *last_dot = strrchr(last_part, '.');
        if (last_dot != NULL) {
            strncpy(resultNameAndExtetion.file_name, last_part, last_dot - last_part);
            resultNameAndExtetion.file_name[last_dot - last_part] = '\0';
            strcpy(resultNameAndExtetion.file_extension, last_dot + 1);
        } else {
            strcpy(resultNameAndExtetion.file_name, last_part);
            resultNameAndExtetion.file_extension[0] = '\0';
        }
    } else {
        strcpy(resultNameAndExtetion.file_name, path);
        resultNameAndExtetion.file_extension[0] = '\0';
    }
    return resultNameAndExtetion;
}




struct pathAndRequest createTheRequest(const char *method, const char *server_url) {
    char path[MAX_PATH_LENGTH];
    char server_url_temp[MAX_URL_LENGTH];
    strcpy(server_url_temp, server_url); // Copiamos la URL original a una variable temporal

    // Buscamos la última aparición de ':', que indicaría el puerto en la URL
    char *port_separator = strrchr(server_url_temp, ':');
    char *path_separator = strchr(server_url_temp, '/');
    if (port_separator != NULL && (path_separator == NULL || port_separator < path_separator)) {
        // Si se encuentra ':', eliminamos el puerto de la URL y copiamos la ruta
        *port_separator = '\0';
        strcpy(path, path_separator);
    } else {
        // Si no se encuentra ':', no hay puerto en la URL, establecemos la ruta predeterminada
        strcpy(path, "/");
    }

    // Eliminamos el puerto de la URL si existe
    char *port_str = strrchr(server_url_temp, ':');
    if (port_str != NULL) {
        *port_str = '\0';
    }

    // Crear la solicitud HTTP con el método, la URL del servidor y la ruta
    char request[MAXIMUM_SIZE_RESPONSE];
    // snprintf(request, MAXIMUM_SIZE_RESPONSE, "%s %s%s HTTP/1.1\r\n\r\n", method, server_url_temp, path);
    snprintf(request, MAXIMUM_SIZE_RESPONSE, "GET / HTTP/1.1\r\nHost: 3.219.115.240\r\nConnection: close\r\n\r\n");

    // Estructura de resultado
    struct pathAndRequest result_struct;
    strcpy(result_struct.path, path);
    strcpy(result_struct.server_url, server_url_temp);
    strcpy(result_struct.request, request);

    return result_struct;
}

void createAndFillTheLogFile(const char *log_file_path, const char *request, const char *response) {

    char *requestModified = malloc(strlen(request) + 1);
    char *initialResponse = malloc(strlen(response) + 1);
    char *responseModified = malloc(strlen(response) + 1);
    
    strcpy(requestModified, request);
    strcpy(initialResponse, response);
    strcpy(responseModified, response);
    
    if (initialResponse == NULL || responseModified == NULL) {
        printf("Error: Failed to allocate memory for the response buffers.\n");
        exit(EXIT_FAILURE);
    }

    // Abrir el archivo de log para escritura
    FILE *log_file = fopen(log_file_path, "a");
    if (log_file == NULL){
        log_file = fopen(log_file_path, "w");
    }
    else {
        log_file = fopen(log_file_path, "a");
    }

    size_t request_length = strlen(requestModified);
    if (request_length >= 4 && strcmp(requestModified + request_length - 4, "\r\n\r\n") == 0) {
        requestModified[request_length - 4] = '\0'; // Reemplazar los últimos 4 caracteres por el carácter nulo
    }
    
    // Buscar la fecha y la hora en la respuesta del servidor
    char *date_time = malloc(32 * sizeof(char)); // Asignar memoria dinámicamente para almacenar la fecha y la hora
    char *date_header = strstr(initialResponse, "date:");
    char *Date_header = strstr(initialResponse, "Date:");

    // Verificar si se encontró alguno de los encabezados
    if (date_header != NULL || Date_header != NULL) {
        // Se encontró el encabezado de fecha y hora
        char *date_start;
        if (date_header != NULL) {
            date_start = strchr(date_header, ' '); // Buscar el primer espacio después de "date:"
        } else {
            date_start = strchr(Date_header, ' '); // Buscar el primer espacio después de "Date:"
        }
        
        if (date_start != NULL) {
            // Avanzar al primer carácter después del espacio
            date_start++;
            // Buscar el final de la línea para obtener la fecha y la hora
            char *date_end = strchr(date_start, '\r');
            if (date_end != NULL) {
                *date_end = '\0'; // Terminar la cadena en el final de la línea
                // Copiar la fecha y la hora en la variable
                strcpy(date_time, date_start);
            }
        }
    } else {
        printf("No se encontró el encabezado de fecha y hora en la respuesta del servidor.\n");
    }

    printf("date_time: %s\n", date_time);

    // Eliminar los saltos de línea de la respuesta del servidor
    for (int i = 0; i < strlen(responseModified); i++) {
        if (responseModified[i] == '\n' || responseModified[i] == '\r') {
            responseModified[i] = ' ';
        }
    }

    // Calcular la longitud necesaria para log_entry
    int fullLineOfLogEntry = strlen(date_time) + strlen(requestModified) + strlen(responseModified) + 3;

    // int fullLineOfLogEntry = strlen(date_time) + strlen(request) + strlen(response) + 1; // Modificar la línea para incluir date_time

    char *log_entry = malloc(fullLineOfLogEntry);

    snprintf(log_entry, fullLineOfLogEntry, "%s %s %s", date_time, requestModified, responseModified);
    fprintf(log_file, "%s\n", log_entry);

    // Verificar si el archivo está abierto antes de cerrarlo
    if (log_file != NULL) {
        fclose(log_file); // Cerrar el archivo solo si está abierto
    }

}




void createAndFillTheFileFromTheServer(const char *fileName, const char *fileExtension, const char *response) {
    char fileComplete[256];
    snprintf(fileComplete, sizeof(fileComplete), "%s.%s", fileName, fileExtension);
    printf("\n\nFile complete: %s\n", fileComplete);

    FILE *file = fopen(fileComplete, "w");
    if (file == NULL) {
        printf("Warning: Failed to create or open the file. Maybe don't needed if the url hasn't have a file to be created.\n");
        return;
    }
    
    // Extracting the content of the file
    int header_end = 0; // Variable to indicate if the end of the header has been found
    // Search for the end of the HTTP header (\r\n\r\n)
    char *header_end_start = strstr(response, "\r\n\r\n");
    if (header_end_start != NULL) {
        // If the end of the header is found, calculate the position of the content
        int content_start = header_end_start - response + 4;
        fprintf(file, "%s", response + content_start);
        header_end = 1;
    }

    // If the end of the header has been found, write the content to the file
    if (header_end) {
        printf("\n\nContent written to file: %s\n\n", fileComplete);
    } else {
        printf("Error: End of header not found in the response.\n");
    }

    fclose(file);
}




void fuctionToSendTheRequestToThePoxy(int socket_file_descriptor, const char *request) {
    write(socket_file_descriptor, request, strlen(request));
}


char *functionToReceiveTheResponseFromTheProxy(int socket_file_descriptor) {
    // Inicializar el tamaño máximo del response
    size_t max_response_size = MAXIMUM_SIZE_RESPONSE;
    // Inicializar un buffer para recibir la respuesta
    char *response = malloc(max_response_size);
    if (response == NULL) {
        printf("Error: No se pudo asignar memoria para la respuesta.\n");
        exit(EXIT_FAILURE);
    }
    // Limpiar el buffer de respuesta
    bzero(response, max_response_size);
    // Variable para seguir leyendo datos del socket
    ssize_t total_bytes_received = 0;
    // Variable para seguir leyendo datos del socket en cada iteración
    ssize_t bytes_received;
    // Leer datos del socket en bucle hasta que no haya más datos o se exceda el tamaño máximo
    while ((bytes_received = read(socket_file_descriptor, response + total_bytes_received, max_response_size - total_bytes_received)) > 0) {
        // Incrementar el total de bytes recibidos
        total_bytes_received += bytes_received;
        // Si el total de bytes recibidos iguala o supera el tamaño máximo, aumentar el tamaño máximo
        if (total_bytes_received >= max_response_size) {
            max_response_size *= 2; // Duplicar el tamaño máximo
            response = realloc(response, max_response_size); // Realocar memoria con el nuevo tamaño máximo
            if (response == NULL) {
                printf("Error: No se pudo realocar memoria para la respuesta.\n");
                exit(EXIT_FAILURE);
            }
            // Limpiar la nueva porción de memoria asignada
            bzero(response + total_bytes_received, max_response_size - total_bytes_received);
        }
    }
    // Verificar errores de lectura
    if (bytes_received < 0) {
        printf("Error al recibir la respuesta del proxy.\n");
        exit(EXIT_FAILURE);
    }
    return response;
}



int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Use: %s </path/to/log.log> <url:port>\n ", argv[0]);
        printf("Here you have an example: %s /home/mauricio/Documents/otras/Propios/log.log api.ejemplo.com:80\n", argv[0]);
    }

    const char *log_file_path = argv[1];
    const char *server_url_with_port = argv[2];
    char method[10]; // Arreglo de caracteres, no puntero a puntero

    int option = menu();

    int socket_file_descriptor; // Declara aquí para evitar redefiniciones
    char nombre_archivo_cache[2 * MD5_DIGEST_LENGTH + 1] = {0};

    switch (option) {
        case 1:
            strcpy(method, "GET"); // Asignación directa del método
            socket_file_descriptor = doTheConnectionWithTheProxy();
            struct pathAndRequest pathAndRequest_get = createTheRequest(method, server_url_with_port); // Declaración y asignación

            // Genera el nombre del archivo de caché basado en el host.
            generar_nombre_archivo_cache(pathAndRequest_get.server_url, nombre_archivo_cache);

            printf("\n\n\nServer URL: %s\n", pathAndRequest_get.server_url);
            printf("Path: %s\n", pathAndRequest_get.path);
            printf("Request: %s\n\n\n", pathAndRequest_get.request);



            struct FileNameAndExtension result_get = separateLastPart(pathAndRequest_get.path); // Estructura para almacenar el resultado de separateLastPart
            fuctionToSendTheRequestToThePoxy(socket_file_descriptor, pathAndRequest_get.request);
            char *response_get = functionToReceiveTheResponseFromTheProxy(socket_file_descriptor);

            
            printf("\nRequest: %s\n", pathAndRequest_get.request);
            printf("Response: %s\n", response_get);
            
            
            createAndFillTheFileFromTheServer(result_get.file_name, result_get.file_extension, response_get);
            createAndFillTheLogFile(log_file_path, pathAndRequest_get.request, response_get);
            close(socket_file_descriptor);
            break;
        case 2:
            strcpy(method, "HEAD"); // Asignación directa del método
            socket_file_descriptor = doTheConnectionWithTheProxy();
            struct pathAndRequest pathAndRequest_head = createTheRequest(method, server_url_with_port); // Declaración y asignación



            printf("\n\n\nServer URL: %s\n", pathAndRequest_head.server_url);
            printf("Path: %s\n", pathAndRequest_head.path);
            printf("Request: %s\n\n\n", pathAndRequest_head.request);



            struct FileNameAndExtension result_head = separateLastPart(pathAndRequest_head.path); // Estructura para almacenar el resultado de separateLastPart
            fuctionToSendTheRequestToThePoxy(socket_file_descriptor, pathAndRequest_head.request);
            char *response_head = functionToReceiveTheResponseFromTheProxy(socket_file_descriptor);
            printf("\nRequest: %s\n", pathAndRequest_head.request);
            printf("Response: %s\n", response_head);
            createAndFillTheFileFromTheServer(result_head.file_name, result_head.file_extension, response_head);
            createAndFillTheLogFile(log_file_path, pathAndRequest_head.request, response_head);
            close(socket_file_descriptor);
            break;
        case 3:
            socket_file_descriptor = doTheConnectionWithTheProxy();
            char request[4] = "EXIT";
            fuctionToSendTheRequestToThePoxy(socket_file_descriptor, request);
            close(socket_file_descriptor);
            return 0;
        default:
            printf("Opción no válida.\n");
            break;
    }

    return 0;
}
