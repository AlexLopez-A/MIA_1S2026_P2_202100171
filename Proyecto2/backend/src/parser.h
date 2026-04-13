#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <sstream>

struct ParsedCommand {
    std::string command;
    std::map<std::string, std::string> params;
};

// Elimina espacios en blanco al inicio y al final
inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

// Convierte una cadena a minusculas
inline std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// Analiza una linea de comando en nombre de comando + parametros
inline ParsedCommand parseCommand(const std::string& input) {
    ParsedCommand parsed;
    std::string line = trim(input);

    if (line.empty() || line[0] == '#') {
        parsed.command = "";
        return parsed;
    }

    // Extrae el nombre del comando (primer token)
    size_t firstSpace = line.find_first_of(" \t");
    if (firstSpace == std::string::npos) {
        parsed.command = toLower(line);
        return parsed;
    }

    parsed.command = toLower(line.substr(0, firstSpace));
    std::string rest = line.substr(firstSpace);

    // Analiza parametros: -key=value o -key="valor con espacios"
    size_t i = 0;
    while (i < rest.size()) {
        // Omitir espacios en blanco
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) i++;
        if (i >= rest.size()) break;

        // Se espera '-'
        if (rest[i] == '-') {
            i++; // skip '-'
            // Leer clave
            std::string key;
            while (i < rest.size() && rest[i] != '=' && rest[i] != ' ' && rest[i] != '\t') {
                key += rest[i];
                i++;
            }
            key = toLower(key);

            std::string value;
            if (i < rest.size() && rest[i] == '=') {
                i++; // skip '='
                if (i < rest.size() && rest[i] == '"') {
                    // Valor entre comillas
                    i++; // skip opening quote
                    while (i < rest.size() && rest[i] != '"') {
                        value += rest[i];
                        i++;
                    }
                    if (i < rest.size()) i++; // skip closing quote
                } else {
                    // Valor sin comillas
                    while (i < rest.size() && rest[i] != ' ' && rest[i] != '\t') {
                        value += rest[i];
                        i++;
                    }
                }
            }
            parsed.params[key] = value;
        } else {
            // Omitir caracter inesperado
            i++;
        }
    }

    return parsed;
}

// Analiza un script multilinea en lineas de comandos individuales
inline std::vector<std::string> parseScript(const std::string& script) {
    std::vector<std::string> lines;
    std::istringstream stream(script);
    std::string line;
    while (std::getline(stream, line)) {
        std::string trimmed = trim(line);
        if (!trimmed.empty()) {
            lines.push_back(trimmed);
        }
    }
    return lines;
}

#endif // PARSER_H
