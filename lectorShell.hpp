#include <string>
#include <sstream>
#include <queue>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>

#ifdef _WIN32
    #include <conio.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

#include <queue>

struct Comando {
    std::string accion;
    std::string valor;
    bool tieneValor;
};

// Convierte la lína recibida desde el cin a la clase Comando
Comando parsearComando(const std::string& linea) {
    std::istringstream iss(linea);
    Comando cmd;
    cmd.tieneValor = false;
    cmd.valor = "";
    iss >> cmd.accion;
    
    std::string resto;
    if (std::getline(iss, resto)) {
        // Quitamos el espacio inicial que queda tras leer la acción
        size_t inicio = resto.find_first_not_of(' ');
        if (inicio != std::string::npos) {
            cmd.valor = resto.substr(inicio);
            cmd.tieneValor = true;
        }
    }
    return cmd;
}

// Hilo lector de comandos
inline std::atomic<bool> inputBloqueado = false;
inline std::string lineaActual = "";
inline std::mutex mutexPrompt;
#ifdef _WIN32 // Para windows
    int leerComandos(std::queue<std::string>& cola, std::mutex& mtx) {
        cola.push("h");
        cola.push("c");
        char ch;
        while (true) {
            if (inputBloqueado) { 
                std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Dormimos para no consumir la CPU al 100%
                continue; // ignorar input mientras mostrarCanciones está activo
            }

            ch = _getch();
            if (ch == '\r') {
                std::lock_guard<std::mutex> lock(mtx);
                cola.push(lineaActual);
                lineaActual = "";
            } else if (ch == '\b') {
                std::lock_guard<std::mutex> lock(mutexPrompt);
                if (!lineaActual.empty()) lineaActual.pop_back();
            } else {
                std::lock_guard<std::mutex> lock(mutexPrompt);
                lineaActual += ch;
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

    int leerComandos(std::queue<std::string>& cola, std::mutex& mtx) {
        cola.push("h");
        cola.push("c");
        char ch;
        while (read(STDIN_FILENO, &ch, 1) == 1) {
            if (inputBloqueado) { 
                std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Dormimos para no consumir la CPU al 100%
                continue; // ignorar input mientras mostrarCanciones está activo
            }
            
            if (ch == '\n' || ch == '\r') {
                std::lock_guard<std::mutex> lock(mtx);
                cola.push(lineaActual);
                lineaActual = "";
            } else if (ch == 127 || ch == '\b') {
                std::lock_guard<std::mutex> lock(mutexPrompt);
                if (!lineaActual.empty()) lineaActual.pop_back();
            } else {
                std::lock_guard<std::mutex> lock(mutexPrompt);
                lineaActual += ch;
            }
        }
        return 0;
    }
#endif