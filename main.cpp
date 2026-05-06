#include <iostream>
#include <string>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <thread>

#define MINIAUDIO_IMPLEMENTATION
#include "reproductor.hpp"

// Convierte un string a un float con un valor por defecto si falla
float toFloat(const std::string& s, float defecto = 1.0f) {
    try { return std::stof(s); }
    catch (...) { return defecto; }
}

// Convierte un string a un int con un valor por defecto si falla
int toInt(const std::string& s, int defecto = 1) {
    try { return std::stoi(s); }
    catch (...) { return defecto; }
}

struct Album {
    std::string nombre;
    unsigned num_canciones;
};

// Muestra ppor pantalla los albums de la carpeta destino junto con su cantidad de canciones,
// y devuelve el número de albums de la carpeta. En caso de error, devuelve 0.
unsigned mostrarAlbumsEnCarpeta(const std::string& r) {
    // Buscamos los archivos
    unsigned contador = 0;
    std::vector<Album> albums;
    albums.clear();
    std::cout << "\033[2K\r" << "Buscando albums en " << MAGENTA << r << RESET << "... ";
    try {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(r)) {
            if (entry.is_regular_file()) {
                std::string path = entry.path().string();
                if (path.ends_with(".mp3") || path.ends_with(".wav") || 
                    path.ends_with(".flac") || path.ends_with(".ogg") || path.ends_with(".opus")) {
                    contador++;
                    Cancion can = obtenerInfoCancion(path);
                    if (can.album != "") {
                        bool album_nuevo = true;
                        for (size_t i=0; i < albums.size(); i++) {
                            if (can.album == albums[i].nombre) {
                                albums[i].num_canciones++;
                                album_nuevo = false;
                                break;
                            }
                        }
                        if (album_nuevo) albums.push_back({can.album, 1});
                    }
                }
            }
        }
    } catch (...) { std::cout << "error al abrir el directorio" << std::endl; return 0;}

    if (contador == 0) { // Comprobamos la búsqueda
        std::cout << "No se ha encontrado ninguna cancion en " << MAGENTA << r << RESET << std::endl;
        return 0;
    } else if (albums.size() == 0) {
        std::cout << "No se ha encontrado ningun album en " << MAGENTA << r << RESET << std::endl;
        return 0;
    }

    std::cout << std::endl;
    for (size_t i=0; i < albums.size(); i++)
        std::cout << CYAN << albums[i].nombre << RESET << " - " << AMARILLO << albums[i].num_canciones << RESET << " canciones" << std::endl;
    return albums.size();
}

// Muestra por pantalla los controles
void mostrarControles() {
    std::cout << "Comandos (Esta ayuda se muestra con 'H' o \"help\"):" << std::endl
        << "Espacio: Play/Pause  | 0: Salir    | +/-/= [%]: Cambiar \% de volumen      | Clear: Limpiar terminal" << std::endl
        << "Q [sec]: Echar atras | W: Autoplay | E [sec]: Adelantar  | I: Ir a cancion | R: Reiniciar  | P: Playlist" << std::endl
        << "A [num]: Anterior    | S: Shuffle  | D [num]: Siguiente  | F: Explorador   | L: Loop  " << std::endl
        << "Z / X: Por decidir   | B: Buscar   | V [num]: Velocidad  | M: Mostrar canciones" << std::endl
        << "C: Cargar carpeta    | C -r: Reiniciar reproductor       | C -a: Cargar solo un album" << std::endl;
}

// Programa reproductor de música dentro de una carpeta
int main() {
    Player player;
    bool autoplay = true, esperando_carpeta = false, esperando_busqueda = false, esperando_confirmacion_busqueda = false,
         esperando_seleccion = false, cambiar_prompt = false, reiniciar_reproductor = false, esperando_album = false;
    int indiceCancion = 0;
    std::string linea, nuevo_prompt, prompt_actual = "> ", ruta;

    // Preparamos la thread encargada de leer el teclado
    std::queue<std::string> cola_comandos;
    std::mutex mutex_cola;
    std::thread input_thread(leerComandos, std::ref(cola_comandos), std::ref(mutex_cola));
    input_thread.detach();

    iniciarUI();
    while (true) {
        // Comprobar si acabó la canción
        if (player.haTerminado() && autoplay) player.reproducirSiguiente();

        // Procesar comandos pendientes
        { std::lock_guard<std::mutex> lock(mutex_cola);
          while (!cola_comandos.empty()) {
            std::string linea = cola_comandos.front();
            cola_comandos.pop();
            if (linea.empty()) continue;

            if (esperando_carpeta) { // Introducir la ruta de una carpeta con 'C'
                Comando carpeta = parsearComando(linea);
                if (carpeta.accion == "0") {
                    std::cout << "\033[2K\r" << "Se ha cancelado el cargar carpeta" << std::endl;
                    esperando_carpeta = false;
                    cambiar_prompt = true;
                    nuevo_prompt = "> ";
                } else {
                    if (esperando_album) {
                        if (mostrarAlbumsEnCarpeta(linea) != 0) {
                            std::cout << "Seleccione un album ('0' para cancelar)" << std::endl;
                            esperando_carpeta = false;
                            cambiar_prompt = true;
                            ruta = linea;
                            nuevo_prompt = "Album que cargar: ";
                        }
                    } else {
                        player.cargarCarpeta(linea, reiniciar_reproductor);
                        // Solo volvemos al prompt normal si la carga tuvo éxito
                        if (!player.playerVacioSilencioso()) {
                            esperando_carpeta = false;
                            cambiar_prompt = true;
                            nuevo_prompt = "> ";
                        }
                    }
                }
                continue;
            } else if (esperando_album) { // Introducir el album de una carpeta con "C -a"
                Comando album = parsearComando(linea);
                if (album.accion == "0") {
                    std::cout << "\033[2K\r" << "Se ha cancelado el seleccionar album" << std::endl;
                    esperando_album = false;
                    cambiar_prompt = true;
                    nuevo_prompt = "> ";
                } else {
                    player.cargarCarpeta(ruta, reiniciar_reproductor, linea);
                    // Solo volvemos al prompt normal si la carga tuvo éxito
                    if (!player.playerVacioSilencioso()) {
                        esperando_album = false;
                        cambiar_prompt = true;
                        nuevo_prompt = "> ";
                    }
                }
                continue;
            } else if (esperando_busqueda) { // Búsqueda de 'B' - Buscar
                indiceCancion = player.buscarCancion(linea);
                if (indiceCancion != -1) {
                        std::cout << "\033[2K\r" << "Quieres reproducir la cancion "
                         << VERDE << player.tituloIndice(indiceCancion) << RESET << "? [y/n]";
                        esperando_confirmacion_busqueda = true;
                }
                esperando_busqueda = false;
                cambiar_prompt = true;
                nuevo_prompt = "> ";
                continue;
            } else if (esperando_confirmacion_busqueda) { // Confirmación de 'B' - Buscar
                Comando confirmacion = parsearComando(linea);
                if (confirmacion.accion == "y" || confirmacion.accion == "yes") {
                    std::cout << std::endl;
                    player.reproducirIndice(indiceCancion);
                    esperando_confirmacion_busqueda = false;
                } else if (confirmacion.accion == "n" || confirmacion.accion == "no") {
                    std::cout << "\033[2K\r" << "No se ha reproducido "
                     << VERDE << player.tituloIndice(indiceCancion) << RESET << std::endl;
                    esperando_confirmacion_busqueda = false;
                }
                continue;
            } else if (esperando_seleccion) { // selección de una opción de 'F' - Explorar
                if (player.seleccionarExplorador(linea)) esperando_seleccion = false;
                continue;
            }

            // Convertimos el string en un comando
            Comando cmd = parsearComando(linea);
            std::string& c = cmd.accion;
            bool tieneValor = cmd.valor != "";
            std::transform(c.begin(), c.end(), c.begin(), ::tolower); // Lo hacemos minúscula

            // Ejecutamos el comando
            if (c == "") player.togglePausa();
            else if (c == "a") player.reproducirRelativoNegativo(tieneValor ? toInt(cmd.valor) : 1);
            else if (c == "d") player.reproducirRelativoPositivo(tieneValor ? toInt(cmd.valor) : 1);
            else if (c == "e") player.saltar(tieneValor ? toFloat(cmd.valor) : 10.0f);
            else if (c == "q") player.saltar(tieneValor ? -toFloat(cmd.valor) : -10.0f);
            else if (c == "i" || c == "ir") player.reproducirIndice(tieneValor ? toInt(cmd.valor, -1) -1 : -1);
            else if (c == "=") player.asignarVolumen(tieneValor ? toFloat(cmd.valor, 50.0f)/100 : 1.0f);
            else if (c == "+") player.cambiarVolumen(tieneValor ? toFloat(cmd.valor, 10.0f)/100 : 0.1f);
            else if (c == "-") player.cambiarVolumen(tieneValor ? -toFloat(cmd.valor, 10.0f)/100 : -0.1f);   
            else if (c == "clear" || c == "cls" || c == "clean" || c == "cl") std::cout << "\033[2J\033[1;1H" << std::flush;
            else if (c == "h" || c == "help") mostrarControles();
            else if (c == "m") player.mostrarCanciones();
            else if (c == "s") player.toggleShuffle();
            else if (c == "l") player.toggleLoop();
            else if (c == "r") player.reiniciar();
            else if (c == "v") player.cambiarPitch(tieneValor ? toFloat(cmd.valor) : 1.0f);
            else if (c == "p") std::cout << "Comando en desarrollo (Cargar canciones de una playlist en concreto)" << std::endl;
            else if (c == "t") std::cout << "Comando en desarrollo (Mostrar artistas)" << std::endl;
            else if (c == "g") std::cout << "Comando en desarrollo (Comando por decidir)" << std::endl;
            else if (c == "j") std::cout << "Comando en desarrollo (Comando por decidir)" << std::endl;
            else if (c == "k") std::cout << "Comando en desarrollo (Comando por decidir)" << std::endl;
            else if (c == "w") {
                if (autoplay) std::cout << "Reproduccion automatica desactivada" << std::endl;
                    else      std::cout << "Reproduccion automatica activada" << std::endl;
                autoplay = !autoplay;
            }
            else if (c == "f") {
                player.iniciarExplorador(tieneValor ? cmd.valor : "");
                esperando_seleccion = true;
            }
            else if (c == "c") {
                reiniciar_reproductor = cmd.tieneFlag('r');
                if (cmd.tieneFlag('a')) esperando_album = true;
                if (!tieneValor || cmd.valor == "") {
                    std::cout << "Escriba el directorio donde estan sus canciones ('0' para cancelar)";
                    cambiar_prompt = true;
                    esperando_carpeta = true;
                    nuevo_prompt = "Directorio donde estan las canciones: ";
                } else {
                    if (esperando_album) {
                        if (mostrarAlbumsEnCarpeta(cmd.valor) != 0) {
                            std::cout << "Seleccione un album ('0' para cancelar)" << std::endl;
                            ruta = cmd.valor;
                            cambiar_prompt = true;
                            nuevo_prompt = "Album que cargar: ";
                        }
                    } else player.cargarCarpeta(cmd.valor, reiniciar_reproductor);
                }
            }
            else if (c == "b") {
                if (tieneValor) {
                    indiceCancion = player.buscarCancion(cmd.valor);
                    if (indiceCancion != -1) {
                        std::cout << "\033[2K\r" << "Quieres reproducir la cancion "
                         << VERDE << player.tituloIndice(indiceCancion) << RESET << "? [y/n]";
                        esperando_confirmacion_busqueda = true;
                    }
                } else {
                    std::cout << "Escriba el titulo de la cancion que quiera buscar" << std::endl;
                    cambiar_prompt = true;
                    nuevo_prompt = "Buscar: ";
                    esperando_busqueda = true;
                }
            }
            else if (c == "0") {
                #ifndef _WIN32
                    restaurarTerminal();
                #endif
                player.pausar();
                std::cout << "Cerrando el programa..." << std::endl;
                exit(0);
            }
            else std::cout << "Comando desconocido: " << c << std::endl;
          }
        }

        if (cambiar_prompt) { // Actualizamos el prompt
            std::lock_guard<std::mutex> lock(mutexPrompt);
            prompt_actual = nuevo_prompt;
            cambiar_prompt = false;
        }

        { // Actualizamos la UI
            auto [pos, dur] = player.obtenerPosicionYDuracion();
            std::lock_guard<std::mutex> lock(mutexPrompt);
            actualizarUI(player.obtenerInfoActual(), pos, dur, prompt_actual, lineaActual);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Dormimos para no consumir la CPU al 100%
    }
}
