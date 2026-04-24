#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>
#include <sstream>
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

namespace {

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string sanitizeCharArray(const char* value, size_t maxLen) {
    size_t len = strnlen(value, maxLen);
    return std::string(value, len);
}

std::string fitToLabel(char fit) {
    switch (std::toupper(static_cast<unsigned char>(fit))) {
        case 'B': return "Best";
        case 'F': return "First";
        case 'W': return "Worst";
        default: return "Unknown";
    }
}

std::string bytesToHuman(long long bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = static_cast<double>(bytes);
    int idx = 0;
    while (size >= 1024.0 && idx < 4) {
        size /= 1024.0;
        idx++;
    }
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(size < 10.0 && idx > 0 ? 2 : 1);
    out << size << ' ' << units[idx];
    return out.str();
}

std::string normalizeVirtualPath(const std::string& rawPath) {
    if (rawPath.empty()) return "/";

    std::string path = rawPath;
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.front() != '/') path = "/" + path;

    std::filesystem::path normalized(path);
    normalized = normalized.lexically_normal();
    std::string result = normalized.string();
    if (result.empty()) return "/";
    if (result.front() != '/') result = "/" + result;
    if (result.size() > 1 && result.back() == '/') result.pop_back();
    return result;
}

bool startsWithPath(const std::filesystem::path& fullPath, const std::filesystem::path& basePath) {
    auto fullIt = fullPath.begin();
    auto baseIt = basePath.begin();
    while (baseIt != basePath.end()) {
        if (fullIt == fullPath.end() || *fullIt != *baseIt) {
            return false;
        }
        ++fullIt;
        ++baseIt;
    }
    return true;
}

bool resolveExplorerPath(const std::filesystem::path& root,
                        const std::string& virtualPath,
                        std::filesystem::path& resolved,
                        std::string& normalizedPath,
                        std::string& error) {
    try {
        normalizedPath = normalizeVirtualPath(virtualPath);
        std::filesystem::path relative = normalizedPath == "/"
            ? std::filesystem::path()
            : std::filesystem::path(normalizedPath.substr(1));

        std::filesystem::path candidate = root / relative;
        resolved = std::filesystem::weakly_canonical(candidate);

        if (!startsWithPath(resolved, root)) {
            error = "Ruta inválida.";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::string fsPermissionsToString(std::filesystem::perms p) {
    auto bit = [p](std::filesystem::perms flag, char on) {
        return (p & flag) != std::filesystem::perms::none ? on : '-';
    };
    std::string out;
    out.reserve(9);
    out.push_back(bit(std::filesystem::perms::owner_read, 'r'));
    out.push_back(bit(std::filesystem::perms::owner_write, 'w'));
    out.push_back(bit(std::filesystem::perms::owner_exec, 'x'));
    out.push_back(bit(std::filesystem::perms::group_read, 'r'));
    out.push_back(bit(std::filesystem::perms::group_write, 'w'));
    out.push_back(bit(std::filesystem::perms::group_exec, 'x'));
    out.push_back(bit(std::filesystem::perms::others_read, 'r'));
    out.push_back(bit(std::filesystem::perms::others_write, 'w'));
    out.push_back(bit(std::filesystem::perms::others_exec, 'x'));
    return out;
}

json buildTreeNode(const std::filesystem::path& realPath,
                   const std::string& virtualPath,
                   int depth,
                   int maxDepth,
                   int& nodeCount,
                   int maxNodes) {
    json node;
    std::string name = (virtualPath == "/")
        ? "/"
        : realPath.filename().string();

    node["name"] = name;
    node["path"] = virtualPath;
    node["permissions"] = fsPermissionsToString(std::filesystem::status(realPath).permissions());

    const bool isDir = std::filesystem::is_directory(realPath);
    node["type"] = isDir ? "directory" : "file";

    if (!isDir) {
        long long fileSize = 0;
        try {
            fileSize = static_cast<long long>(std::filesystem::file_size(realPath));
        } catch (...) {
            fileSize = 0;
        }
        node["sizeBytes"] = fileSize;
        node["sizeHuman"] = bytesToHuman(fileSize);
        return node;
    }

    node["children"] = json::array();
    if (depth >= maxDepth || nodeCount >= maxNodes) {
        return node;
    }

    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(realPath)) {
        entries.push_back(entry);
    }

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        const bool aDir = a.is_directory();
        const bool bDir = b.is_directory();
        if (aDir != bDir) return aDir > bDir;
        return toLowerCopy(a.path().filename().string()) < toLowerCopy(b.path().filename().string());
    });

    for (const auto& entry : entries) {
        if (nodeCount >= maxNodes) break;
        nodeCount++;

        std::string childPath = virtualPath;
        if (childPath != "/") childPath += "/";
        childPath += entry.path().filename().string();

        node["children"].push_back(buildTreeNode(entry.path(), childPath, depth + 1, maxDepth, nodeCount, maxNodes));
    }

    return node;
}

bool readDiskMbr(const std::string& path, MBR& mbr) {
    FILE* disk = fopen(path.c_str(), "rb");
    if (!disk) return false;
    fseek(disk, 0, SEEK_SET);
    size_t readCount = fread(&mbr, sizeof(MBR), 1, disk);
    fclose(disk);
    return readCount == 1;
}

const MountedPart* findMounted(const std::string& diskPath, const std::string& partName) {
    auto& mounted = getMountedList();
    for (const auto& m : mounted) {
        if (diskPath == std::string(m.path) && partName == std::string(m.name)) {
            return &m;
        }
    }
    return nullptr;
}

} // namespace

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
    if (cmd.command == "remove") return cmd_remove(cmd.params);
    if (cmd.command == "rename") return cmd_rename(cmd.params);
    if (cmd.command == "copy") return cmd_copy(cmd.params);
    if (cmd.command == "move") return cmd_move(cmd.params);
    if (cmd.command == "find") return cmd_find(cmd.params);
    if (cmd.command == "chown") return cmd_chown(cmd.params);
    if (cmd.command == "chmod") return cmd_chmod(cmd.params);
    if (cmd.command == "loss") return cmd_loss(cmd.params);
    if (cmd.command == "journaling") return cmd_journaling(cmd.params);
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
            entry["currentUser"] = std::string(m.current_user);
            result.push_back(entry);
        }

        res.set_content(result.dump(), "application/json");
    });

    // ==================== GET /api/disks ====================
    svr.Get("/api/disks", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");

        std::set<std::string> diskPaths;
        for (const auto& m : getMountedList()) {
            diskPaths.insert(std::string(m.path));
        }

        try {
            std::filesystem::path diskDir("discos");
            if (std::filesystem::exists(diskDir) && std::filesystem::is_directory(diskDir)) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(diskDir)) {
                    if (!entry.is_regular_file()) continue;
                    std::string ext = toLowerCopy(entry.path().extension().string());
                    if (ext == ".mia" || ext == ".dsk") {
                        diskPaths.insert(std::filesystem::absolute(entry.path()).string());
                    }
                }
            }
        } catch (...) {}

        json disks = json::array();
        for (const auto& path : diskPaths) {
            MBR mbr;
            if (!readDiskMbr(path, mbr)) continue;

            json item;
            item["path"] = path;
            item["fileName"] = std::filesystem::path(path).filename().string();
            item["sizeBytes"] = mbr.mbr_tamano;
            item["sizeHuman"] = bytesToHuman(mbr.mbr_tamano);
            item["fit"] = fitToLabel(mbr.mbr_disk_fit);
            item["mountedPartitions"] = json::array();

            for (const auto& mounted : getMountedList()) {
                if (path == std::string(mounted.path)) {
                    json p;
                    p["id"] = std::string(mounted.id);
                    p["name"] = std::string(mounted.name);
                    p["status"] = mounted.logged_in ? "logged" : "mounted";
                    item["mountedPartitions"].push_back(p);
                }
            }
            item["mountedCount"] = item["mountedPartitions"].size();
            disks.push_back(item);
        }

        std::sort(disks.begin(), disks.end(), [](const json& a, const json& b) {
            return a.value("fileName", "") < b.value("fileName", "");
        });

        res.set_content(disks.dump(), "application/json");
    });

    // ==================== GET /api/disks/partitions ====================
    svr.Get("/api/disks/partitions", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");

        if (!req.has_param("path")) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing path\"}", "application/json");
            return;
        }

        std::string diskPath = req.get_param_value("path");
        MBR mbr;
        if (!readDiskMbr(diskPath, mbr)) {
            res.status = 404;
            res.set_content("{\"error\":\"Disk not found\"}", "application/json");
            return;
        }

        json response;
        response["disk"]["path"] = diskPath;
        response["disk"]["fileName"] = std::filesystem::path(diskPath).filename().string();
        response["disk"]["sizeBytes"] = mbr.mbr_tamano;
        response["disk"]["sizeHuman"] = bytesToHuman(mbr.mbr_tamano);
        response["disk"]["fit"] = fitToLabel(mbr.mbr_disk_fit);
        response["partitions"] = json::array();

        FILE* disk = fopen(diskPath.c_str(), "rb");
        if (!disk) {
            res.status = 404;
            res.set_content("{\"error\":\"Disk not found\"}", "application/json");
            return;
        }

        for (int i = 0; i < 4; i++) {
            const Partition& p = mbr.mbr_partitions[i];
            if (p.part_status != '1' || p.part_start == -1 || p.part_size <= 0) continue;

            std::string partName = sanitizeCharArray(p.part_name, 16);
            const MountedPart* mounted = findMounted(diskPath, partName);

            json part;
            part["name"] = partName;
            part["type"] = (std::toupper(static_cast<unsigned char>(p.part_type)) == 'E') ? "extended" : "primary";
            part["fit"] = fitToLabel(p.part_fit);
            part["sizeBytes"] = p.part_size;
            part["sizeHuman"] = bytesToHuman(p.part_size);
            part["start"] = p.part_start;
            part["status"] = mounted ? (mounted->logged_in ? "logged" : "mounted") : "unmounted";
            part["mountId"] = mounted ? std::string(mounted->id) : "";
            response["partitions"].push_back(part);

            if (std::toupper(static_cast<unsigned char>(p.part_type)) == 'E') {
                int ebrPos = p.part_start;
                int guard = 0;
                while (ebrPos != -1 && guard < 256) {
                    EBR ebr;
                    fseek(disk, ebrPos, SEEK_SET);
                    size_t readCount = fread(&ebr, sizeof(EBR), 1, disk);
                    if (readCount != 1) break;

                    std::string ebrName = sanitizeCharArray(ebr.part_name, 16);
                    bool hasLogicalData = ebr.part_size > 0 || !ebrName.empty() || ebr.part_next != -1;
                    if (!hasLogicalData && guard > 0) break;

                    const MountedPart* logicalMounted = findMounted(diskPath, ebrName);

                    json logical;
                    logical["name"] = ebrName.empty() ? "(sin nombre)" : ebrName;
                    logical["type"] = "logical";
                    logical["fit"] = fitToLabel(ebr.part_fit);
                    logical["sizeBytes"] = ebr.part_size;
                    logical["sizeHuman"] = bytesToHuman(ebr.part_size);
                    logical["start"] = ebr.part_start == -1 ? ebrPos : ebr.part_start;
                    logical["status"] = logicalMounted ? (logicalMounted->logged_in ? "logged" : "mounted") : "unmounted";
                    logical["mountId"] = logicalMounted ? std::string(logicalMounted->id) : "";
                    response["partitions"].push_back(logical);

                    if (ebr.part_next == -1 || ebr.part_next == ebrPos) break;
                    ebrPos = ebr.part_next;
                    guard++;
                }
            }
        }

        fclose(disk);
        res.set_content(response.dump(), "application/json");
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

    
    svr.Get("/api/tree", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        std::map<std::string, std::string> params;
        params["path"] = req.has_param("path") ? req.get_param_value("path") : "/";
        params["name"] = req.has_param("name") ? req.get_param_value("name") : "*";
        std::string tree = cmd_find(params);
        json out; out["tree"] = tree;
        res.set_content(out.dump(), "application/json");
    });

    // ==================== GET /api/fs/tree ====================
    svr.Get("/api/fs/tree", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");

        if (!req.has_param("id")) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing id\"}", "application/json");
            return;
        }

        std::string id = req.get_param_value("id");
        MountedPart* mp = getMountedById(id);
        if (!mp) {
            res.status = 404;
            res.set_content("{\"error\":\"Partition not mounted\"}", "application/json");
            return;
        }

        std::string partFolder = "archivos/" + std::string(mp->id);
        try { std::filesystem::create_directories(partFolder); } catch (...) {}
        std::filesystem::path root = std::filesystem::weakly_canonical(std::filesystem::path(partFolder));
        if (!std::filesystem::exists(root)) {
            res.status = 500;
            res.set_content("{\"error\":\"Local explorer root not found\"}", "application/json");
            return;
        }

        std::string requestedPath = req.has_param("path") ? req.get_param_value("path") : "/";
        std::filesystem::path target;
        std::string normalizedPath;
        std::string error;
        if (!resolveExplorerPath(root, requestedPath, target, normalizedPath, error)) {
            res.status = 400;
            json out;
            out["error"] = error;
            res.set_content(out.dump(), "application/json");
            return;
        }

        if (!std::filesystem::exists(target)) {
            res.status = 404;
            res.set_content("{\"error\":\"Path not found\"}", "application/json");
            return;
        }

        if (!std::filesystem::is_directory(target)) {
            res.status = 400;
            res.set_content("{\"error\":\"Path is not a directory\"}", "application/json");
            return;
        }

        int nodeCount = 1;
        json out;
        out["partitionId"] = id;
        out["root"] = buildTreeNode(target, normalizedPath, 0, 12, nodeCount, 2500);
        out["truncated"] = nodeCount >= 2500;
        res.set_content(out.dump(), "application/json");
    });

    // ==================== GET /api/fs/file-content ====================
    svr.Get("/api/fs/file-content", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");

        if (!req.has_param("id") || !req.has_param("path")) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing id or path\"}", "application/json");
            return;
        }

        std::string id = req.get_param_value("id");
        MountedPart* mp = getMountedById(id);
        if (!mp) {
            res.status = 404;
            res.set_content("{\"error\":\"Partition not mounted\"}", "application/json");
            return;
        }

        std::string partFolder = "archivos/" + std::string(mp->id);
        try { std::filesystem::create_directories(partFolder); } catch (...) {}
        std::filesystem::path root = std::filesystem::weakly_canonical(std::filesystem::path(partFolder));
        if (!std::filesystem::exists(root)) {
            res.status = 500;
            res.set_content("{\"error\":\"Local explorer root not found\"}", "application/json");
            return;
        }

        std::filesystem::path target;
        std::string normalizedPath;
        std::string error;
        if (!resolveExplorerPath(root, req.get_param_value("path"), target, normalizedPath, error)) {
            res.status = 400;
            json out;
            out["error"] = error;
            res.set_content(out.dump(), "application/json");
            return;
        }

        if (!std::filesystem::exists(target) || !std::filesystem::is_regular_file(target)) {
            res.status = 404;
            res.set_content("{\"error\":\"File not found\"}", "application/json");
            return;
        }

        long long sizeBytes = 0;
        try {
            sizeBytes = static_cast<long long>(std::filesystem::file_size(target));
        } catch (...) {
            sizeBytes = 0;
        }

        if (sizeBytes > 1024 * 1024) {
            res.status = 413;
            res.set_content("{\"error\":\"File is too large to preview\"}", "application/json");
            return;
        }

        std::ifstream in(target);
        if (!in.is_open()) {
            res.status = 500;
            res.set_content("{\"error\":\"Unable to read file\"}", "application/json");
            return;
        }

        std::stringstream buffer;
        buffer << in.rdbuf();

        json out;
        out["partitionId"] = id;
        out["path"] = normalizedPath;
        out["name"] = target.filename().string();
        out["sizeBytes"] = sizeBytes;
        out["sizeHuman"] = bytesToHuman(sizeBytes);
        out["permissions"] = fsPermissionsToString(std::filesystem::status(target).permissions());
        out["content"] = buffer.str();
        res.set_content(out.dump(), "application/json");
    });
    
    svr.Get("/api/journal", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        std::map<std::string, std::string> params;
        if(req.has_param("id")) params["id"] = req.get_param_value("id");
        std::string j = cmd_journaling(params);
        json out; out["journal"] = j;
        res.set_content(out.dump(), "application/json");
    });

    // ==================== GET /api/journal-table ====================
    svr.Get("/api/journal-table", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");

        if (!req.has_param("id")) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing id\"}", "application/json");
            return;
        }

        std::string id = req.get_param_value("id");
        MountedPart* mp = getMountedById(id);
        if (!mp) {
            res.status = 404;
            res.set_content("{\"error\":\"Partition not mounted\"}", "application/json");
            return;
        }

        FILE* file = fopen(mp->path, "rb");
        if (!file) {
            res.status = 500;
            res.set_content("{\"error\":\"Unable to open disk\"}", "application/json");
            return;
        }

        Superblock sb;
        fseek(file, mp->part_start, SEEK_SET);
        fread(&sb, sizeof(Superblock), 1, file);

        json out;
        out["partitionId"] = id;
        out["filesystemType"] = sb.s_filesystem_type;
        out["entries"] = json::array();

        if (sb.s_filesystem_type != 3) {
            fclose(file);
            out["message"] = "La partición no es EXT3, no tiene journaling.";
            res.set_content(out.dump(), "application/json");
            return;
        }

        int journalStart = mp->part_start + static_cast<int>(sizeof(Superblock));
        fseek(file, journalStart, SEEK_SET);

        int row = 0;
        for (int i = 0; i < 50; i++) {
            Journal j;
            size_t readCount = fread(&j, sizeof(Journal), 1, file);
            if (readCount != 1) break;

            if (j.j_content.i_operation[0] == '\0') {
                continue;
            }

            char dateBuff[20] = {0};
            struct tm* tmInfo = localtime(&j.j_content.i_date);
            if (tmInfo) {
                strftime(dateBuff, sizeof(dateBuff), "%Y-%m-%d %H:%M", tmInfo);
            }

            json entry;
            entry["index"] = ++row;
            entry["operation"] = sanitizeCharArray(j.j_content.i_operation, sizeof(j.j_content.i_operation));
            entry["path"] = sanitizeCharArray(j.j_content.i_path, sizeof(j.j_content.i_path));
            entry["content"] = sanitizeCharArray(j.j_content.i_content, sizeof(j.j_content.i_content));
            entry["date"] = std::string(dateBuff);
            out["entries"].push_back(entry);
        }

        fclose(file);
        out["message"] = row == 0 ? "Sin entradas de journaling." : "OK";
        res.set_content(out.dump(), "application/json");
    });

    svr.listen("0.0.0.0", 8080);

    return 0;
}
