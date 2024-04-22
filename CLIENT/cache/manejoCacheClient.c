#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <openssl/md5.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/types.h>
#include <limits.h>
#include "manejoCacheClient.h"

void generar_nombre_archivo_cache(const char *url, char *nombre_archivo_cache) {
    /**
    * Genera un nombre de archivo de caché basado en un hash MD5 de la URL dada.
    * 
    * @param url La URL de la cual se generará el nombre del archivo de caché.
    * @param nombre_archivo_cache Un buffer donde se almacenará el nombre del archivo generado.
    * 
    * Esta función toma una URL como entrada y genera un nombre de archivo único
    * utilizando el algoritmo de hash MD5. El resultado es una cadena de caracteres
    * hexadecimales que representan el hash MD5 de la URL. Este nombre de archivo
    * se utiliza para guardar y recuperar respuestas de caché de una manera única,
    * asegurando que cada URL tenga su propio archivo de caché correspondiente.
    */
    unsigned char digest[MD5_DIGEST_LENGTH]; // Crea un array para almacenar el hash MD5.

    // Calcula el hash MD5 de la URL y almacena el resultado en 'digest'.
    MD5((unsigned char*)url, strlen(url), (unsigned char*)&digest);    

    // Convierte el hash MD5 en una cadena de caracteres hexadecimales.
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        // Cada byte del hash MD5 se convierte en dos caracteres hexadecimales.
        sprintf(&nombre_archivo_cache[i*2], "%02x", (unsigned int)digest[i]);
    }

    // Añade el carácter nulo al final para asegurar que la salida sea una cadena C válida.
    nombre_archivo_cache[MD5_DIGEST_LENGTH * 2] = '\0'; // Asegurar terminación de cadena

    // Opcional: imprime la URL para propósitos de depuración o registro.
    printf("URL: %s\n", url);
}


void almacenar_respuesta_cache(const char *nombre_archivo_cache, const char *request, const char *respuesta) {
    /**
    * Almacena la respuesta HTTP en el archivo de caché especificado, precedida por una estampa de tiempo.
    *
    * @param nombre_archivo_cache El nombre del archivo de caché donde se almacenará la respuesta.
    * @param request La solicitud HTTP original que generó la respuesta.
    * @param respuesta La respuesta HTTP completa que se va a almacenar.
    *
    * Esta función abre (o crea si no existe) un archivo correspondiente al nombre de archivo de caché proporcionado.
    * Escribe la estampa de tiempo actual seguida de la respuesta HTTP. La estampa de tiempo se utiliza para
    * verificar la validez del caché basado en el TTL (Time To Live) configurado.
    */

    char ruta_completa[1024];
    snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", CACHE_DIR, nombre_archivo_cache);
    int fullLineOfCacheEntry = strlen(request) + strlen(respuesta) + 1;
    // Intenta abrir (o crear si no existe) el archivo de caché con permisos de escritura.
    FILE *archivo = fopen(ruta_completa, "w");
    
    if(archivo == NULL) {
        // Intenta crear la carpeta "historial"
        if (mkdir(CACHE_DIR, 0777) == 0) {
            printf("La carpeta \"%s\" se ha creado exitosamente.\n", CACHE_DIR);
            // Intenta abrir el archivo nuevamente después de crear la carpeta.
            archivo = fopen(ruta_completa, "w");
        } else {
            perror("Error al intentar crear la carpeta");
            exit(EXIT_FAILURE);
        }
    }

    // Verifica si el archivo se abrió correctamente.
    if(archivo != NULL){
        // Obtiene la hora actual del sistema como un valor de tiempo de tipo time_t.
        time_t now = time(NULL);

        // Escribe la estampa de tiempo actual al comienzo del archivo de caché.
        // Esta estampa es utilizada para validar la frescura del caché.
        fprintf(archivo, "%ld\n", now);

        // Escribe la respuesta HTTP completa en el archivo de caché.
        // fputs(respuesta, archivo);
        char *cache_entry = malloc(fullLineOfCacheEntry);
        snprintf(cache_entry, fullLineOfCacheEntry, "\n%s\n%s\n", request, respuesta);
        fprintf(archivo, "%s\n", cache_entry);

        // Cierra el archivo para asegurarse de que todos los cambios se escriban en el disco.
        fclose(archivo);
    }
    // Si el archivo no pudo abrirse, no se realiza ninguna acción.
}



int obtener_respuesta_cache(const char *nombre_archivo_cache, char *respuesta) {
    /**
    * Intenta recuperar una respuesta HTTP del caché basándose en el nombre de archivo proporcionado.
    * 
    * @param nombre_archivo_cache El nombre del archivo de caché que contiene la respuesta.
    * @param respuesta Buffer donde se almacena la respuesta HTTP recuperada del caché.
    * @return int Retorna 1 si la respuesta es recuperada con éxito y es válida según el TTL,
    *             o 0 si el caché no es válido o no pudo ser leído.
    *
    * Esta función verifica si un archivo de caché existe y está actualizado. Lee la estampa de tiempo
    * almacenada en el archivo para determinar si la respuesta en caché sigue siendo válida. Si la
    * respuesta está dentro del TTL, la lee del archivo y la almacena en el buffer de respuesta.
    */

    struct stat st; // Variable para almacenar información del archivo.

    char ruta_completa[1024];
    snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", CACHE_DIR, nombre_archivo_cache);

    // Usa stat para obtener información del archivo y verifica si existe.
    if (stat(ruta_completa, &st) == 0) {
        

        // Abre el archivo de caché en modo de lectura.
        FILE *archivo = fopen(ruta_completa, "r");
        if(archivo == NULL) {
            printf("Error archivo cache");
        }
        // Verifica si el archivo se abrió correctamente.
        if (archivo != NULL) {
            time_t stored_time; // Variable para almacenar la estampa de tiempo leída del archivo.

            // Intenta leer la estampa de tiempo del archivo.
            if (fscanf(archivo, "%ld\n", &stored_time) == 1) {
                // Obtiene la hora actual del sistema.
                time_t now = time(NULL);

                // Calcula la diferencia entre la hora actual y la estampa de tiempo.
                // Si la diferencia es menor o igual al TTL global, la respuesta en caché es válida.
                if (difftime(now, stored_time) <= 0) {
                    // Lee la respuesta del archivo de caché y la almacena en el buffer de respuesta.
                    fread(respuesta, 1, st.st_size, archivo);
                    // Cierra el archivo.
                    fclose(archivo);
                    // Asegura que la respuesta sea una cadena C terminada en NULL.
                    respuesta[st.st_size] = '\0';
                    return 1; // Retorna éxito.
                }
                // Si el caché ha expirado, continuar y devolver 0.
            }
            // Cierra el archivo si la estampa de tiempo no se leyó o si el caché ha expirado.
            fclose(archivo);
        }
    }
    // Retorna falla si no hay archivo de caché, no se pudo abrir, o el caché ha expirado.
    return 0;
}



void limpiar_cache(const char *directorio_cache) {
    DIR *dir;
    struct dirent *entrada;

    // Abre el directorio de caché
    dir = opendir(directorio_cache);
    if (dir == NULL) {
        perror("Error al abrir el directorio de caché");
        exit(EXIT_FAILURE);
    }

    // Recorre todas las entradas del directorio
    while ((entrada = readdir(dir)) != NULL) {
        // Excluye las entradas "." y ".."
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0) {
            continue;
        }

        // Construye la ruta completa del archivo de caché
        char ruta_archivo[FILENAME_MAX];
        snprintf(ruta_archivo, FILENAME_MAX, "%s/%s", directorio_cache, entrada->d_name);

        // Elimina el archivo de caché
        if (remove(ruta_archivo) != 0) {
            perror("Error al eliminar el archivo de caché");
        } else {
            printf("Archivo de caché eliminado: %s\n", ruta_archivo);
        }
    }

    // Cierra el directorio
    closedir(dir);
}