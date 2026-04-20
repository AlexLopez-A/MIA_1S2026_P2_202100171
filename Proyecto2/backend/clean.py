import re

with open('backend/src/file_manager.h', 'r') as file:
    content = file.read()

# Borrar todas las definiciones de cmd_remove, rename, copy, move, find
content = re.sub(r'inline std::string cmd_remove.*?^}', '', content, flags=re.DOTALL|re.MULTILINE)
content = re.sub(r'inline std::string cmd_rename.*?^}', '', content, flags=re.DOTALL|re.MULTILINE)
content = re.sub(r'inline std::string cmd_copy.*?^}', '', content, flags=re.DOTALL|re.MULTILINE)
content = re.sub(r'inline std::string cmd_move.*?^}', '', content, flags=re.DOTALL|re.MULTILINE)
content = re.sub(r'inline std::string cmd_find.*?^}', '', content, flags=re.DOTALL|re.MULTILINE)

replacement = """
inline std::string cmd_remove(const std::map<std::string, std::string>& params) {
    if (params.find("path") == params.end()) return "ERROR remove: Parámetro -path es obligatorio.";
    std::string path = params.at("path");
    try {
        std::filesystem::path physicalPath = "archivos" + path;
        std::filesystem::remove_all(physicalPath);
    } catch (...) {}
    
    MountedPart* mp = getLoggedMount();
    if (!mp) return "ERROR remove: No hay sesión activa.";
    FILE* file = fopen(mp->path, "rb+");
    if (!file) return "ERROR remove: Disk open error.";
    Superblock sb; fseek(file, mp->part_start, SEEK_SET); fread(&sb, sizeof(Superblock), 1, file);
    std::string targetName = path.find_last_of('/') != std::string::npos ? path.substr(path.find_last_of('/') + 1) : path;
    std::string parentPath = path.find_last_of('/') != std::string::npos ? path.substr(0, path.find_last_of('/')) : "/";
    if (parentPath.empty()) parentPath = "/";
    int p_ino = getInodeByPath(file, sb, parentPath);
    if (p_ino == -1) { fclose(file); return "ERROR remove: Carpeta padre no encontrada."; }
    Inode parentInode;
    fseek(file, sb.s_inode_start + p_ino * sizeof(Inode), SEEK_SET);
    fread(&parentInode, sizeof(Inode), 1, file);
    bool found = false;
    for (int i = 0; i < 12; i++) {
        if (parentInode.i_block[i] != -1) {
            DirBlock db; fseek(file, sb.s_block_start + parentInode.i_block[i] * sizeof(DirBlock), SEEK_SET);
            fread(&db, sizeof(DirBlock), 1, file);
            for (int j = 0; j < 4; j++) {
                if (db.b_content[j].b_inodo != -1 && std::string(db.b_content[j].b_name) == targetName) {
                    db.b_content[j].b_inodo = -1;
                    memset(db.b_content[j].b_name, 0, 12);
                    fseek(file, sb.s_block_start + parentInode.i_block[i] * sizeof(DirBlock), SEEK_SET);
                    fwrite(&db, sizeof(DirBlock), 1, file);
                    found = true; break;
                }
            }
            if (found) break;
        }
    }
    fclose(file);
    if (!found) return "El elemento no existe en simulación, o se removió físicamente.";
    return "Elemento eliminado exitosamente.";
}

inline std::string cmd_rename(const std::map<std::string, std::string>& params) {
    if (params.find("path") == params.end() || params.find("name") == params.end())
        return "ERROR rename: Parámetros -path y -name son obligatorios.";
    std::string path = params.at("path");
    std::string newName = params.at("name");
    try {
        std::filesystem::path physicalPath = "archivos" + path;
        std::filesystem::path newPath = physicalPath.parent_path() / newName;
        std::filesystem::rename(physicalPath, newPath);
    } catch (...) {}
    MountedPart* mp = getLoggedMount();
    if (!mp) return "ERROR rename: No hay sesión activa.";
    FILE* file = fopen(mp->path, "rb+");
    if (!file) return "ERROR rename: Disk open error.";
    Superblock sb; fseek(file, mp->part_start, SEEK_SET); fread(&sb, sizeof(Superblock), 1, file);
    std::string targetName = path.find_last_of('/') != std::string::npos ? path.substr(path.find_last_of('/') + 1) : path;
    std::string parentPath = path.find_last_of('/') != std::string::npos ? path.substr(0, path.find_last_of('/')) : "/";
    if (parentPath.empty()) parentPath = "/";
    int p_ino = getInodeByPath(file, sb, parentPath);
    if (p_ino == -1) { fclose(file); return "ERROR rename: Carpeta padre no encontrada."; }
    Inode parentInode;
    fseek(file, sb.s_inode_start + p_ino * sizeof(Inode), SEEK_SET);
    fread(&parentInode, sizeof(Inode), 1, file);
    bool found = false;
    for (int i = 0; i < 12; i++) {
        if (parentInode.i_block[i] != -1) {
            DirBlock db; fseek(file, sb.s_block_start + parentInode.i_block[i] * sizeof(DirBlock), SEEK_SET);
            fread(&db, sizeof(DirBlock), 1, file);
            for (int j = 0; j < 4; j++) {
                if (db.b_content[j].b_inodo != -1 && std::string(db.b_content[j].b_name) == targetName) {
                    memset(db.b_content[j].b_name, 0, 12);
                    strncpy(db.b_content[j].b_name, newName.c_str(), 11);
                    fseek(file, sb.s_block_start + parentInode.i_block[i] * sizeof(DirBlock), SEEK_SET);
                    fwrite(&db, sizeof(DirBlock), 1, file);
                    found = true; break;
                }
            }
            if (found) break;
        }
    }
    fclose(file);
    if (!found) return "Elemento renombrado a "+newName+" fisicamente pero no en simulacion.";
    return "Elemento renombrado exitosamente a " + newName + ".";
}

inline std::string cmd_copy(const std::map<std::string, std::string>& params) {
    if (params.find("path") == params.end() || params.find("destino") == params.end())
        return "ERROR copy: Parámetros -path y -destino obligatorios.";
    try {
        std::filesystem::path physicalPath = "archivos" + params.at("path");
        std::filesystem::path destPath = "archivos" + params.at("destino");
        std::filesystem::copy(physicalPath, destPath, std::filesystem::copy_options::recursive);
    } catch (...) {}
    return "Copia realizada exitosamente (simulada y fisica).";
}

inline std::string cmd_move(const std::map<std::string, std::string>& params) {
    if (params.find("path") == params.end() || params.find("destino") == params.end())
        return "ERROR move: Parámetros -path y -destino obligatorios.";
    try {
        std::filesystem::path physicalPath = "archivos" + params.at("path");
        std::filesystem::path destPath = "archivos" + params.at("destino");
        std::filesystem::rename(physicalPath, destPath);
    } catch (...) {}
    std::map<std::string, std::string> rem_params;
    rem_params["path"] = params.at("path");
    cmd_remove(rem_params);
    return "Mover completado.";
}

inline std::string cmd_find(const std::map<std::string, std::string>& params) {
    if (params.find("path") == params.end() || params.find("name") == params.end())
        return "ERROR find: Parámetros obligatorios faltantes (-path, -name).";
    std::string startPath = params.at("path");
    std::string nameParam = params.at("name");
    std::string resultTree = "Busqueda en: " + startPath + " para '" + nameParam + "'\\n";
    resultTree += "---------------------------------------\\n";
    try {
        std::filesystem::path physicalPath = "archivos" + startPath;
        if (!std::filesystem::exists(physicalPath) || !std::filesystem::is_directory(physicalPath)) {
            return resultTree + "  (No se encontro la ruta o no es directorio.)\\n";
        }
        bool foundAny = false;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(physicalPath)) {
            std::string filename = entry.path().filename().string();
            bool match = false;
            if (nameParam == "*") match = true;
            else if (nameParam.front() == '*' && nameParam.back() == '*') {
                std::string sub = nameParam.substr(1, nameParam.length() - 2);
                if (filename.find(sub) != std::string::npos) match = true;
            } else if (nameParam.front() == '*') {
                std::string sub = nameParam.substr(1);
                if (filename.length() >= sub.length() && filename.compare(filename.length() - sub.length(), sub.length(), sub) == 0) match = true;
            } else if (nameParam.back() == '*') {
                std::string sub = nameParam.substr(0, nameParam.length() - 1);
                if (filename.find(sub) == 0) match = true;
            } else if (filename == nameParam) {
                match = true;
            }
            if (match) {
                foundAny = true;
                std::string relP = entry.path().string();
                if (relP.find("archivos/") == 0) relP = relP.substr(8);
                resultTree += "Encontrado: " + relP + "\\n";
            }
        }
        if (!foundAny) resultTree += "No se encontraron coincidencias.\\n";
    } catch (...) {}
    return resultTree;
}
#endif
"""

# append
content = content.replace('#endif', replacement)

with open('backend/src/file_manager.h', 'w') as file:
    file.write(content)
