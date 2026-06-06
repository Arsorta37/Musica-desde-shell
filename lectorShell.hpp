#pragma once
#include <string>
#include <sstream>
#include <queue>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <algorithm>

#include "lectorOpciones.hpp"

#ifdef _WIN32
    #include <conio.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

struct Comando {
    std::vector<char> flags;
    std::string accion;
    std::string valor;

    bool tieneFlag(char f) const { // Utilidad para comprobar flags
        return std::find(flags.begin(), flags.end(), f) != flags.end();
    }
};

// Devuelve el string linea sin los espacios iniciales
std::string quitarEspacioInicial(const std::string &linea) {
    size_t inicio = linea.find_first_not_of(' ');
    if (inicio != std::string::npos)
        return linea.substr(inicio);
    else
        return "";
}

// Convierte la línea recibida desde el terminal a la clase Comando
Comando parsearComando(const std::string& linea) {
    std::istringstream iss(linea);
    Comando cmd;

    // 1. Extraer la acción principal
    if (!(iss >> cmd.accion)) return cmd;

    // 2. Procesar el resto de tokens
    std::string token;
    while (iss >> token) {
        // Comprobamos si es un valor o una flag
        if (!token.empty() && token[0] == '-') {
            // Es un bloque de flags (ej: "-ap" o "-a")
            for (size_t i = 1; i < token.size(); ++i)
                cmd.flags.push_back(token[i]); // Cada letra es una flag
        } else {
            // Es parte del valor. Concatenamos por si contiene espacios
            if (!cmd.valor.empty()) cmd.valor += " ";
            cmd.valor += token;
        }
    }
    return cmd;
}

// Variables globales compartidas entre hilos
inline std::atomic<bool> inputBloqueado = false;
inline std::string input_actual = "";
inline std::mutex mutexPrompt;

// Hilo lector de comandos
#ifdef _WIN32 // Para windows
    int leerComandos(std::queue<std::string>& cola, std::mutex& mtx, const bool cargarCarpeta=true) {
        if (cargarCarpeta) cola.push(controles_cargar_carpeta.controles[0] + " -" + flag_cargar_carpeta_reiniciar);
        char ch;
        while (true) {
            if (inputBloqueado) { 
                std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Dormimos para no consumir la CPU al 100%
                continue; // ignorar input mientras mostrarCanciones está activo
            }

            ch = _getch();
            if (ch == '\r') {
                std::lock_guard<std::mutex> lock(mtx);
                cola.push(input_actual);
                input_actual = "";
            } else if (ch == '\b') {
                std::lock_guard<std::mutex> lock(mutexPrompt);
                if (!input_actual.empty()) input_actual.pop_back();
            } else {
                std::lock_guard<std::mutex> lock(mutexPrompt);
                input_actual += ch;
            }
        }
        return 0;
    }
#else // Para linux
    termios terminalOriginal;
    void restaurarTerminal() { tcsetattr(STDIN_FILENO, TCSANOW, &terminalOriginal); }
    void activarModoRaw() {
        tcgetattr(STDIN_FILENO, &terminalOriginal);
        termios raw = terminalOriginal;
        raw.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }

    int leerComandos(std::queue<std::string>& cola, std::mutex& mtx, const bool cargarCarpeta=true) {
        if (cargarCarpeta) cola.push(controles_cargar_carpeta.controles[0] + " -" + flag_cargar_carpeta_reiniciar);
        char ch;
        while (read(STDIN_FILENO, &ch, 1) == 1) {
            if (inputBloqueado) { 
                std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Dormimos para no consumir la CPU al 100%
                continue; // ignorar input mientras mostrarCanciones está activo
            }
            
            if (ch == '\n' || ch == '\r') {
                std::lock_guard<std::mutex> lock(mtx);
                cola.push(input_actual);
                input_actual = "";
            } else if (ch == 127 || ch == '\b') {
                std::lock_guard<std::mutex> lock(mutexPrompt);
                if (!input_actual.empty()) input_actual.pop_back();
            } else {
                std::lock_guard<std::mutex> lock(mutexPrompt);
                input_actual += ch;
            }
        }
        return 0;
    }
#endif
