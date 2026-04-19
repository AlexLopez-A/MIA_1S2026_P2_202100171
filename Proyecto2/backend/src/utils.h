#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <ctime>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdio>
#include "structures.h"

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

inline bool appendJournalEntry(const MountedPart* mp,
                               const std::string& operation,
                               const std::string& path,
                               const std::string& content) {
    if (!mp || operation.empty()) return false;

    FILE* file = fopen(mp->path, "rb+");
    if (!file) return false;

    Superblock sb;
    fseek(file, mp->part_start, SEEK_SET);
    if (fread(&sb, sizeof(Superblock), 1, file) != 1) {
        fclose(file);
        return false;
    }

    // Solo EXT3 tiene journaling
    if (sb.s_filesystem_type != 3) {
        fclose(file);
        return false;
    }

    int journalStart = mp->part_start + (int)sizeof(Superblock);

    Journal entry{};
    entry.j_count = 1;

    std::string op = operation.substr(0, sizeof(entry.j_content.i_operation) - 1);
    std::string p = path.substr(0, sizeof(entry.j_content.i_path) - 1);
    std::string c = content.substr(0, sizeof(entry.j_content.i_content) - 1);

    strncpy(entry.j_content.i_operation, op.c_str(), sizeof(entry.j_content.i_operation) - 1);
    strncpy(entry.j_content.i_path, p.c_str(), sizeof(entry.j_content.i_path) - 1);
    strncpy(entry.j_content.i_content, c.c_str(), sizeof(entry.j_content.i_content) - 1);
    entry.j_content.i_date = getCurrentTime();

    int writeIndex = -1;
    for (int i = 0; i < 50; i++) {
        Journal current;
        fseek(file, journalStart + i * (int)sizeof(Journal), SEEK_SET);
        if (fread(&current, sizeof(Journal), 1, file) != 1) break;

        if (current.j_content.i_operation[0] == '\0') {
            writeIndex = i;
            break;
        }
    }

    // Si ya está lleno, sobrescribir la última entrada.
    if (writeIndex == -1) writeIndex = 49;

    fseek(file, journalStart + writeIndex * (int)sizeof(Journal), SEEK_SET);
    bool ok = fwrite(&entry, sizeof(Journal), 1, file) == 1;
    fclose(file);
    return ok;
}

#endif // UTILS_H
