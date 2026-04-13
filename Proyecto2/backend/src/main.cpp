#include <iostream>
#include <string>
#include "../include/httplib.h"
#include "../include/json.hpp"
#include "parser.h"
#include "structures.h"
#include "utils.h"
#include "disk_manager.h"
#include "mount_manager.h"
#include "fs_manager.h"
#include "user_manager.h"
#include "file_manager.h"
#include "reports.h"

using json = nlohmann::json;

// Declaración anticipada para el comando execute
std::string executeCommand(const ParsedCommand& cmd);

// Comando execute: lee un archivo de script desde disco y ejecuta cada línea
std::string cmd_execute(const std::map<std::string, std::string>& params) {
    if (params.find("path") == params.end())
        return "ERROR execute: Parámetro -path es obligatorio.";

    std::string path = params.at("path");

    std::ifstream scriptFile(path);
    if (!scriptFile.is_open())
        return "ERROR execute: No se pudo abrir el archivo '" + path + "'.";

    std::stringstream buffer;
    buffer << scriptFile.rdbuf();
    scriptFile.close();

    std::vector<std::string> lines = parseScript(buffer.str());
    std::string output;

    for (auto& line : lines) {
        ParsedCommand parsed = parseCommand(line);
        if (parsed.command.empty()) {
            // Comentario
            output += "# " + line + "\n";
            continue;
        }
        std::string result = executeCommand(parsed);
        output += "> " + line + "\n";
        output += result + "\n";
    }

    return "Archivo ejecutado: '" + path + "'\n" + output;
}

// Ejecuta un comando parseado y devuelve el resultado
std::string executeCommand(const ParsedCommand& cmd) {
    if (cmd.command.empty()) return "";

    if (cmd.command == "execute") return cmd_execute(cmd.params);
    if (cmd.command == "mkdisk") return cmd_mkdisk(cmd.params);
    if (cmd.command == "rmdisk") return cmd_rmdisk(cmd.params);
    if (cmd.command == "fdisk") return cmd_fdisk(cmd.params);
    if (cmd.command == "mount") return cmd_mount(cmd.params);
    if (cmd.command == "mounted") return cmd_mounted();
    if (cmd.command == "unmount") return cmd_unmount(cmd.params);
    if (cmd.command == "mkfs") return cmd_mkfs(cmd.params);
    if (cmd.command == "login") return cmd_login(cmd.params);
    if (cmd.command == "logout") return cmd_logout();
    if (cmd.command == "mkgrp") return cmd_mkgrp(cmd.params);
    if (cmd.command == "rmgrp") return cmd_rmgrp(cmd.params);
    if (cmd.command == "mkusr") return cmd_mkusr(cmd.params);
    if (cmd.command == "rmusr") return cmd_rmusr(cmd.params);
    if (cmd.command == "chgrp") return cmd_chgrp(cmd.params);
    if (cmd.command == "mkdir") return cmd_mkdir(cmd.params);
    if (cmd.command == "mkfile") return cmd_mkfile(cmd.params);
    if (cmd.command == "cat") return cmd_cat(cmd.params);
    if (cmd.command == "rep") return cmd_rep(cmd.params);

    return "ERROR: Comando '" + cmd.command + "' no reconocido.";
}

int main() {
    httplib::Server svr;

    // ==================== CORS ====================
    svr.Options("/(.*)", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    // ==================== POST /api/execute ====================
    svr.Post("/api/execute", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");

        try {
            json body = json::parse(req.body);
            std::string command = body.value("command", "");

            if (command.empty()) {
                json resp;
                resp["success"] = false;
                resp["message"] = "Comando vacío.";
                resp["output"] = "";
                res.set_content(resp.dump(), "application/json");
                return;
            }

            ParsedCommand parsed = parseCommand(command);

            // Comentarios
            if (parsed.command.empty()) {
                json resp;
                resp["success"] = true;
                resp["message"] = "Comentario";
                resp["output"] = command;
                res.set_content(resp.dump(), "application/json");
                return;
            }

            std::string result = executeCommand(parsed);

            json resp;
            bool isError = result.substr(0, 5) == "ERROR";
            resp["success"] = !isError;
            resp["message"] = result;
            resp["output"] = result;
            res.set_content(resp.dump(), "application/json");
        } catch (const std::exception& e) {
            json resp;
            resp["success"] = false;
            resp["message"] = std::string("Error interno: ") + e.what();
            resp["output"] = "";
            res.set_content(resp.dump(), "application/json");
        }
    });

    // ==================== POST /api/execute-script ====================
    svr.Post("/api/execute-script", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");

        try {
            json body = json::parse(req.body);
            std::string script = body.value("script", "");

            std::vector<std::string> lines = parseScript(script);
            json results = json::array();

            for (auto& line : lines) {
                ParsedCommand parsed = parseCommand(line);
                json entry;

                if (parsed.command.empty()) {
                    // Comentario o línea vacía
                    entry["success"] = true;
                    entry["message"] = "Comentario";
                    entry["output"] = line;
                } else {
                    std::string result = executeCommand(parsed);
                    bool isError = result.substr(0, 5) == "ERROR";
                    entry["success"] = !isError;
                    entry["message"] = result;
                    entry["output"] = result;
                }

                results.push_back(entry);
            }

            res.set_content(results.dump(), "application/json");
        } catch (const std::exception& e) {
            json results = json::array();
            json entry;
            entry["success"] = false;
            entry["message"] = std::string("Error interno: ") + e.what();
            entry["output"] = "";
            results.push_back(entry);
            res.set_content(results.dump(), "application/json");
        }
    });

    // ==================== GET /api/mounted ====================
    svr.Get("/api/mounted", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");

        auto& mounted = getMountedList();
        json result = json::array();

        for (auto& m : mounted) {
            json entry;
            entry["id"] = std::string(m.id);
            entry["path"] = std::string(m.path);
            entry["name"] = std::string(m.name);
            entry["status"] = m.logged_in ? "logged" : "mounted";
            result.push_back(entry);
        }

        res.set_content(result.dump(), "application/json");
    });

    // ==================== GET /api/report ====================
    svr.Get("/api/report", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");

        std::string path = req.get_param_value("path");
        if (path.empty()) {
            res.status = 400;
            res.set_content("Missing path parameter", "text/plain");
            return;
        }

        // Lee el archivo y devuelve su contenido
        std::ifstream file(path);
        if (!file.is_open()) {
            res.status = 404;
            res.set_content("Report file not found", "text/plain");
            return;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        file.close();

        // Determina el tipo de contenido
        std::string ext = path.substr(path.find_last_of('.') + 1);
        if (ext == "svg") {
            res.set_content(content, "image/svg+xml");
        } else if (ext == "png") {
            // Para archivos binarios, leer de forma distinta
            std::ifstream binFile(path, std::ios::binary);
            std::string binContent((std::istreambuf_iterator<char>(binFile)),
                                    std::istreambuf_iterator<char>());
            binFile.close();
            res.set_content(binContent, "image/png");
        } else if (ext == "jpg" || ext == "jpeg") {
            std::ifstream binFile(path, std::ios::binary);
            std::string binContent((std::istreambuf_iterator<char>(binFile)),
                                    std::istreambuf_iterator<char>());
            binFile.close();
            res.set_content(binContent, "image/jpeg");
        } else if (ext == "pdf") {
            std::ifstream binFile(path, std::ios::binary);
            std::string binContent((std::istreambuf_iterator<char>(binFile)),
                                    std::istreambuf_iterator<char>());
            binFile.close();
            res.set_content(binContent, "application/pdf");
        } else {
            res.set_content(content, "text/plain");
        }
    });

    std::cout << "========================================" << std::endl;
    std::cout << "  MIA Proyecto 1 - Backend Server" << std::endl;
    std::cout << "  Carnet: 202100171" << std::endl;
    std::cout << "  Servidor corriendo en puerto 8080" << std::endl;
    std::cout << "========================================" << std::endl;

    svr.listen("0.0.0.0", 8080);

    return 0;
}
