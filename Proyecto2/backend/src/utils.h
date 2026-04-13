#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <ctime>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <cstring>

// Verifica si el archivo existe
inline bool fileExists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

// Crea directorios padre de forma recursiva
inline void createParentDirs(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == 0) return;
    std::string dir = path.substr(0, pos);
    std::string cmd = "mkdir -p \"" + dir + "\"";
    system(cmd.c_str());
}

// Obtiene nombre de archivo DOT desde la ruta de salida (reemplaza extension por .dot)
inline std::string getDotPath(const std::string& outputPath) {
    size_t dotPos = outputPath.find_last_of('.');
    if (dotPos == std::string::npos) return outputPath + ".dot";
    return outputPath.substr(0, dotPos) + ".dot";
}

// Formatea time_t a una cadena legible
inline std::string formatTime(time_t t) {
    if (t == 0) return "N/A";
    char buf[64];
    struct tm* tm_info = localtime(&t);
    strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", tm_info);
    return std::string(buf);
}

// Obtiene la hora actual
inline time_t getCurrentTime() {
    return time(nullptr);
}

// Genera una firma de disco aleatoria
inline int generateSignature() {
    srand((unsigned)time(nullptr));
    return rand() % 1000000;
}

// Convierte tamano con unidad a bytes
inline int toBytes(int size, char unit) {
    switch (unit) {
        case 'k': case 'K': return size * 1024;
        case 'm': case 'M': return size * 1024 * 1024;
        default: return size * 1024; // por defecto K
    }
}

// Escapa cadenas para Graphviz
inline std::string escapeGraphviz(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '<') result += "&lt;";
        else if (c == '>') result += "&gt;";
        else if (c == '&') result += "&amp;";
        else if (c == '"') result += "&quot;";
        else if (c == '\n') result += "\\n";
        else if (c == '\0') continue;
        else result += c;
    }
    return result;
}

// Obtiene el nombre de archivo desde una ruta
inline std::string getFilename(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

// Obtiene el directorio desde una ruta
inline std::string getDirname(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return "/";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

#endif // UTILS_H
