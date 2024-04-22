#ifndef MANEJOCACHE_H
#define MANEJOCACHE_H

#define CACHE_SIZE (MAX * 10)  // Establece un tamaño de caché considerable para almacenar respuestas HTTP.
#define CACHE_DIR "./cache/historial"  // Define el directorio actual como ubicación del caché.

struct LimpiarArgs {
    long ttl;
};

void generar_nombre_archivo_cache(const char *method, const char *url, char *nombre_archivo_cache);
void almacenar_respuesta_cache(const char *nombre_archivo_cache, const char *respuesta);
int obtener_respuesta_cache(const char *nombre_archivo_cache, char *respuesta, long ttl);
void limpiar_cache(const char *directorio_cache, long ttl);
void *funcion_limpiar(void *args);

#endif
