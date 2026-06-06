#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <unordered_map>

// Colores de texto
#define RESET "\033[0m"
#define ROJO "\033[31m"
#define VERDE "\033[32m"
#define AMARILLO "\033[33m"
#define AZUL "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN "\033[36m"
#define NEGRITA "\033[1m"
#define FONDO_AZUL "\033[44m"

// Convierte un string a un float con un valor por defecto si falla
float toFloat(const std::string& s, float defecto = 1.0f) {
    try { return std::stof(s); }
    catch (...) { return defecto; }
};

// Convierte un string a un int con un valor por defecto si falla
int toInt(const std::string& s, int defecto = 1) {
    try { return std::stoi(s); }
    catch (...) { return defecto; }
};

// Controles
struct Control {
    std::vector<std::string> controles;

    unsigned asignarControles(const std::string& linea) {
        // 1. Extraer la acción principal
        std::istringstream iss(linea);
        std::string aux;
        if (!(iss >> aux)) return 0;

        // 2. Procesar los controles
        while (iss >> aux)
            controles.push_back(aux);
        return controles.size();
    }

    bool controlCorrecto(const std::string& f) const {
        return std::find(controles.begin(), controles.end(), f) != controles.end(); }

    std::string controlesToString() const {
        std::string aux;
        for (size_t i=0; i < controles.size(); i++) {
            if (!controles[i].empty()) {
                aux += toupper(controles[i][0]);
                aux += controles[i].substr(1);
            }
            if (i != controles.size()-1)
                aux += "/";
        }
        return aux;
    }
};

inline Control controles_salir; //0
inline Control controles_help; //h help
inline Control controles_pause; // 
inline Control controles_clear; //clear clean cls
inline Control controles_vol_mas; //+
inline Control controles_vol_menos; //-
inline Control controles_vol_igual; //=
inline Control controles_autoplay; //w
inline Control controles_ir_a; //i
inline Control controles_reiniciar; //r
inline Control controles_atras; //q
inline Control controles_adelante; //e
inline Control controles_anterior; //a
inline Control controles_siguiente; //d
inline Control controles_shuffle; //s
inline Control controles_loop; //l
inline Control controles_explorador; //f
inline Control controles_buscar; //b
inline Control controles_velocidad; //v
inline Control controles_mostrar_canciones; //m
inline Control controles_cargar_carpeta; //c
inline char flag_cargar_carpeta_reiniciar; //r
inline char flag_cargar_carpeta_album; //a

// Colores por defecto
inline std::string color_Carpeta = MAGENTA;
inline std::string color_Tiempo_actual = VERDE;
inline std::string color_Duracion = AZUL;
inline std::string color_Cancion = VERDE;
inline std::string color_Numeros = AMARILLO;
inline std::string color_Barra_progreso = VERDE;
inline std::string color_Album = CYAN;
inline std::string color_Error = ROJO;

// Inicializa todos los controles a los valores originales
void controlesPorDefecto() {
    controles_salir.controles.push_back("exit");
    controles_salir.controles.push_back("0");
    controles_help.controles.push_back("h");
    controles_help.controles.push_back("help");
    controles_clear.controles.push_back("clear");
    controles_clear.controles.push_back("clean");
    controles_clear.controles.push_back("cls");
    controles_vol_mas.controles.push_back("+");
    controles_vol_menos.controles.push_back("-");
    controles_vol_igual.controles.push_back("=");
    controles_autoplay.controles.push_back("w");
    controles_ir_a.controles.push_back("i");
    controles_reiniciar.controles.push_back("r");
    controles_atras.controles.push_back("q");
    controles_adelante.controles.push_back("e");
    controles_anterior.controles.push_back("a");
    controles_siguiente.controles.push_back("d");
    controles_shuffle.controles.push_back("s");
    controles_loop.controles.push_back("l");
    controles_explorador.controles.push_back("f");
    controles_buscar.controles.push_back("b");
    controles_velocidad.controles.push_back("v");
    controles_mostrar_canciones.controles.push_back("m");
    controles_cargar_carpeta.controles.push_back("c");
    flag_cargar_carpeta_reiniciar = 'r';
    flag_cargar_carpeta_album = 'a';
}

// Separador de la UI por defecto
std::string separador_UI = "-";

// Ruta de la carpeta por defecto
std::string ruta_inicial = "";

// Volumen al iniciar la aplicación
float volumen_inicial = 0.5f;

bool cargarOpciones(const std::string& fichero_opciones) {
    std::ifstream op(fichero_opciones);
    if (!op.is_open()) return false;

    std::unordered_map<std::string, Control*> mapaControles = {
        {"Salir", &controles_salir},
        {"Help", &controles_help},
        {"Pause", &controles_pause},
        {"Clear", &controles_clear},
        {"Vol+", &controles_vol_mas},
        {"Vol-", &controles_vol_menos},
        {"Vol=", &controles_vol_igual},
        {"Autoplay", &controles_autoplay},
        {"Ir_a", &controles_ir_a},
        {"Reiniciar", &controles_reiniciar},
        {"Atras", &controles_atras},
        {"Adelante", &controles_adelante},
        {"Anterior", &controles_anterior},
        {"Siguiente", &controles_siguiente},
        {"Shuffle", &controles_shuffle},
        {"Loop", &controles_loop},
        {"Explorador", &controles_explorador},
        {"Buscar", &controles_buscar},
        {"Velocidad", &controles_velocidad},
        {"Mostrar_canciones", &controles_mostrar_canciones},
        {"Cargar_carpeta", &controles_cargar_carpeta}
    };
    
    std::unordered_map<std::string, std::string*> mapaColores = {
        {"Carpeta", &color_Carpeta},
        {"Tiempo_actual", &color_Tiempo_actual},
        {"Duracion", &color_Duracion},
        {"Cancion", &color_Cancion},
        {"Numeros", &color_Numeros},
        {"Barra_progreso", &color_Barra_progreso},
        {"Album", &color_Album},
        {"Error", &color_Error}
    };

    std::string linea;
    while (std::getline(op, linea)) {
        // Ignorar comentarios y líneas vacías
        if (linea.empty() || linea[0] == '/') continue;

        // Separar clave y valor por ':'
        size_t sep = linea.find(':');
        if (sep == std::string::npos) continue;
        std::string clave = linea.substr(0, sep);
        std::string valor = linea.substr(sep + 1);

        // Quitar espacios iniciales del valor
        size_t inicio = valor.find_first_not_of(' ');
        valor = (inicio != std::string::npos) ? valor.substr(inicio) : "";

        if (clave == "Flag_carpeta_reiniciar") {
            if (!valor.empty()) flag_cargar_carpeta_reiniciar = valor[0];
        } else if (clave == "Flag_carpeta_album") {
            if (!valor.empty()) flag_cargar_carpeta_album = valor[0];
        } else if (clave == "Separador_UI") {
            separador_UI = valor.empty() ? "-" : valor;
        } else if (clave == "Ruta_inicial") {
            ruta_inicial = valor;
        } else if (clave == "Vol_ini") {
            volumen_inicial = toFloat(valor, 50.0f)/100;
        } else if (mapaControles.count(clave)) {
            Control* ctrl = mapaControles[clave];
            ctrl->controles.clear();
            std::istringstream iss(valor);
            std::string token;
            while (iss >> token) ctrl->controles.push_back(token);
            // Caso especial: Pause puede no tener valor (significa tecla espacio/enter)
            // Se gestiona en main comparando con línea vacía, no hace falta guardar " "
        } else if (mapaColores.count(clave)) {
            *mapaColores[clave] = "\033[" + valor + "m";
        }
    }
    return true;
}