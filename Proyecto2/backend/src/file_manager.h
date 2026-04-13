#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include "structures.h"
#include "mount_manager.h"
#include "user_manager.h"
#include "utils.h"
#include <string>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>
#include <fstream>
#include <algorithm>
#include <filesystem>

// ==================== Helpers ====================

// Asigna un inodo libre, devuelve su indice o -1
inline int allocateInode(FILE* file, Superblock& sb) {
    for (int i = sb.s_first_ino; i < sb.s_inodes_count; i++) {
        char bm;
        fseek(file, sb.s_bm_inode_start + i, SEEK_SET);
        fread(&bm, 1, 1, file);
        if (bm != '1') {
            char one = '1';
            fseek(file, sb.s_bm_inode_start + i, SEEK_SET);
            fwrite(&one, 1, 1, file);
            sb.s_free_inodes_count--;
            sb.s_first_ino = i + 1;
            return i;
        }
    }
    return -1;
}

// Asigna un bloque libre, devuelve su indice o -1
inline int allocateBlock(FILE* file, Superblock& sb) {
    for (int i = sb.s_first_blo; i < sb.s_blocks_count; i++) {
        char bm;
        fseek(file, sb.s_bm_block_start + i, SEEK_SET);
        fread(&bm, 1, 1, file);
        if (bm != '1') {
            char one = '1';
            fseek(file, sb.s_bm_block_start + i, SEEK_SET);
            fwrite(&one, 1, 1, file);
            sb.s_free_blocks_count--;
            sb.s_first_blo = i + 1;
            return i;
        }
    }
    return -1;
}

// Lee un inodo desde disco
inline Inode readInode(FILE* file, Superblock& sb, int inodeIdx) {
    Inode inode;
    fseek(file, sb.s_inode_start + inodeIdx * (int)sizeof(Inode), SEEK_SET);
    fread(&inode, sizeof(Inode), 1, file);
    return inode;
}

// Escribe un inodo en disco
inline void writeInode(FILE* file, Superblock& sb, int inodeIdx, const Inode& inode) {
    fseek(file, sb.s_inode_start + inodeIdx * (int)sizeof(Inode), SEEK_SET);
    fwrite(&inode, sizeof(Inode), 1, file);
}

// Lee un bloque de directorio desde disco
inline DirBlock readDirBlock(FILE* file, Superblock& sb, int blockIdx) {
    DirBlock db;
    fseek(file, sb.s_block_start + blockIdx * (int)sizeof(FileBlock), SEEK_SET);
    fread(&db, sizeof(DirBlock), 1, file);
    return db;
}

// Escribe un bloque de directorio en disco
inline void writeDirBlock(FILE* file, Superblock& sb, int blockIdx, const DirBlock& db) {
    fseek(file, sb.s_block_start + blockIdx * (int)sizeof(FileBlock), SEEK_SET);
    fwrite(&db, sizeof(DirBlock), 1, file);
}

// Lee un bloque de archivo desde disco
inline FileBlock readFileBlock(FILE* file, Superblock& sb, int blockIdx) {
    FileBlock fb;
    fseek(file, sb.s_block_start + blockIdx * (int)sizeof(FileBlock), SEEK_SET);
    fread(&fb, sizeof(FileBlock), 1, file);
    return fb;
}

// Lee un bloque de apuntadores desde disco
inline PointerBlock readPointerBlock(FILE* file, Superblock& sb, int blockIdx) {
    PointerBlock pb;
    fseek(file, sb.s_block_start + blockIdx * (int)sizeof(FileBlock), SEEK_SET);
    fread(&pb, sizeof(PointerBlock), 1, file);
    return pb;
}

// Escribe un bloque de apuntadores en disco
inline void writePointerBlock(FILE* file, Superblock& sb, int blockIdx, const PointerBlock& pb) {
    fseek(file, sb.s_block_start + blockIdx * (int)sizeof(FileBlock), SEEK_SET);
    fwrite(&pb, sizeof(PointerBlock), 1, file);
}

// Divide ruta en componentes: "/home/user/file.txt" -> ["home","user","file.txt"]
inline std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::istringstream stream(path);
    std::string token;
    while (std::getline(stream, token, '/')) {
        if (!token.empty()) parts.push_back(token);
    }
    return parts;
}

// Busca el indice de inodo por nombre en un inodo de directorio (primero directos, luego indirecto)
inline int findInDir(FILE* file, Superblock& sb, int dirInodeIdx, const std::string& name) {
    Inode dirInode = readInode(file, sb, dirInodeIdx);

    // Buscar en bloques directos
    for (int i = 0; i < 12; i++) {
        if (dirInode.i_block[i] == -1) continue;
        DirBlock db = readDirBlock(file, sb, dirInode.i_block[i]);
        for (int j = 0; j < 4; j++) {
            if (db.b_content[j].b_inodo != -1) {
                std::string entryName(db.b_content[j].b_name, strnlen(db.b_content[j].b_name, 12));
                if (entryName == name) return db.b_content[j].b_inodo;
            }
        }
    }

    // Buscar en indirecto simple
    if (dirInode.i_block[12] != -1) {
        PointerBlock pb = readPointerBlock(file, sb, dirInode.i_block[12]);
        for (int i = 0; i < 16; i++) {
            if (pb.b_pointers[i] == -1) continue;
            DirBlock db = readDirBlock(file, sb, pb.b_pointers[i]);
            for (int j = 0; j < 4; j++) {
                if (db.b_content[j].b_inodo != -1) {
                    std::string entryName(db.b_content[j].b_name, strnlen(db.b_content[j].b_name, 12));
                    if (entryName == name) return db.b_content[j].b_inodo;
                }
            }
        }
    }

    return -1; // no encontrado
}

// Add an entry (name, inodeIdx) to a directory inode
// Returns true on success
inline bool addEntryToDir(FILE* file, Superblock& sb, int dirInodeIdx, const std::string& name, int childInodeIdx) {
    Inode dirInode = readInode(file, sb, dirInodeIdx);

    // Intentar en bloques directos
    for (int i = 0; i < 12; i++) {
        if (dirInode.i_block[i] == -1) {
            // Asignar nuevo bloque
            int newBlock = allocateBlock(file, sb);
            if (newBlock == -1) return false;
            dirInode.i_block[i] = newBlock;
            DirBlock db;
            strncpy(db.b_content[0].b_name, name.c_str(), 12);
            db.b_content[0].b_inodo = childInodeIdx;
            writeDirBlock(file, sb, newBlock, db);
            writeInode(file, sb, dirInodeIdx, dirInode);
            return true;
        }
        // Bloque existente, buscar espacio libre
        DirBlock db = readDirBlock(file, sb, dirInode.i_block[i]);
        for (int j = 0; j < 4; j++) {
            if (db.b_content[j].b_inodo == -1) {
                strncpy(db.b_content[j].b_name, name.c_str(), 12);
                db.b_content[j].b_inodo = childInodeIdx;
                writeDirBlock(file, sb, dirInode.i_block[i], db);
                return true;
            }
        }
    }

    // Intentar bloque indirecto simple
    if (dirInode.i_block[12] == -1) {
        int ptrBlock = allocateBlock(file, sb);
        if (ptrBlock == -1) return false;
        dirInode.i_block[12] = ptrBlock;
        PointerBlock pb;
        int dataBlock = allocateBlock(file, sb);
        if (dataBlock == -1) return false;
        pb.b_pointers[0] = dataBlock;
        writePointerBlock(file, sb, ptrBlock, pb);

        DirBlock db;
        strncpy(db.b_content[0].b_name, name.c_str(), 12);
        db.b_content[0].b_inodo = childInodeIdx;
        writeDirBlock(file, sb, dataBlock, db);
        writeInode(file, sb, dirInodeIdx, dirInode);
        return true;
    }

    PointerBlock pb = readPointerBlock(file, sb, dirInode.i_block[12]);
    for (int i = 0; i < 16; i++) {
        if (pb.b_pointers[i] == -1) {
            int dataBlock = allocateBlock(file, sb);
            if (dataBlock == -1) return false;
            pb.b_pointers[i] = dataBlock;
            writePointerBlock(file, sb, dirInode.i_block[12], pb);

            DirBlock db;
            strncpy(db.b_content[0].b_name, name.c_str(), 12);
            db.b_content[0].b_inodo = childInodeIdx;
            writeDirBlock(file, sb, dataBlock, db);
            return true;
        }
        DirBlock db = readDirBlock(file, sb, pb.b_pointers[i]);
        for (int j = 0; j < 4; j++) {
            if (db.b_content[j].b_inodo == -1) {
                strncpy(db.b_content[j].b_name, name.c_str(), 12);
                db.b_content[j].b_inodo = childInodeIdx;
                writeDirBlock(file, sb, pb.b_pointers[i], db);
                return true;
            }
        }
    }

    return false; // sin espacio
}

// Navega al directorio padre, creando directorios si r=true
// Devuelve indice del inodo padre, -1 si falla
inline int navigateToParent(FILE* file, Superblock& sb, const std::vector<std::string>& parts,
                             bool createParents, int uid, int gid) {
    int currentInode = 0; // iniciar en raiz

    // Navegar todas las partes excepto la ultima (archivo/dir a crear)
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        int nextInode = findInDir(file, sb, currentInode, parts[i]);
        if (nextInode == -1) {
            if (!createParents) return -1;
            // Crear directorio intermedio
            int newInodeIdx = allocateInode(file, sb);
            if (newInodeIdx == -1) return -1;

            Inode newDir;
            newDir.i_uid = uid;
            newDir.i_gid = gid;
            newDir.i_size = 0;
            newDir.i_atime = getCurrentTime();
            newDir.i_ctime = getCurrentTime();
            newDir.i_mtime = getCurrentTime();
            newDir.i_type = '0';
            newDir.i_perm[0] = '6'; newDir.i_perm[1] = '6'; newDir.i_perm[2] = '4';
            int newBlockIdx = allocateBlock(file, sb);
            if (newBlockIdx == -1) return -1;
            newDir.i_block[0] = newBlockIdx;

            DirBlock db;
            strncpy(db.b_content[0].b_name, ".", 12);
            db.b_content[0].b_inodo = newInodeIdx;
            strncpy(db.b_content[1].b_name, "..", 12);
            db.b_content[1].b_inodo = currentInode;
            writeDirBlock(file, sb, newBlockIdx, db);
            writeInode(file, sb, newInodeIdx, newDir);

            // Agregar entrada al padre
            if (!addEntryToDir(file, sb, currentInode, parts[i], newInodeIdx))
                return -1;

            currentInode = newInodeIdx;
        } else {
            currentInode = nextInode;
        }
    }

    return currentInode;
}

// Escribe contenido en un inodo de archivo, asignando bloques segun sea necesario
inline bool writeFileContent(FILE* file, Superblock& sb, int inodeIdx, const std::string& content) {
    Inode fileInode = readInode(file, sb, inodeIdx);
    fileInode.i_size = (int)content.size();
    fileInode.i_mtime = getCurrentTime();

    int totalBlocks = ((int)content.size() + 63) / 64;
    if (totalBlocks == 0) totalBlocks = 1;

    int written = 0;

    // Bloques directos (0-11)
    for (int i = 0; i < 12 && written < (int)content.size(); i++) {
        int blockIdx = fileInode.i_block[i];
        if (blockIdx == -1) {
            blockIdx = allocateBlock(file, sb);
            if (blockIdx == -1) return false;
            fileInode.i_block[i] = blockIdx;
        }
        FileBlock fb;
        memset(fb.b_content, 0, 64);
        int toCopy = std::min(64, (int)content.size() - written);
        memcpy(fb.b_content, content.c_str() + written, toCopy);
        written += toCopy;
        fseek(file, sb.s_block_start + blockIdx * (int)sizeof(FileBlock), SEEK_SET);
        fwrite(&fb, sizeof(FileBlock), 1, file);
    }

    // Indirecto simple (i_block[12])
    if (written < (int)content.size()) {
        if (fileInode.i_block[12] == -1) {
            int ptrBlock = allocateBlock(file, sb);
            if (ptrBlock == -1) return false;
            fileInode.i_block[12] = ptrBlock;
            PointerBlock pb;
            writePointerBlock(file, sb, ptrBlock, pb);
        }
        PointerBlock pb = readPointerBlock(file, sb, fileInode.i_block[12]);
        for (int i = 0; i < 16 && written < (int)content.size(); i++) {
            int blockIdx = pb.b_pointers[i];
            if (blockIdx == -1) {
                blockIdx = allocateBlock(file, sb);
                if (blockIdx == -1) return false;
                pb.b_pointers[i] = blockIdx;
            }
            FileBlock fb;
            memset(fb.b_content, 0, 64);
            int toCopy = std::min(64, (int)content.size() - written);
            memcpy(fb.b_content, content.c_str() + written, toCopy);
            written += toCopy;
            fseek(file, sb.s_block_start + blockIdx * (int)sizeof(FileBlock), SEEK_SET);
            fwrite(&fb, sizeof(FileBlock), 1, file);
        }
        writePointerBlock(file, sb, fileInode.i_block[12], pb);
    }

    // Doble indirecto (i_block[13])
    if (written < (int)content.size()) {
        if (fileInode.i_block[13] == -1) {
            int ptrBlock = allocateBlock(file, sb);
            if (ptrBlock == -1) return false;
            fileInode.i_block[13] = ptrBlock;
            PointerBlock pb;
            writePointerBlock(file, sb, ptrBlock, pb);
        }
        PointerBlock pb1 = readPointerBlock(file, sb, fileInode.i_block[13]);
        for (int i = 0; i < 16 && written < (int)content.size(); i++) {
            if (pb1.b_pointers[i] == -1) {
                int ptrBlock2 = allocateBlock(file, sb);
                if (ptrBlock2 == -1) return false;
                pb1.b_pointers[i] = ptrBlock2;
                PointerBlock pb2;
                writePointerBlock(file, sb, ptrBlock2, pb2);
            }
            PointerBlock pb2 = readPointerBlock(file, sb, pb1.b_pointers[i]);
            for (int j = 0; j < 16 && written < (int)content.size(); j++) {
                int blockIdx = pb2.b_pointers[j];
                if (blockIdx == -1) {
                    blockIdx = allocateBlock(file, sb);
                    if (blockIdx == -1) return false;
                    pb2.b_pointers[j] = blockIdx;
                }
                FileBlock fb;
                memset(fb.b_content, 0, 64);
                int toCopy = std::min(64, (int)content.size() - written);
                memcpy(fb.b_content, content.c_str() + written, toCopy);
                written += toCopy;
                fseek(file, sb.s_block_start + blockIdx * (int)sizeof(FileBlock), SEEK_SET);
                fwrite(&fb, sizeof(FileBlock), 1, file);
            }
            writePointerBlock(file, sb, pb1.b_pointers[i], pb2);
        }
        writePointerBlock(file, sb, fileInode.i_block[13], pb1);
    }

    // Triple indirecto (i_block[14])
    if (written < (int)content.size()) {
        if (fileInode.i_block[14] == -1) {
            int ptrBlock = allocateBlock(file, sb);
            if (ptrBlock == -1) return false;
            fileInode.i_block[14] = ptrBlock;
            PointerBlock pb;
            writePointerBlock(file, sb, ptrBlock, pb);
        }
        PointerBlock pb1 = readPointerBlock(file, sb, fileInode.i_block[14]);
        for (int i = 0; i < 16 && written < (int)content.size(); i++) {
            if (pb1.b_pointers[i] == -1) {
                int p2 = allocateBlock(file, sb);
                if (p2 == -1) return false;
                pb1.b_pointers[i] = p2;
                PointerBlock pb;
                writePointerBlock(file, sb, p2, pb);
            }
            PointerBlock pb2 = readPointerBlock(file, sb, pb1.b_pointers[i]);
            for (int j = 0; j < 16 && written < (int)content.size(); j++) {
                if (pb2.b_pointers[j] == -1) {
                    int p3 = allocateBlock(file, sb);
                    if (p3 == -1) return false;
                    pb2.b_pointers[j] = p3;
                    PointerBlock pb;
                    writePointerBlock(file, sb, p3, pb);
                }
                PointerBlock pb3 = readPointerBlock(file, sb, pb2.b_pointers[j]);
                for (int k = 0; k < 16 && written < (int)content.size(); k++) {
                    int blockIdx = pb3.b_pointers[k];
                    if (blockIdx == -1) {
                        blockIdx = allocateBlock(file, sb);
                        if (blockIdx == -1) return false;
                        pb3.b_pointers[k] = blockIdx;
                    }
                    FileBlock fb;
                    memset(fb.b_content, 0, 64);
                    int toCopy = std::min(64, (int)content.size() - written);
                    memcpy(fb.b_content, content.c_str() + written, toCopy);
                    written += toCopy;
                    fseek(file, sb.s_block_start + blockIdx * (int)sizeof(FileBlock), SEEK_SET);
                    fwrite(&fb, sizeof(FileBlock), 1, file);
                }
                writePointerBlock(file, sb, pb2.b_pointers[j], pb3);
            }
            writePointerBlock(file, sb, pb1.b_pointers[i], pb2);
        }
        writePointerBlock(file, sb, fileInode.i_block[14], pb1);
    }

    writeInode(file, sb, inodeIdx, fileInode);
    return true;
}

// Lee todo el contenido desde un inodo de archivo
inline std::string readFileContent(FILE* file, Superblock& sb, int inodeIdx) {
    Inode fileInode = readInode(file, sb, inodeIdx);
    std::string content;
    int remaining = fileInode.i_size;

    // Bloques directos
    for (int i = 0; i < 12 && remaining > 0; i++) {
        if (fileInode.i_block[i] == -1) break;
        FileBlock fb = readFileBlock(file, sb, fileInode.i_block[i]);
        int toCopy = std::min(64, remaining);
        content.append(fb.b_content, toCopy);
        remaining -= toCopy;
    }

    // Indirecto simple
    if (remaining > 0 && fileInode.i_block[12] != -1) {
        PointerBlock pb = readPointerBlock(file, sb, fileInode.i_block[12]);
        for (int i = 0; i < 16 && remaining > 0; i++) {
            if (pb.b_pointers[i] == -1) break;
            FileBlock fb = readFileBlock(file, sb, pb.b_pointers[i]);
            int toCopy = std::min(64, remaining);
            content.append(fb.b_content, toCopy);
            remaining -= toCopy;
        }
    }

    // Doble indirecto
    if (remaining > 0 && fileInode.i_block[13] != -1) {
        PointerBlock pb1 = readPointerBlock(file, sb, fileInode.i_block[13]);
        for (int i = 0; i < 16 && remaining > 0; i++) {
            if (pb1.b_pointers[i] == -1) break;
            PointerBlock pb2 = readPointerBlock(file, sb, pb1.b_pointers[i]);
            for (int j = 0; j < 16 && remaining > 0; j++) {
                if (pb2.b_pointers[j] == -1) break;
                FileBlock fb = readFileBlock(file, sb, pb2.b_pointers[j]);
                int toCopy = std::min(64, remaining);
                content.append(fb.b_content, toCopy);
                remaining -= toCopy;
            }
        }
    }

    // Triple indirecto
    if (remaining > 0 && fileInode.i_block[14] != -1) {
        PointerBlock pb1 = readPointerBlock(file, sb, fileInode.i_block[14]);
        for (int i = 0; i < 16 && remaining > 0; i++) {
            if (pb1.b_pointers[i] == -1) break;
            PointerBlock pb2 = readPointerBlock(file, sb, pb1.b_pointers[i]);
            for (int j = 0; j < 16 && remaining > 0; j++) {
                if (pb2.b_pointers[j] == -1) break;
                PointerBlock pb3 = readPointerBlock(file, sb, pb2.b_pointers[j]);
                for (int k = 0; k < 16 && remaining > 0; k++) {
                    if (pb3.b_pointers[k] == -1) break;
                    FileBlock fb = readFileBlock(file, sb, pb3.b_pointers[k]);
                    int toCopy = std::min(64, remaining);
                    content.append(fb.b_content, toCopy);
                    remaining -= toCopy;
                }
            }
        }
    }

    return content;
}

// ==================== MKDIR ====================
inline std::string cmd_mkdir(const std::map<std::string, std::string>& params) {
    if (params.find("path") == params.end())
        return "ERROR mkdir: Parámetro -path es obligatorio.";

    std::string path = params.at("path");
    bool recursive = false;
    if (params.find("r") != params.end() || params.find("p") != params.end()) {
        recursive = true;
    }

    // Crear la carpeta física
    try {
        std::filesystem::path physicalPath = "archivos" + path;
        if (recursive) {
            std::filesystem::create_directories(physicalPath);
        } else {
            std::filesystem::create_directory(physicalPath);
        }
    } catch (const std::filesystem::filesystem_error& e) {
        // Ignorar errores si la carpeta ya existe, pero reportar otros
        if (e.code() != std::errc::file_exists) {
            return "ERROR mkdir (physical): " + std::string(e.what());
        }
    }

    MountedPart* mp = getLoggedMount();
    if (!mp) return "ERROR mkdir: No hay sesión activa. Use 'login' primero.";

    FILE* file = fopen(mp->path, "rb+");
    if (!file) return "ERROR mkdir: No se pudo abrir el disco.";

    int sbStart = mp->part_start;
    Superblock sb;
    fseek(file, sbStart, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::vector<std::string> parts = splitPath(path);
    if (parts.empty()) {
        fclose(file);
        return "ERROR mkdir: Ruta inválida.";
    }

    // Navegar/crear hacia el padre
    int parentInode = 0; // root
    for (size_t i = 0; i < parts.size(); i++) {
        int nextInode = findInDir(file, sb, parentInode, parts[i]);
        if (nextInode != -1) {
            parentInode = nextInode;
            continue;
        }
        // No encontrado: crear si es recursivo o si es el ultimo componente
        if (!recursive && i + 1 < parts.size()) {
            fclose(file);
            return "ERROR mkdir: No existe la ruta padre. Use -r para crear recursivamente.";
        }
        // Crear directorio
        int newInodeIdx = allocateInode(file, sb);
        if (newInodeIdx == -1) {
            fclose(file);
            return "ERROR mkdir: No hay inodos disponibles.";
        }

        Inode newDir;
        newDir.i_uid = mp->current_uid;
        newDir.i_gid = mp->current_gid;
        newDir.i_size = 0;
        newDir.i_atime = getCurrentTime();
        newDir.i_ctime = getCurrentTime();
        newDir.i_mtime = getCurrentTime();
        newDir.i_type = '0';
        newDir.i_perm[0] = '6'; newDir.i_perm[1] = '6'; newDir.i_perm[2] = '4';

        int newBlockIdx = allocateBlock(file, sb);
        if (newBlockIdx == -1) {
            fclose(file);
            return "ERROR mkdir: No hay bloques disponibles.";
        }
        newDir.i_block[0] = newBlockIdx;

        DirBlock db;
        strncpy(db.b_content[0].b_name, ".", 12);
        db.b_content[0].b_inodo = newInodeIdx;
        strncpy(db.b_content[1].b_name, "..", 12);
        db.b_content[1].b_inodo = parentInode;
        writeDirBlock(file, sb, newBlockIdx, db);
        writeInode(file, sb, newInodeIdx, newDir);

        if (!addEntryToDir(file, sb, parentInode, parts[i], newInodeIdx)) {
            fclose(file);
            return "ERROR mkdir: No se pudo agregar entrada al directorio padre.";
        }

        parentInode = newInodeIdx;
    }

    // Guardar superbloque
    fseek(file, sbStart, SEEK_SET);
    fwrite(&sb, sizeof(Superblock), 1, file);
    fclose(file);

    return "Directorio '" + path + "' creado exitosamente.";
}

// ==================== MKFILE ====================
inline std::string cmd_mkfile(const std::map<std::string, std::string>& params) {
    if (params.find("path") == params.end())
        return "ERROR mkfile: Parámetro -path es obligatorio.";

    std::string path = params.at("path");
    bool recursive = false;
    if (params.find("r") != params.end()) recursive = true;

    int size = 0;
    if (params.find("size") != params.end()) {
        size = std::stoi(params.at("size"));
        if (size < 0) return "ERROR mkfile: El tamaño no puede ser negativo.";
    }

    std::string cont;
    if (params.find("cont") != params.end()) {
        // Leer contenido desde un archivo real del sistema host
        std::string contPath = params.at("cont");
        std::ifstream ifs(contPath);
        if (!ifs.is_open())
            return "ERROR mkfile: No se pudo abrir el archivo de contenido '" + contPath + "'.";
        std::stringstream ss;
        ss << ifs.rdbuf();
        cont = ss.str();
        ifs.close();
    } else if (size > 0) {
        // Generar contenido: 0123456789 repetido
        std::string digits = "0123456789";
        cont.reserve(size);
        for (int i = 0; i < size; i++) {
            cont += digits[i % 10];
        }
    }

    // Crear el archivo físico
    try {
        std::filesystem::path physicalPath = "archivos" + path;
        if (recursive) {
            if (physicalPath.has_parent_path()) {
                std::filesystem::create_directories(physicalPath.parent_path());
            }
        }
        std::ofstream ofs(physicalPath);
        if (!ofs.is_open()) {
             return "ERROR mkfile (physical): No se pudo crear el archivo físico '" + physicalPath.string() + "'.";
        }
        ofs << cont;
        ofs.close();
    } catch (const std::filesystem::filesystem_error& e) {
        return "ERROR mkfile (physical): " + std::string(e.what());
    }

    MountedPart* mp = getLoggedMount();
    if (!mp) return "ERROR mkfile: No hay sesión activa. Use 'login' primero.";

    FILE* file = fopen(mp->path, "rb+");
    if (!file) return "ERROR mkfile: No se pudo abrir el disco.";

    int sbStart = mp->part_start;
    Superblock sb;
    fseek(file, sbStart, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::vector<std::string> parts = splitPath(path);
    if (parts.empty()) {
        fclose(file);
        return "ERROR mkfile: Ruta inválida.";
    }

    // Navegar al directorio padre
    int parentInode = navigateToParent(file, sb, parts, recursive, mp->current_uid, mp->current_gid);
    if (parentInode == -1) {
        fclose(file);
        return "ERROR mkfile: No existe la carpeta padre. Use -r para crear recursivamente.";
    }

    std::string fileName = parts.back();

    // Verificar si el archivo ya existe
    int existing = findInDir(file, sb, parentInode, fileName);
    if (existing != -1) {
        // El archivo existe, sobrescribir contenido
        writeFileContent(file, sb, existing, cont);
        Inode fi = readInode(file, sb, existing);
        fi.i_mtime = getCurrentTime();
        writeInode(file, sb, existing, fi);
        fseek(file, sbStart, SEEK_SET);
        fwrite(&sb, sizeof(Superblock), 1, file);
        fclose(file);
        return "Archivo '" + path + "' actualizado exitosamente.";
    }

    // Crear nuevo inodo de archivo
    int newInodeIdx = allocateInode(file, sb);
    if (newInodeIdx == -1) {
        fclose(file);
        return "ERROR mkfile: No hay inodos disponibles.";
    }

    Inode newFile;
    newFile.i_uid = mp->current_uid;
    newFile.i_gid = mp->current_gid;
    newFile.i_size = (int)cont.size();
    newFile.i_atime = getCurrentTime();
    newFile.i_ctime = getCurrentTime();
    newFile.i_mtime = getCurrentTime();
    newFile.i_type = '1';
    newFile.i_perm[0] = '6'; newFile.i_perm[1] = '6'; newFile.i_perm[2] = '4';
    writeInode(file, sb, newInodeIdx, newFile);

    // Escribir contenido
    if (!cont.empty()) {
        writeFileContent(file, sb, newInodeIdx, cont);
    }

    // Agregar entrada al directorio padre
    if (!addEntryToDir(file, sb, parentInode, fileName, newInodeIdx)) {
        fclose(file);
        return "ERROR mkfile: No se pudo agregar entrada al directorio padre.";
    }

    // Guardar superbloque
    fseek(file, sbStart, SEEK_SET);
    fwrite(&sb, sizeof(Superblock), 1, file);
    fclose(file);

    return "Archivo '" + path + "' creado exitosamente (" + std::to_string(cont.size()) + " bytes).";
}

// ==================== CAT ====================
inline std::string cmd_cat(const std::map<std::string, std::string>& params) {
    // cat puede recibir multiples archivos: -file1, -file2, etc.
    MountedPart* mp = getLoggedMount();
    if (!mp) return "ERROR cat: No hay sesión activa.";

    FILE* file = fopen(mp->path, "rb+");
    if (!file) return "ERROR cat: No se pudo abrir el disco.";

    int sbStart = mp->part_start;
    Superblock sb;
    fseek(file, sbStart, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::string result;

    // Intentar file1, file2, ..., file10 y tambien solo "file"
    for (int n = 1; n <= 20; n++) {
        std::string key = "file" + std::to_string(n);
        if (params.find(key) == params.end()) {
            if (n == 1 && params.find("file") != params.end()) {
                key = "file";
            } else {
                continue;
            }
        }

        std::string filePath = params.at(key);
        std::vector<std::string> parts = splitPath(filePath);

        // Navegar para encontrar el archivo
        int currentInode = 0;
        bool found = true;
        for (size_t i = 0; i < parts.size(); i++) {
            int nextInode = findInDir(file, sb, currentInode, parts[i]);
            if (nextInode == -1) {
                result += "ERROR: No se encontró '" + filePath + "'\n";
                found = false;
                break;
            }
            currentInode = nextInode;
        }

        if (found) {
            std::string content = readFileContent(file, sb, currentInode);
            if (!result.empty()) result += "\n";
            result += "--- " + filePath + " ---\n";
            result += content;
        }
    }

    fclose(file);

    if (result.empty()) return "ERROR cat: No se especificaron archivos.";
    return result;
}

#endif // FILE_MANAGER_H
