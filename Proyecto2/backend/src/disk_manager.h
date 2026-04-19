#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include "structures.h"
#include "parser.h"
#include "utils.h"
#include <string>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <vector>

// ==================== MKDISK ====================
inline std::string cmd_mkdisk(const std::map<std::string, std::string>& params) {
    // Requeridos: -size, -path
    // Opcionales: -fit (por defecto FF), -unit (por defecto M)

    // Validar parametros permitidos
    for (auto& kv : params) {
        if (kv.first != "size" && kv.first != "path" && kv.first != "fit" && kv.first != "unit")
            return "ERROR mkdisk: Parámetro -" + kv.first + " no es válido.";
    }

    if (params.find("size") == params.end())
        return "ERROR mkdisk: Parámetro -size es obligatorio.";
    if (params.find("path") == params.end())
        return "ERROR mkdisk: Parámetro -path es obligatorio.";

    int size = std::stoi(params.at("size"));
    if (size <= 0)
        return "ERROR mkdisk: El tamaño debe ser mayor a 0.";

    std::string path = params.at("path");

    char fit = 'F'; // por defecto First Fit
    if (params.find("fit") != params.end()) {
        std::string f = toLower(params.at("fit"));
        if (f == "bf") fit = 'B';
        else if (f == "ff") fit = 'F';
        else if (f == "wf") fit = 'W';
    }

    char unit = 'M'; // por defecto Megabytes
    if (params.find("unit") != params.end()) {
        std::string u = toLower(params.at("unit"));
        if (u == "k") unit = 'K';
        else if (u == "m") unit = 'M';
    }

    int totalBytes = toBytes(size, unit);

    // Crear directorios padre
    createParentDirs(path);

    // Verificar si el archivo ya existe
    if (fileExists(path))
        return "ERROR mkdisk: El disco ya existe en: " + path;

    // Crear archivo binario lleno de ceros
    FILE* file = fopen(path.c_str(), "wb");
    if (!file)
        return "ERROR mkdisk: No se pudo crear el archivo en: " + path;

    // Escribir ceros en bloques de 1024 bytes
    char buffer[1024];
    memset(buffer, 0, 1024);
    int remaining = totalBytes;
    while (remaining > 0) {
        int toWrite = (remaining > 1024) ? 1024 : remaining;
        fwrite(buffer, 1, toWrite, file);
        remaining -= toWrite;
    }

    // Escribir MBR al inicio
    MBR mbr;
    mbr.mbr_tamano = totalBytes;
    mbr.mbr_fecha_creacion = getCurrentTime();
    mbr.mbr_disk_signature = generateSignature();
    mbr.mbr_disk_fit = fit;

    fseek(file, 0, SEEK_SET);
    fwrite(&mbr, sizeof(MBR), 1, file);
    fclose(file);

    return "Disco creado exitosamente en: " + path + " (" + std::to_string(totalBytes) + " bytes)";
}

// ==================== RMDISK ====================
inline std::string cmd_rmdisk(const std::map<std::string, std::string>& params) {
    if (params.find("path") == params.end())
        return "ERROR rmdisk: Parámetro -path es obligatorio.";

    std::string path = params.at("path");

    if (!fileExists(path))
        return "ERROR rmdisk: El disco no existe en: " + path;

    if (remove(path.c_str()) != 0)
        return "ERROR rmdisk: No se pudo eliminar el disco.";

    return "Disco eliminado exitosamente: " + path;
}

// ==================== FDISK ====================

// Auxiliar: encuentra el primer espacio suficiente para una particion de tamano dado
struct Gap {
    int start;
    int size;
};

inline std::vector<Gap> findGaps(MBR& mbr) {
    std::vector<Gap> gaps;
    int diskSize = mbr.mbr_tamano;

    // Recolectar rangos usados (inicio, fin)
    struct Range { int start; int end; };
    std::vector<Range> used;

    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status == '1') {
            used.push_back({mbr.mbr_partitions[i].part_start,
                           mbr.mbr_partitions[i].part_start + mbr.mbr_partitions[i].part_size});
        }
    }

    // Ordenar por inicio
    std::sort(used.begin(), used.end(), [](const Range& a, const Range& b) {
        return a.start < b.start;
    });

    // Buscar espacios libres
    int current = (int)sizeof(MBR);
    for (auto& r : used) {
        if (r.start > current) {
            gaps.push_back({current, r.start - current});
        }
        current = r.end;
    }
    if (current < diskSize) {
        gaps.push_back({current, diskSize - current});
    }

    return gaps;
}

inline int findStartByFit(std::vector<Gap>& gaps, int size, char fit) {
    if (gaps.empty()) return -1;

    int bestIdx = -1;
    for (int i = 0; i < (int)gaps.size(); i++) {
        if (gaps[i].size >= size) {
            if (bestIdx == -1) {
                bestIdx = i;
            } else {
                if (fit == 'F') {
                    // First fit: tomar el primero encontrado
                    break;
                } else if (fit == 'B') {
                    // Best fit: hueco mas pequeno que ajuste
                    if (gaps[i].size < gaps[bestIdx].size) bestIdx = i;
                } else if (fit == 'W') {
                    // Worst fit: hueco mas grande
                    if (gaps[i].size > gaps[bestIdx].size) bestIdx = i;
                }
            }
            if (fit == 'F') break;
        }
    }

    if (bestIdx == -1) return -1;
    return gaps[bestIdx].start;
}

inline std::string cmd_fdisk(const std::map<std::string, std::string>& params) {
    // Requeridos: -size, -path, -name
    // Opcionales: -unit (por defecto K), -type (por defecto P), -fit (por defecto WF)

    if (params.find("path") == params.end())
        return "ERROR fdisk: Parámetro -path es obligatorio.";

    std::string path = params.at("path");
    if (!fileExists(path))
        return "ERROR fdisk: El disco no existe en: " + path;

    // Verificar si es operacion de eliminacion
    if (params.find("delete") != params.end()) {
        if (params.find("name") == params.end())
            return "ERROR fdisk: Se requiere -name para eliminar partición.";

        std::string name = params.at("name");
        std::string delType = toLower(params.at("delete"));

        FILE* file = fopen(path.c_str(), "rb+");
        if (!file) return "ERROR fdisk: No se pudo abrir el disco.";

        MBR mbr;
        fseek(file, 0, SEEK_SET);
        fread(&mbr, sizeof(MBR), 1, file);

        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '1' &&
                std::string(mbr.mbr_partitions[i].part_name) == name) {
                
                int pStart = mbr.mbr_partitions[i].part_start;
                int pSize = mbr.mbr_partitions[i].part_size;
                
                mbr.mbr_partitions[i].part_status = '0';
                mbr.mbr_partitions[i].part_size = 0;
                fseek(file, 0, SEEK_SET);
                fwrite(&mbr, sizeof(MBR), 1, file);
                
                if (delType == "full") {
                    char zero = '\0';
                    fseek(file, pStart, SEEK_SET);
                    for (int j = 0; j < pSize; j++) {
                        fwrite(&zero, 1, 1, file);
                    }
                }
                
                fclose(file);
                return "Partición '" + name + "' eliminada exitosamente (" + delType + ").";
            }
        }

        // Verificar particiones logicas
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '1' &&
                mbr.mbr_partitions[i].part_type == 'E') {
                int ebrPos = mbr.mbr_partitions[i].part_start;
                while (ebrPos != -1) {
                    EBR ebr;
                    fseek(file, ebrPos, SEEK_SET);
                    fread(&ebr, sizeof(EBR), 1, file);
                    if (ebr.part_mount == '1' && std::string(ebr.part_name) == name) {
                        int pStart = ebr.part_start;
                        int pSize = ebr.part_size;
                        
                        ebr.part_mount = '0';
                        ebr.part_size = 0;
                        fseek(file, ebrPos, SEEK_SET);
                        fwrite(&ebr, sizeof(EBR), 1, file);
                        
                        if (delType == "full") {
                            char zero = '\0';
                            fseek(file, pStart, SEEK_SET);
                            for (int j = 0; j < pSize; j++) {
                                fwrite(&zero, 1, 1, file);
                            }
                        }
                        
                        fclose(file);
                        return "Partición lógica '" + name + "' eliminada exitosamente (" + delType + ").";
                    }
                    ebrPos = ebr.part_next;
                }
            }
        }

        fclose(file);
        return "ERROR fdisk: No se encontró la partición '" + name + "'.";
    }

    // Verificar si es operacion de redimensionar
    if (params.find("add") != params.end()) {
        if (params.find("name") == params.end())
            return "ERROR fdisk: Se requiere -name para redimensionar partición.";

        std::string name = params.at("name");
        int addSize = std::stoi(params.at("add"));
        
        char unit = 'K';
        if (params.find("unit") != params.end()) {
            std::string u = toLower(params.at("unit"));
            if (u == "m") unit = 'M';
            else if (u == "b") unit = 'B';
        }

        int addBytes = toBytes(std::abs(addSize), unit);
        if (addSize < 0) addBytes = -addBytes;

        FILE* file = fopen(path.c_str(), "rb+");
        if (!file) return "ERROR fdisk: No se pudo abrir el disco.";

        MBR mbr;
        fseek(file, 0, SEEK_SET);
        fread(&mbr, sizeof(MBR), 1, file);

        // Recolectar la ubicacion y tamanho de la particion a modificar
        int pIndex = -1;
        bool isLogic = false;
        int pStart = -1;
        int pSize = -1;
        int ebrPosToUpdate = -1;
        
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '1' &&
                std::string(mbr.mbr_partitions[i].part_name) == name) {
                pIndex = i;
                pStart = mbr.mbr_partitions[i].part_start;
                pSize = mbr.mbr_partitions[i].part_size;
                break;
            }
        }

        if (pIndex == -1) {
            // Verificar si es logica
            for (int i = 0; i < 4; i++) {
                if (mbr.mbr_partitions[i].part_status == '1' &&
                    mbr.mbr_partitions[i].part_type == 'E') {
                    int ebrPos = mbr.mbr_partitions[i].part_start;
                    while (ebrPos != -1) {
                        EBR ebr;
                        fseek(file, ebrPos, SEEK_SET);
                        fread(&ebr, sizeof(EBR), 1, file);
                        if (ebr.part_mount == '1' && std::string(ebr.part_name) == name) {
                            isLogic = true;
                            pStart = ebr.part_start;
                            pSize = ebr.part_size;
                            ebrPosToUpdate = ebrPos;
                            break;
                        }
                        ebrPos = ebr.part_next;
                    }
                    if (isLogic) break;
                }
            }
        }

        if (pIndex == -1 && !isLogic) {
            fclose(file);
            return "ERROR fdisk: No se encontró la partición '" + name + "' para redimensionar.";
        }

        int newSize = pSize + addBytes;
        if (newSize <= 0) {
            fclose(file);
            return "ERROR fdisk: El tamaño final de la partición resultaría <= 0.";
        }

        // Si es aumento, verificar solapamiento
        if (addBytes > 0) {
            // Buscar inicio de la siguiente particion
            int nextStart = mbr.mbr_tamano; // Limite del disco

            for (int i = 0; i < 4; i++) {
                if (mbr.mbr_partitions[i].part_status == '1') {
                    if (mbr.mbr_partitions[i].part_start > pStart) {
                        if (mbr.mbr_partitions[i].part_start < nextStart) {
                            nextStart = mbr.mbr_partitions[i].part_start;
                        }
                    }
                }
            }

            // Si es logica, buscar limite mas estricto (siguiente EBR)
            if (isLogic) {
                EBR currEbr;
                fseek(file, ebrPosToUpdate, SEEK_SET);
                fread(&currEbr, sizeof(EBR), 1, file);
                if (currEbr.part_next != -1) {
                    if (currEbr.part_next < nextStart) {
                        nextStart = currEbr.part_next;
                    }
                }
            }
            
            if (pStart + newSize > nextStart) {
                fclose(file);
                return "ERROR fdisk: No hay suficiente espacio libre (" + std::to_string(addBytes) + " b) después de la partición.";
            }
        }
        
        // Aplicar redimensionado
        if (!isLogic) {
            mbr.mbr_partitions[pIndex].part_size = newSize;
            fseek(file, 0, SEEK_SET);
            fwrite(&mbr, sizeof(MBR), 1, file);
        } else {
            EBR ebr;
            fseek(file, ebrPosToUpdate, SEEK_SET);
            fread(&ebr, sizeof(EBR), 1, file);
            ebr.part_size = newSize;
            fseek(file, ebrPosToUpdate, SEEK_SET);
            fwrite(&ebr, sizeof(EBR), 1, file);
        }

        fclose(file);
        return "Partición '" + name + "' " + (addBytes > 0 ? "ampliada" : "reducida") + " en " + std::to_string(std::abs(addBytes)) + " bytes exitosamente.";
    }

    // Crear particion
    if (params.find("size") == params.end())
        return "ERROR fdisk: Parámetro -size es obligatorio.";
    if (params.find("name") == params.end())
        return "ERROR fdisk: Parámetro -name es obligatorio.";

    int size = std::stoi(params.at("size"));
    if (size <= 0)
        return "ERROR fdisk: El tamaño debe ser mayor a 0.";

    std::string name = params.at("name");

    char unit = 'K';
    if (params.find("unit") != params.end()) {
        std::string u = toLower(params.at("unit"));
        if (u == "m") unit = 'M';
        else if (u == "b") unit = 'B';
        else unit = 'K';
    }

    int partSize;
    if (unit == 'B') partSize = size;
    else partSize = toBytes(size, unit);

    char type = 'P';
    if (params.find("type") != params.end()) {
        std::string t = toLower(params.at("type"));
        if (t == "p") type = 'P';
        else if (t == "e") type = 'E';
        else if (t == "l") type = 'L';
    }

    char fit = 'W';
    if (params.find("fit") != params.end()) {
        std::string f = toLower(params.at("fit"));
        if (f == "bf") fit = 'B';
        else if (f == "ff") fit = 'F';
        else if (f == "wf") fit = 'W';
    }

    // Leer MBR
    FILE* file = fopen(path.c_str(), "rb+");
    if (!file) return "ERROR fdisk: No se pudo abrir el disco.";

    MBR mbr;
    fseek(file, 0, SEEK_SET);
    fread(&mbr, sizeof(MBR), 1, file);

    // Verificar unicidad del nombre
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status == '1' &&
            std::string(mbr.mbr_partitions[i].part_name) == name) {
            fclose(file);
            return "ERROR fdisk: Ya existe una partición con el nombre '" + name + "'.";
        }
        // Verificar nombres logicos
        if (mbr.mbr_partitions[i].part_status == '1' &&
            mbr.mbr_partitions[i].part_type == 'E') {
            int ebrPos = mbr.mbr_partitions[i].part_start;
            while (ebrPos != -1) {
                EBR ebr;
                fseek(file, ebrPos, SEEK_SET);
                fread(&ebr, sizeof(EBR), 1, file);
                if (ebr.part_mount == '1' && std::string(ebr.part_name) == name) {
                    fclose(file);
                    return "ERROR fdisk: Ya existe una partición lógica con el nombre '" + name + "'.";
                }
                ebrPos = ebr.part_next;
            }
        }
    }

    // === PARTICION LOGICA ===
    if (type == 'L') {
        // Buscar particion extendida
        int extIdx = -1;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '1' &&
                mbr.mbr_partitions[i].part_type == 'E') {
                extIdx = i;
                break;
            }
        }
        if (extIdx == -1) {
            fclose(file);
            return "ERROR fdisk: No existe partición extendida. No se puede crear partición lógica.";
        }

        Partition& ext = mbr.mbr_partitions[extIdx];
        int extStart = ext.part_start;
        int extEnd = ext.part_start + ext.part_size;

        // Leer primer EBR
        EBR firstEbr;
        fseek(file, extStart, SEEK_SET);
        fread(&firstEbr, sizeof(EBR), 1, file);

        if (firstEbr.part_mount == '0') {
            // Primera particion logica
            firstEbr.part_mount = '1';
            firstEbr.part_fit = fit;
            firstEbr.part_start = extStart;
            firstEbr.part_size = (int)sizeof(EBR) + partSize;
            firstEbr.part_next = -1;
            strncpy(firstEbr.part_name, name.c_str(), 15);
            firstEbr.part_name[15] = '\0';

            if (firstEbr.part_start + firstEbr.part_size > extEnd) {
                fclose(file);
                return "ERROR fdisk: No hay espacio en la partición extendida.";
            }

            fseek(file, extStart, SEEK_SET);
            fwrite(&firstEbr, sizeof(EBR), 1, file);
            fclose(file);
            return "Partición lógica '" + name + "' creada exitosamente.";
        }

        // Buscar ultimo EBR
        int lastEbrPos = extStart;
        EBR lastEbr;
        fseek(file, lastEbrPos, SEEK_SET);
        fread(&lastEbr, sizeof(EBR), 1, file);

        while (lastEbr.part_next != -1) {
            lastEbrPos = lastEbr.part_next;
            fseek(file, lastEbrPos, SEEK_SET);
            fread(&lastEbr, sizeof(EBR), 1, file);
        }

        // Nuevo EBR despues del ultimo
        int newEbrPos = lastEbr.part_start + lastEbr.part_size;
        if (newEbrPos + (int)sizeof(EBR) + partSize > extEnd) {
            fclose(file);
            return "ERROR fdisk: No hay espacio en la partición extendida.";
        }

        EBR newEbr;
        newEbr.part_mount = '1';
        newEbr.part_fit = fit;
        newEbr.part_start = newEbrPos;
        newEbr.part_size = (int)sizeof(EBR) + partSize;
        newEbr.part_next = -1;
        strncpy(newEbr.part_name, name.c_str(), 15);
        newEbr.part_name[15] = '\0';

        // Actualizar puntero next del EBR anterior
        lastEbr.part_next = newEbrPos;
        fseek(file, lastEbrPos, SEEK_SET);
        fwrite(&lastEbr, sizeof(EBR), 1, file);

        // Escribir nuevo EBR
        fseek(file, newEbrPos, SEEK_SET);
        fwrite(&newEbr, sizeof(EBR), 1, file);

        fclose(file);
        return "Partición lógica '" + name + "' creada exitosamente.";
    }

    // === PRIMARIA O EXTENDIDA ===
    if (type == 'E') {
        // Verificar que no exista una extendida
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '1' &&
                mbr.mbr_partitions[i].part_type == 'E') {
                fclose(file);
                return "ERROR fdisk: Ya existe una partición extendida.";
            }
        }
    }

    // Buscar slot libre
    int freeSlot = -1;
    int activeCount = 0;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status == '1') {
            activeCount++;
        } else if (freeSlot == -1) {
            freeSlot = i;
        }
    }

    if (freeSlot == -1 || activeCount >= 4) {
        fclose(file);
        return "ERROR fdisk: No hay espacio para más particiones primarias/extendidas (máximo 4).";
    }

    // Buscar posicion de inicio
    auto gaps = findGaps(mbr);
    int startPos = findStartByFit(gaps, partSize, mbr.mbr_disk_fit);

    if (startPos == -1) {
        fclose(file);
        return "ERROR fdisk: No hay espacio suficiente en el disco.";
    }

    // Crear particion
    mbr.mbr_partitions[freeSlot].part_status = '1';
    mbr.mbr_partitions[freeSlot].part_type = type;
    mbr.mbr_partitions[freeSlot].part_fit = fit;
    mbr.mbr_partitions[freeSlot].part_start = startPos;
    mbr.mbr_partitions[freeSlot].part_size = partSize;
    strncpy(mbr.mbr_partitions[freeSlot].part_name, name.c_str(), 15);
    mbr.mbr_partitions[freeSlot].part_name[15] = '\0';

    // Escribir MBR
    fseek(file, 0, SEEK_SET);
    fwrite(&mbr, sizeof(MBR), 1, file);

    // Si es extendida, escribir EBR inicial
    if (type == 'E') {
        EBR ebr;
        ebr.part_start = startPos;
        fseek(file, startPos, SEEK_SET);
        fwrite(&ebr, sizeof(EBR), 1, file);
    }

    fclose(file);
    std::string typeStr = (type == 'P') ? "primaria" : "extendida";
    return "Partición " + typeStr + " '" + name + "' creada exitosamente.";
}

#endif // DISK_MANAGER_H
