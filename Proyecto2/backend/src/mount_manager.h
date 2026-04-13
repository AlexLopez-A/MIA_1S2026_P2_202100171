#ifndef MOUNT_MANAGER_H
#define MOUNT_MANAGER_H

#include "structures.h"
#include "parser.h"
#include "utils.h"
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <map>

// Lista global de particiones montadas
inline std::vector<MountedPart>& getMountedList() {
    static std::vector<MountedPart> mounted;
    return mounted;
}

// Rastrea letras de disco: ruta -> letra
inline std::map<std::string, char>& getDiskLetters() {
    static std::map<std::string, char> letters;
    return letters;
}

// Rastrea cantidad de particiones por disco: ruta -> cantidad
inline std::map<std::string, int>& getDiskPartCount() {
    static std::map<std::string, int> counts;
    return counts;
}

// Genera ID de montaje: carnet=202100171, ultimos 2 digitos = "71"
// Formato: "71" + numeroParticion + letraDisco
inline std::string generateMountId(const std::string& diskPath, int partNumber) {
    auto& letters = getDiskLetters();
    if (letters.find(diskPath) == letters.end()) {
        // Asignar la siguiente letra disponible
        char nextLetter = 'A' + (char)letters.size();
        letters[diskPath] = nextLetter;
    }
    char letter = letters[diskPath];

    // Carnet 202100171 -> ultimos 2 digitos: "71"
    std::string id = "71" + std::to_string(partNumber) + letter;
    return id;
}

// Verifica si la particion ya esta montada
inline bool isAlreadyMounted(const std::string& diskPath, const std::string& partName) {
    auto& mounted = getMountedList();
    for (auto& m : mounted) {
        if (std::string(m.path) == diskPath && std::string(m.name) == partName) {
            return true;
        }
    }
    return false;
}

// Obtiene una particion montada por ID
inline MountedPart* getMountedById(const std::string& id) {
    auto& mounted = getMountedList();
    for (auto& m : mounted) {
        if (std::string(m.id) == id) {
            return &m;
        }
    }
    return nullptr;
}

// Lleva el contador correlativo
inline int& getNextCorrelative() {
    static int nextCorr = 1;
    return nextCorr;
}

// ==================== MOUNT ====================
inline std::string cmd_mount(const std::map<std::string, std::string>& params) {
    if (params.find("path") == params.end())
        return "ERROR mount: Parámetro -path es obligatorio.";
    if (params.find("name") == params.end())
        return "ERROR mount: Parámetro -name es obligatorio.";

    std::string path = params.at("path");
    std::string name = params.at("name");

    if (!fileExists(path))
        return "ERROR mount: El disco no existe en: " + path;

    // Verificar si ya esta montada
    if (isAlreadyMounted(path, name))
        return "ERROR mount: La partición '" + name + "' ya está montada.";

    // Leer MBR y buscar particion
    FILE* file = fopen(path.c_str(), "rb+");
    if (!file) return "ERROR mount: No se pudo abrir el disco.";

    MBR mbr;
    fseek(file, 0, SEEK_SET);
    fread(&mbr, sizeof(MBR), 1, file);

    // Buscar en particiones primarias/extendidas
    int partStart = -1;
    int foundPartIdx = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status == '1' &&
            std::string(mbr.mbr_partitions[i].part_name) == name) {
            partStart = mbr.mbr_partitions[i].part_start;
            foundPartIdx = i;
            break;
        }
    }

    // Buscar en particiones logicas
    if (partStart == -1) {
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '1' &&
                mbr.mbr_partitions[i].part_type == 'E') {
                int ebrPos = mbr.mbr_partitions[i].part_start;
                while (ebrPos != -1) {
                    EBR ebr;
                    fseek(file, ebrPos, SEEK_SET);
                    fread(&ebr, sizeof(EBR), 1, file);
                    if (ebr.part_mount == '1' && std::string(ebr.part_name) == name) {
                        partStart = ebr.part_start;
                        break;
                    }
                    ebrPos = ebr.part_next;
                }
                if (partStart != -1) break;
            }
        }
    }

    if (partStart == -1) {
        fclose(file);
        return "ERROR mount: No se encontró la partición '" + name + "'.";
    }

    // Generar ID
    auto& partCount = getDiskPartCount();
    if (partCount.find(path) == partCount.end()) partCount[path] = 0;
    partCount[path]++;
    std::string id = generateMountId(path, partCount[path]);

    // Establecer part_correlative y part_id en disco
    int corr = getNextCorrelative()++;
    if (foundPartIdx != -1) {
        mbr.mbr_partitions[foundPartIdx].part_correlative = corr;
        strncpy(mbr.mbr_partitions[foundPartIdx].part_id, id.c_str(), 4);
        fseek(file, 0, SEEK_SET);
        fwrite(&mbr, sizeof(MBR), 1, file);
    }
    // (las particiones logicas no tienen part_correlative/part_id en EBR)

    fclose(file);

    // Agregar a la lista de montadas
    MountedPart mp;
    strncpy(mp.id, id.c_str(), 9);
    strncpy(mp.path, path.c_str(), 199);
    strncpy(mp.name, name.c_str(), 15);
    mp.part_start = partStart;
    mp.logged_in = false;

    getMountedList().push_back(mp);

    return "Partición '" + name + "' montada con ID: " + id;
}

// ==================== MOUNTED ====================
inline std::string cmd_mounted() {
    auto& mounted = getMountedList();
    if (mounted.empty())
        return "No hay particiones montadas.";

    std::string result = "=== PARTICIONES MONTADAS ===\n";
    result += "ID\t\tDisco\t\t\t\t\tPartición\n";
    result += "---\t\t-----\t\t\t\t\t---------\n";
    for (auto& m : mounted) {
        result += std::string(m.id) + "\t\t" + std::string(m.path) + "\t\t" + std::string(m.name) + "\n";
    }
    return result;
}

// ==================== UNMOUNT ====================
inline std::string cmd_unmount(const std::map<std::string, std::string>& params) {
    if (params.find("id") == params.end())
        return "ERROR unmount: Parámetro -id es obligatorio.";

    std::string id = params.at("id");
    auto& mounted = getMountedList();

    for (auto it = mounted.begin(); it != mounted.end(); ++it) {
        if (std::string(it->id) == id) {
            std::string name = std::string(it->name);
            std::string diskPath = std::string(it->path);

            // Reiniciar part_correlative y part_id en disco
            FILE* file = fopen(diskPath.c_str(), "rb+");
            if (file) {
                MBR mbr;
                fseek(file, 0, SEEK_SET);
                fread(&mbr, sizeof(MBR), 1, file);
                for (int i = 0; i < 4; i++) {
                    if (mbr.mbr_partitions[i].part_status == '1' &&
                        std::string(mbr.mbr_partitions[i].part_name) == name) {
                        mbr.mbr_partitions[i].part_correlative = -1;
                        memset(mbr.mbr_partitions[i].part_id, 0, 4);
                        fseek(file, 0, SEEK_SET);
                        fwrite(&mbr, sizeof(MBR), 1, file);
                        break;
                    }
                }
                fclose(file);
            }

            mounted.erase(it);
            return "Partición '" + name + "' desmontada exitosamente.";
        }
    }

    return "ERROR unmount: No se encontró la partición con ID '" + id + "'.";
}

#endif // MOUNT_MANAGER_H
