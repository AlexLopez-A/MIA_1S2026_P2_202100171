#ifndef USER_MANAGER_H
#define USER_MANAGER_H

#include "structures.h"
#include "mount_manager.h"
#include "utils.h"
#include <string>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

// ==================== Auxiliar: Leer users.txt ====================

inline std::string readUsersFile(FILE* file, Superblock& sb) {
    // Leer inodo 1 (users.txt)
    Inode usersInode;
    fseek(file, sb.s_inode_start + (int)sizeof(Inode), SEEK_SET);
    fread(&usersInode, sizeof(Inode), 1, file);

    std::string content;
    // Leer todos los bloques del archivo
    for (int i = 0; i < 12; i++) {
        if (usersInode.i_block[i] == -1) break;
        FileBlock fb;
        fseek(file, sb.s_block_start + usersInode.i_block[i] * (int)sizeof(FileBlock), SEEK_SET);
        fread(&fb, sizeof(FileBlock), 1, file);
        for (int j = 0; j < 64; j++) {
            if (fb.b_content[j] == '\0') break;
            content += fb.b_content[j];
        }
    }

    // Manejar bloque indirecto simple (i_block[12])
    if (usersInode.i_block[12] != -1) {
        PointerBlock pb;
        fseek(file, sb.s_block_start + usersInode.i_block[12] * (int)sizeof(FileBlock), SEEK_SET);
        fread(&pb, sizeof(PointerBlock), 1, file);
        for (int i = 0; i < 16; i++) {
            if (pb.b_pointers[i] == -1) break;
            FileBlock fb;
            fseek(file, sb.s_block_start + pb.b_pointers[i] * (int)sizeof(FileBlock), SEEK_SET);
            fread(&fb, sizeof(FileBlock), 1, file);
            for (int j = 0; j < 64; j++) {
                if (fb.b_content[j] == '\0') break;
                content += fb.b_content[j];
            }
        }
    }

    return content;
}

inline void writeUsersFile(FILE* file, Superblock& sb, const std::string& content, int sbStart) {
    // Leer inodo de users
    Inode usersInode;
    fseek(file, sb.s_inode_start + (int)sizeof(Inode), SEEK_SET);
    fread(&usersInode, sizeof(Inode), 1, file);

    usersInode.i_size = (int)content.size();
    usersInode.i_mtime = getCurrentTime();

    // Calcular bloques necesarios
    int blocksNeeded = ((int)content.size() + 63) / 64;
    if (blocksNeeded == 0) blocksNeeded = 1;

    int written = 0;
    for (int i = 0; i < blocksNeeded && i < 12; i++) {
        int blockIdx = usersInode.i_block[i];
        if (blockIdx == -1) {
            // Asignar nuevo bloque
            blockIdx = sb.s_first_blo;
            usersInode.i_block[i] = blockIdx;

            // Marcar bitmap
            char one = '1';
            fseek(file, sb.s_bm_block_start + blockIdx, SEEK_SET);
            fwrite(&one, 1, 1, file);

            // Buscar siguiente bloque libre
            sb.s_free_blocks_count--;
            sb.s_first_blo++;
            while (sb.s_first_blo < sb.s_blocks_count) {
                char bm;
                fseek(file, sb.s_bm_block_start + sb.s_first_blo, SEEK_SET);
                fread(&bm, 1, 1, file);
                if (bm != '1') break;
                sb.s_first_blo++;
            }
        }

        FileBlock fb;
        memset(fb.b_content, 0, 64);
        int toCopy = std::min(64, (int)content.size() - written);
        memcpy(fb.b_content, content.c_str() + written, toCopy);
        written += toCopy;

        fseek(file, sb.s_block_start + blockIdx * (int)sizeof(FileBlock), SEEK_SET);
        fwrite(&fb, sizeof(FileBlock), 1, file);
    }

    // Escribir inodo actualizado
    fseek(file, sb.s_inode_start + (int)sizeof(Inode), SEEK_SET);
    fwrite(&usersInode, sizeof(Inode), 1, file);

    // Escribir superbloque actualizado
    fseek(file, sbStart, SEEK_SET);
    fwrite(&sb, sizeof(Superblock), 1, file);
}

// ==================== LOGIN ====================
inline std::string cmd_login(const std::map<std::string, std::string>& params) {
    if (params.find("user") == params.end())
        return "ERROR login: Parámetro -user es obligatorio.";
    if (params.find("pass") == params.end())
        return "ERROR login: Parámetro -pass es obligatorio.";
    if (params.find("id") == params.end())
        return "ERROR login: Parámetro -id es obligatorio.";

    std::string user = params.at("user");
    std::string pass = params.at("pass");
    std::string id = params.at("id");

    MountedPart* mp = getMountedById(id);
    if (!mp)
        return "ERROR login: No se encontró la partición montada con ID '" + id + "'.";

    // Verificar si existe ALGUNA sesion activa en CUALQUIER montaje (solo una sesion global)
    {
        auto& allMounts = getMountedList();
        for (auto& m : allMounts) {
            if (m.logged_in)
                return "ERROR login: Ya hay una sesión activa. Use 'logout' primero.";
        }
    }

    // Leer superbloque
    FILE* file = fopen(mp->path, "rb+");
    if (!file) return "ERROR login: No se pudo abrir el disco.";

    Superblock sb;
    fseek(file, mp->part_start, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    // Leer users.txt
    std::string usersContent = readUsersFile(file, sb);
    fclose(file);

    // Analizar users.txt para encontrar usuario
    std::istringstream stream(usersContent);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        // Formato: ID,TIPO,GrupoONombre,...
        std::istringstream ls(line);
        std::string sId, sType, sGroup, sUser, sPass;
        std::getline(ls, sId, ',');
        std::getline(ls, sType, ',');

        if (sId == "0") continue; // eliminado

        if (sType == "U") {
            std::getline(ls, sGroup, ',');
            std::getline(ls, sUser, ',');
            std::getline(ls, sPass, ',');
            // Limpiar espacios
            sUser = trim(sUser);
            sPass = trim(sPass);

            if (sUser == user && sPass == pass) {
                mp->logged_in = true;
                strncpy(mp->current_user, user.c_str(), 19);
                mp->current_uid = std::stoi(trim(sId));

                // Buscar ID de grupo
                std::istringstream stream2(usersContent);
                std::string line2;
                while (std::getline(stream2, line2)) {
                    if (line2.empty()) continue;
                    std::istringstream ls2(line2);
                    std::string gId, gType, gName;
                    std::getline(ls2, gId, ',');
                    std::getline(ls2, gType, ',');
                    std::getline(ls2, gName, ',');
                    gName = trim(gName);
                    if (gType == "G" && gId != "0" && gName == trim(sGroup)) {
                        mp->current_gid = std::stoi(trim(gId));
                        break;
                    }
                }

                return "Sesión iniciada como '" + user + "'.";
            }
        }
    }

    return "ERROR login: Usuario o contraseña incorrectos.";
}

// ==================== LOGOUT ====================
inline std::string cmd_logout() {
    auto& mounted = getMountedList();
    for (auto& m : mounted) {
        if (m.logged_in) {
            std::string user = std::string(m.current_user);
            m.logged_in = false;
            memset(m.current_user, 0, 20);
            m.current_uid = 0;
            m.current_gid = 0;
            return "Sesión cerrada. Usuario '" + user + "' desconectado.";
        }
    }
    return "ERROR logout: No hay sesión activa.";
}

// Obtener el montaje con sesion activa
inline MountedPart* getLoggedMount() {
    auto& mounted = getMountedList();
    for (auto& m : mounted) {
        if (m.logged_in) return &m;
    }
    return nullptr;
}

// ==================== MKGRP ====================
inline std::string cmd_mkgrp(const std::map<std::string, std::string>& params) {
    if (params.find("name") == params.end())
        return "ERROR mkgrp: Parámetro -name es obligatorio.";

    std::string name = params.at("name");

    MountedPart* mp = getLoggedMount();
    if (!mp) return "ERROR mkgrp: No hay sesión activa.";
    if (std::string(mp->current_user) != "root")
        return "ERROR mkgrp: Solo el usuario root puede crear grupos.";

    FILE* file = fopen(mp->path, "rb+");
    if (!file) return "ERROR mkgrp: No se pudo abrir el disco.";

    int sbStart = mp->part_start;
    Superblock sb;
    fseek(file, sbStart, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::string usersContent = readUsersFile(file, sb);

    // Verificar si el grupo ya existe
    int maxGroupId = 0;
    std::istringstream stream(usersContent);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string sId, sType, sName;
        std::getline(ls, sId, ',');
        std::getline(ls, sType, ',');
        std::getline(ls, sName, ',');
        sName = trim(sName);

        int id = std::stoi(trim(sId));
        if (sType == "G") {
            if (id > maxGroupId) maxGroupId = id;
            if (id != 0 && sName == name) {
                fclose(file);
                return "ERROR mkgrp: El grupo '" + name + "' ya existe.";
            }
        }
    }

    // Agregar grupo
    int newId = maxGroupId + 1;
    usersContent += std::to_string(newId) + ",G," + name + "\n";

    writeUsersFile(file, sb, usersContent, sbStart);
    fclose(file);

    return "Grupo '" + name + "' creado exitosamente con ID " + std::to_string(newId) + ".";
}

// ==================== RMGRP ====================
inline std::string cmd_rmgrp(const std::map<std::string, std::string>& params) {
    if (params.find("name") == params.end())
        return "ERROR rmgrp: Parámetro -name es obligatorio.";

    std::string name = params.at("name");

    MountedPart* mp = getLoggedMount();
    if (!mp) return "ERROR rmgrp: No hay sesión activa.";
    if (std::string(mp->current_user) != "root")
        return "ERROR rmgrp: Solo el usuario root puede eliminar grupos.";

    FILE* file = fopen(mp->path, "rb+");
    if (!file) return "ERROR rmgrp: No se pudo abrir el disco.";

    int sbStart = mp->part_start;
    Superblock sb;
    fseek(file, sbStart, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::string usersContent = readUsersFile(file, sb);

    // Buscar y marcar grupo como eliminado (poner ID en 0)
    bool found = false;
    std::string newContent;
    std::istringstream stream(usersContent);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string sId, sType, sName;
        std::getline(ls, sId, ',');
        std::getline(ls, sType, ',');
        std::getline(ls, sName, ',');
        sName = trim(sName);

        if (sType == "G" && sId != "0" && sName == name) {
            newContent += "0,G," + name + "\n";
            found = true;
        } else {
            newContent += line + "\n";
        }
    }

    if (!found) {
        fclose(file);
        return "ERROR rmgrp: No se encontró el grupo '" + name + "'.";
    }

    writeUsersFile(file, sb, newContent, sbStart);
    fclose(file);

    return "Grupo '" + name + "' eliminado exitosamente.";
}

// ==================== MKUSR ====================
inline std::string cmd_mkusr(const std::map<std::string, std::string>& params) {
    if (params.find("user") == params.end())
        return "ERROR mkusr: Parámetro -user es obligatorio.";
    if (params.find("pass") == params.end())
        return "ERROR mkusr: Parámetro -pass es obligatorio.";
    if (params.find("grp") == params.end())
        return "ERROR mkusr: Parámetro -grp es obligatorio.";

    std::string user = params.at("user");
    std::string pass = params.at("pass");
    std::string grp = params.at("grp");

    if (user.size() > 10)
        return "ERROR mkusr: El nombre de usuario no puede exceder 10 caracteres.";
    if (pass.size() > 10)
        return "ERROR mkusr: La contraseña no puede exceder 10 caracteres.";

    MountedPart* mp = getLoggedMount();
    if (!mp) return "ERROR mkusr: No hay sesión activa.";
    if (std::string(mp->current_user) != "root")
        return "ERROR mkusr: Solo el usuario root puede crear usuarios.";

    FILE* file = fopen(mp->path, "rb+");
    if (!file) return "ERROR mkusr: No se pudo abrir el disco.";

    int sbStart = mp->part_start;
    Superblock sb;
    fseek(file, sbStart, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::string usersContent = readUsersFile(file, sb);

    // Verificar que el grupo exista
    bool groupExists = false;
    std::istringstream stream(usersContent);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string sId, sType, sName;
        std::getline(ls, sId, ',');
        std::getline(ls, sType, ',');
        std::getline(ls, sName, ',');
        sName = trim(sName);
        if (sType == "G" && sId != "0" && sName == grp) {
            groupExists = true;
            break;
        }
    }

    if (!groupExists) {
        fclose(file);
        return "ERROR mkusr: El grupo '" + grp + "' no existe.";
    }

    // Verificar que el usuario no exista y obtener el ID maximo
    int maxUserId = 0;
    std::istringstream stream2(usersContent);
    while (std::getline(stream2, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string sId, sType, sGrp, sUser;
        std::getline(ls, sId, ',');
        std::getline(ls, sType, ',');
        if (sType == "U") {
            std::getline(ls, sGrp, ',');
            std::getline(ls, sUser, ',');
            sUser = trim(sUser);
            int id = std::stoi(trim(sId));
            if (id > maxUserId) maxUserId = id;
            if (id != 0 && sUser == user) {
                fclose(file);
                return "ERROR mkusr: El usuario '" + user + "' ya existe.";
            }
        }
    }

    // Agregar usuario
    int newId = maxUserId + 1;
    usersContent += std::to_string(newId) + ",U," + grp + "," + user + "," + pass + "\n";

    writeUsersFile(file, sb, usersContent, sbStart);
    fclose(file);

    return "Usuario '" + user + "' creado exitosamente con ID " + std::to_string(newId) + ".";
}

// ==================== RMUSR ====================
inline std::string cmd_rmusr(const std::map<std::string, std::string>& params) {
    if (params.find("user") == params.end())
        return "ERROR rmusr: Parámetro -user es obligatorio.";

    std::string user = params.at("user");

    MountedPart* mp = getLoggedMount();
    if (!mp) return "ERROR rmusr: No hay sesión activa.";
    if (std::string(mp->current_user) != "root")
        return "ERROR rmusr: Solo el usuario root puede eliminar usuarios.";

    FILE* file = fopen(mp->path, "rb+");
    if (!file) return "ERROR rmusr: No se pudo abrir el disco.";

    int sbStart = mp->part_start;
    Superblock sb;
    fseek(file, sbStart, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::string usersContent = readUsersFile(file, sb);

    bool found = false;
    std::string newContent;
    std::istringstream stream(usersContent);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string sId, sType, sGrp, sUser, sPass;
        std::getline(ls, sId, ',');
        std::getline(ls, sType, ',');
        if (sType == "U") {
            std::getline(ls, sGrp, ',');
            std::getline(ls, sUser, ',');
            std::getline(ls, sPass, ',');
            sUser = trim(sUser);
            if (sId != "0" && sUser == user) {
                newContent += "0,U," + trim(sGrp) + "," + user + "," + trim(sPass) + "\n";
                found = true;
                continue;
            }
        }
        newContent += line + "\n";
    }

    if (!found) {
        fclose(file);
        return "ERROR rmusr: No se encontró el usuario '" + user + "'.";
    }

    writeUsersFile(file, sb, newContent, sbStart);
    fclose(file);

    return "Usuario '" + user + "' eliminado exitosamente.";
}

// ==================== CHGRP ====================
inline std::string cmd_chgrp(const std::map<std::string, std::string>& params) {
    if (params.find("user") == params.end())
        return "ERROR chgrp: Parámetro -user es obligatorio.";
    if (params.find("grp") == params.end())
        return "ERROR chgrp: Parámetro -grp es obligatorio.";

    std::string user = params.at("user");
    std::string grp = params.at("grp");

    MountedPart* mp = getLoggedMount();
    if (!mp) return "ERROR chgrp: No hay sesión activa.";
    if (std::string(mp->current_user) != "root")
        return "ERROR chgrp: Solo el usuario root puede cambiar grupos.";

    FILE* file = fopen(mp->path, "rb+");
    if (!file) return "ERROR chgrp: No se pudo abrir el disco.";

    int sbStart = mp->part_start;
    Superblock sb;
    fseek(file, sbStart, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::string usersContent = readUsersFile(file, sb);

    // Verificar que el grupo exista
    bool groupExists = false;
    std::istringstream stream(usersContent);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string sId, sType, sName;
        std::getline(ls, sId, ',');
        std::getline(ls, sType, ',');
        std::getline(ls, sName, ',');
        sName = trim(sName);
        if (sType == "G" && sId != "0" && sName == grp) {
            groupExists = true;
            break;
        }
    }

    if (!groupExists) {
        fclose(file);
        return "ERROR chgrp: El grupo '" + grp + "' no existe.";
    }

    // Buscar usuario y cambiar grupo
    bool found = false;
    std::string newContent;
    std::istringstream stream2(usersContent);
    while (std::getline(stream2, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string sId, sType, sGrp, sUser, sPass;
        std::getline(ls, sId, ',');
        std::getline(ls, sType, ',');
        if (sType == "U") {
            std::getline(ls, sGrp, ',');
            std::getline(ls, sUser, ',');
            std::getline(ls, sPass, ',');
            sUser = trim(sUser);
            sPass = trim(sPass);
            if (sId != "0" && sUser == user) {
                newContent += sId + ",U," + grp + "," + user + "," + sPass + "\n";
                found = true;
                continue;
            }
        }
        newContent += line + "\n";
    }

    if (!found) {
        fclose(file);
        return "ERROR chgrp: No se encontró el usuario '" + user + "'.";
    }

    writeUsersFile(file, sb, newContent, sbStart);
    fclose(file);

    return "Grupo del usuario '" + user + "' cambiado a '" + grp + "' exitosamente.";
}


#endif
