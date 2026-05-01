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

// Muestra por pantalla los controles
void mostrarControles() {
    std::cout << "Comandos (Esta ayuda se muestra con 'H' o \"help\"):" << std::endl
        << "Espacio: Play/Pause     | 0: Salir    | +/-/= [%]: Cambiar \% de volumen" << std::endl
        << "Q [sec]: Echar atras    | W: Autoplay | E [sec]: Adelantar | I: Ir a cancion | R: Reiniciar  | P: Playlist" << std::endl
        << "A [num]: Anterior       | S: Shuffle  | D [num]: Siguiente | F: Explorador   | L: Loop  " << std::endl
        << "C [str]: Cargar carpeta | B: Buscar   | V [num]: Velocidad | M: Mostrar canciones" << std::endl
        << "Clear: Limpiar terminal |" << std::endl;
}

// Programa reproductor de música dentro de una carpeta
int main() {
    Player player;
    bool autoplay = true, esperandoCarpeta = false, esperandoBusqueda = false, esperandoConfirmacionBusqueda = false,
         esperandoSeleccion = false, cambiarPrompt = false;
    int indiceCancion = 0;
    std::string linea, nuevoPrompt = "", promptActual = "> ";

    // Preparamos la thread encargada de leer el teclado
    std::queue<std::string> colaComandos;
    std::mutex mutexCola;
    std::thread inputThread(leerComandos, std::ref(colaComandos), std::ref(mutexCola));
    inputThread.detach();

    iniciarUI();
    while (true) {
        // Comprobar si acabó la canción
        if (player.haTerminado() && autoplay) player.reproducirSiguiente();

        // Procesar comandos pendientes
        { std::lock_guard<std::mutex> lock(mutexCola);
          while (!colaComandos.empty()) {
            std::string linea = colaComandos.front();
            colaComandos.pop();
            if (linea.empty()) continue;

            if (esperandoCarpeta) { // Introducir la ruta de una carpeta con 'C'
                Comando carpeta = parsearComando(linea);
                if (carpeta.accion == "0") {
                    std::cout << "\033[2K\r" << "Se ha cancelado el cargar carpeta" << std::endl;
                    esperandoCarpeta = false;
                    cambiarPrompt = true;
                    nuevoPrompt = "> ";
                } else {
                    player.cargarCarpeta(linea);
                    // Solo volvemos al prompt normal si la carga tuvo éxito
                    if (!player.playerVacioSilencioso()) {
                        esperandoCarpeta = false;
                        cambiarPrompt = true;
                        nuevoPrompt = "> ";
                    }
                }
                continue;
            } else if (esperandoBusqueda) { // Búsqueda de 'B' - Buscar
                indiceCancion = player.buscarCancion(linea);
                if (indiceCancion != -1) {
                        std::cout << "\033[2K\r" << "Quieres reproducir la cancion "
                         << VERDE << player.tituloIndice(indiceCancion) << RESET << "? [y/n]";
                        esperandoConfirmacionBusqueda = true;
                }
                esperandoBusqueda = false;
                cambiarPrompt = true;
                nuevoPrompt = "> ";
                continue;
            } else if (esperandoConfirmacionBusqueda) { // Confirmación de 'B' - Buscar
                Comando confirmacion = parsearComando(linea);
                if (confirmacion.accion == "y" || confirmacion.accion == "yes") {
                    std::cout << std::endl;
                    player.reproducirIndice(indiceCancion);
                    esperandoConfirmacionBusqueda = false;
                } else if (confirmacion.accion == "n" || confirmacion.accion == "no") {
                    std::cout << "\033[2K\r" << "No se ha reproducido "
                     << VERDE << player.tituloIndice(indiceCancion) << RESET << std::endl;
                    esperandoConfirmacionBusqueda = false;
                }
                continue;
            } else if (esperandoSeleccion) { // selección de una opción de 'F' - Explorar
                if (player.seleccionarExplorador(linea)) esperandoSeleccion = false;
                continue;
            }

            // Convertimos el string en un comando
            Comando cmd = parsearComando(linea);
            std::string& c = cmd.accion;
            std::transform(c.begin(), c.end(), c.begin(), ::tolower); // Lo hacemos minúscula

            // Ejecutamos el comando
            if (c == "") player.togglePausa();
            else if (c == "a") player.reproducirRelativoNegativo(cmd.tieneValor ? toInt(cmd.valor) : 1);
            else if (c == "d") player.reproducirRelativoPositivo(cmd.tieneValor ? toInt(cmd.valor) : 1);
            else if (c == "e") player.saltar(cmd.tieneValor ? toFloat(cmd.valor) : 10.0f);
            else if (c == "q") player.saltar(cmd.tieneValor ? -toFloat(cmd.valor) : -10.0f);
            else if (c == "i" || c == "ir") player.reproducirIndice(cmd.tieneValor ? toInt(cmd.valor, -1) -1 : -1);
            else if (c == "=") player.asignarVolumen(cmd.tieneValor ? toFloat(cmd.valor, 50.0f)/100 : 1.0f);
            else if (c == "+") player.cambiarVolumen(cmd.tieneValor ? toFloat(cmd.valor, 10.0f)/100 : 0.1f);
            else if (c == "-") player.cambiarVolumen(cmd.tieneValor ? -toFloat(cmd.valor, 10.0f)/100 : -0.1f);   
            else if (c == "clear" || c == "cls" || c == "clean" || c == "cl") std::cout << "\033[2J\033[1;1H" << std::flush;
            else if (c == "h" || c == "help") mostrarControles();
            else if (c == "m") player.mostrarCanciones();
            else if (c == "s") player.toggleShuffle();
            else if (c == "l") player.toggleLoop();
            else if (c == "r") player.reiniciar();
            else if (c == "v") player.cambiarPitch(cmd.tieneValor ? toFloat(cmd.valor) : 1.0f);
            else if (c == "p") std::cout << "Comando en desarrollo (Cargar canciones de una playlist en concreto)" << std::endl;
            else if (c == "t") std::cout << "Comando en desarrollo (Comando por decidir)" << std::endl;
            else if (c == "g") std::cout << "Comando en desarrollo (Comando por decidir)" << std::endl;
            else if (c == "j") std::cout << "Comando en desarrollo (Comando por decidir)" << std::endl;
            else if (c == "k") std::cout << "Comando en desarrollo (Comando por decidir)" << std::endl;
            else if (c == "x") std::cout << "Comando en desarrollo (Mostrar artistas)" << std::endl;
            else if (c == "w") {
                if (autoplay) std::cout << "Reproduccion automatica desactivada" << std::endl;
                    else      std::cout << "Reproduccion automatica activada" << std::endl;
                autoplay = !autoplay;
            }
            else if (c == "f") {
                player.iniciarExplorador(cmd.tieneValor ? cmd.valor : "");
                esperandoSeleccion = true;
            }
            else if (c == "c") {
                if (cmd.tieneValor) player.cargarCarpeta(cmd.valor);
                else {
                    std::cout << "Escriba el directorio donde estan sus canciones ('0' para cancelar)";
                    cambiarPrompt = true;
                    nuevoPrompt = "Directorio donde estan las canciones: ";
                    esperandoCarpeta = true;
                }
            }
            else if (c == "b") {
                if (cmd.tieneValor) {
                    indiceCancion = player.buscarCancion(cmd.valor);
                    if (indiceCancion != -1) {
                        std::cout << "\033[2K\r" << "Quieres reproducir la cancion "
                         << VERDE << player.tituloIndice(indiceCancion) << RESET << "? [y/n]";
                        esperandoConfirmacionBusqueda = true;
                    }
                } else {
                    std::cout << "Escriba el titulo de la cancion que quiera buscar" << std::endl;
                    cambiarPrompt = true;
                    nuevoPrompt = "Buscar: ";
                    esperandoBusqueda = true;
                }
            }
            else if (c == "0") {
                #ifndef _WIN32
                    restaurarTerminal();
                #endif
                player.pausar();
                std::cout << "Cerrando el programa, pulse enter para salir..." << std::endl;
                exit(0);
            }
            else std::cout << "Comando desconocido: " << c << std::endl;
          }
        }

        if (cambiarPrompt) { // Actualizamos el prompt
            std::lock_guard<std::mutex> lock(mutexPrompt);
            promptActual = nuevoPrompt;
        }

        { // Actualizamos la UI
            auto [pos, dur] = player.obtenerPosicionYDuracion();
            std::lock_guard<std::mutex> lock(mutexPrompt);
            actualizarUI(player.obtenerInfoActual(), pos, dur, promptActual, lineaActual);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Dormimos para no consumir la CPU al 100%
    }
}

// Compilación:
// Linux: g++ -std=c++23 Reproductor.cpp -o Reproductor -lpthread -lm -ldl
// Windows: g++ -std=c++23 Reproductor.cpp -o Reproductor.exe -lpthread -lole32 -lwinmm