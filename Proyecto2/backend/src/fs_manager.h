#ifndef FS_MANAGER_H
#define FS_MANAGER_H

#include "structures.h"
#include "mount_manager.h"
#include "utils.h"
#include <string>
#include <cstdio>
#include <cstring>
#include <cmath>

// ==================== MKFS ====================
inline std::string cmd_mkfs(const std::map<std::string, std::string>& params) {
    if (params.find("id") == params.end())
        return "ERROR mkfs: Parámetro -id es obligatorio.";

    std::string id = params.at("id");
    std::string type = "full"; // por defecto
    if (params.find("type") != params.end()) {
        type = toLower(params.at("type"));
    }

    std::string fsType = "2fs"; 
    if (params.find("fs") != params.end()) {
        fsType = toLower(params.at("fs"));
    }

    // Buscar particion montada
    MountedPart* mp = getMountedById(id);
    if (!mp)
        return "ERROR mkfs: No se encontró la partición montada con ID '" + id + "'.";

    // Limpiar sesion activa en este montaje (formatear invalida datos previos)
    mp->logged_in = false;
    memset(mp->current_user, 0, 20);
    mp->current_uid = 0;
    mp->current_gid = 0;

    std::string diskPath = std::string(mp->path);
    std::string partName = std::string(mp->name);

    // Abrir disco y leer MBR
    FILE* file = fopen(diskPath.c_str(), "rb+");
    if (!file)
        return "ERROR mkfs: No se pudo abrir el disco.";

    MBR mbr;
    fseek(file, 0, SEEK_SET);
    fread(&mbr, sizeof(MBR), 1, file);

    // Buscar informacion de la particion
    int partStart = -1;
    int partSize = 0;

    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status == '1' &&
            std::string(mbr.mbr_partitions[i].part_name) == partName) {
            partStart = mbr.mbr_partitions[i].part_start;
            partSize = mbr.mbr_partitions[i].part_size;
            break;
        }
    }

    // Verificar particiones logicas
    if (partStart == -1) {
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '1' &&
                mbr.mbr_partitions[i].part_type == 'E') {
                int ebrPos = mbr.mbr_partitions[i].part_start;
                while (ebrPos != -1) {
                    EBR ebr;
                    fseek(file, ebrPos, SEEK_SET);
                    fread(&ebr, sizeof(EBR), 1, file);
                    if (ebr.part_mount == '1' && std::string(ebr.part_name) == partName) {
                        partStart = ebr.part_start + (int)sizeof(EBR);
                        partSize = ebr.part_size - (int)sizeof(EBR);
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
        return "ERROR mkfs: No se encontró la partición.";
    }

    int n = 0;
    if (fsType == "3fs") {
        int numerator = partSize - (int)sizeof(Superblock) - 50 * (int)sizeof(Journal);
        int denominator = 1 + 3 + (int)sizeof(Inode) + 3 * (int)sizeof(FileBlock);
        n = (int)std::floor((double)numerator / denominator);
    } else {
        int numerator = partSize - (int)sizeof(Superblock);
        int denominator = 1 + 3 + (int)sizeof(Inode) + 3 * (int)sizeof(FileBlock);
        n = (int)std::floor((double)numerator / denominator);
    }

    if (n <= 0) {
        fclose(file);
        return "ERROR mkfs: La partición es demasiado pequeña para formatear.";
    }

    int inodeCount = n;
    int blockCount = 3 * n;

    // Crear superbloque
    Superblock sb;
    sb.s_filesystem_type = (fsType == "3fs") ? 3 : 2;
    sb.s_inodes_count = inodeCount;
    sb.s_blocks_count = blockCount;
    sb.s_free_inodes_count = inodeCount;
    sb.s_free_blocks_count = blockCount;
    sb.s_mtime = getCurrentTime();
    sb.s_umtime = 0;
    sb.s_mnt_count = 1;
    sb.s_magic = 0xEF53;
    sb.s_inode_size = sizeof(Inode);
    sb.s_block_size = sizeof(FileBlock);
    sb.s_first_ino = 0;
    sb.s_first_blo = 0;

    // Distribucion: Superblock | [Journaling si es 3fs] | BM_Inodes | BM_Blocks | Inodes | Blocks
    int sbStart = partStart;
    int journalStart = sbStart + (int)sizeof(Superblock);
    int bmInodeStart = (fsType == "3fs") ? journalStart + 50 * (int)sizeof(Journal) : journalStart;
    int bmBlockStart = bmInodeStart + inodeCount;
    int inodeStart = bmBlockStart + blockCount;
    int blockStart = inodeStart + inodeCount * (int)sizeof(Inode);

    sb.s_bm_inode_start = bmInodeStart;
    sb.s_bm_block_start = bmBlockStart;
    sb.s_inode_start = inodeStart;
    sb.s_block_start = blockStart;

    // Limpiar el area de la particion
    char zero = 0;
    fseek(file, partStart, SEEK_SET);
    for (int i = 0; i < partSize; i++) {
        fwrite(&zero, 1, 1, file);
    }

    // Escribir superbloque
    fseek(file, sbStart, SEEK_SET);
    fwrite(&sb, sizeof(Superblock), 1, file);

    // Escribir estructuras de Journaling si es 3fs
    if (fsType == "3fs") {
        Journal emptyJournal;
        fseek(file, journalStart, SEEK_SET);
        for (int i = 0; i < 50; i++) {
            fwrite(&emptyJournal, sizeof(Journal), 1, file);
        }
    }

    // Inicializar bitmaps en 0
    // (ya quedaron en cero)

    // Crear directorio raiz (inodo 0)
    // Marcar inodo 0 como usado
    char one = '1';
    fseek(file, bmInodeStart, SEEK_SET);
    fwrite(&one, 1, 1, file);

    // Marcar bloque 0 como usado
    fseek(file, bmBlockStart, SEEK_SET);
    fwrite(&one, 1, 1, file);

    // Crear inodo raiz
    Inode rootInode;
    rootInode.i_uid = 1;
    rootInode.i_gid = 1;
    rootInode.i_size = 0;
    rootInode.i_atime = getCurrentTime();
    rootInode.i_ctime = getCurrentTime();
    rootInode.i_mtime = getCurrentTime();
    rootInode.i_block[0] = 0; // primer bloque de directorio
    rootInode.i_type = '0';   // carpeta
    rootInode.i_perm[0] = '6'; rootInode.i_perm[1] = '6'; rootInode.i_perm[2] = '4';

    fseek(file, inodeStart, SEEK_SET);
    fwrite(&rootInode, sizeof(Inode), 1, file);

    // Crear bloque de directorio raiz con . y ..
    DirBlock rootBlock;
    strcpy(rootBlock.b_content[0].b_name, ".");
    rootBlock.b_content[0].b_inodo = 0;
    strcpy(rootBlock.b_content[1].b_name, "..");
    rootBlock.b_content[1].b_inodo = 0;

    fseek(file, blockStart, SEEK_SET);
    fwrite(&rootBlock, sizeof(DirBlock), 1, file);

    // Crear archivo users.txt: grupo=root, usuario=root, password=123
    // Contenido: "1,G,root\n1,U,root,root,123\n"
    std::string usersContent = "1,G,root\n1,U,root,root,123\n";

    // Marcar inodo 1 como usado
    fseek(file, bmInodeStart + 1, SEEK_SET);
    fwrite(&one, 1, 1, file);

    // Marcar bloque 1 como usado
    fseek(file, bmBlockStart + 1, SEEK_SET);
    fwrite(&one, 1, 1, file);

    // Crear inodo de users.txt
    Inode usersInode;
    usersInode.i_uid = 1;
    usersInode.i_gid = 1;
    usersInode.i_size = (int)usersContent.size();
    usersInode.i_atime = getCurrentTime();
    usersInode.i_ctime = getCurrentTime();
    usersInode.i_mtime = getCurrentTime();
    usersInode.i_block[0] = 1;
    usersInode.i_type = '1'; // archivo
    usersInode.i_perm[0] = '6'; usersInode.i_perm[1] = '6'; usersInode.i_perm[2] = '4';

    fseek(file, inodeStart + (int)sizeof(Inode), SEEK_SET);
    fwrite(&usersInode, sizeof(Inode), 1, file);

    // Escribir contenido de users.txt en el bloque 1
    FileBlock usersBlock;
    memset(usersBlock.b_content, 0, 64);
    strncpy(usersBlock.b_content, usersContent.c_str(), 63);

    fseek(file, blockStart + (int)sizeof(FileBlock), SEEK_SET);
    fwrite(&usersBlock, sizeof(FileBlock), 1, file);

    // Agregar entrada users.txt al bloque del directorio raiz
    rootBlock.b_content[2].b_inodo = 1;
    strcpy(rootBlock.b_content[2].b_name, "users.txt");

    fseek(file, blockStart, SEEK_SET);
    fwrite(&rootBlock, sizeof(DirBlock), 1, file);

    // Actualizar superbloque
    sb.s_free_inodes_count = inodeCount - 2;
    sb.s_free_blocks_count = blockCount - 2;
    sb.s_first_ino = 2;
    sb.s_first_blo = 2;

    fseek(file, sbStart, SEEK_SET);
    fwrite(&sb, sizeof(Superblock), 1, file);

    fclose(file);

    if (fsType == "3fs") {
        appendJournalEntry(mp, "mkfs", "/" + partName, "fs=" + fsType);
    }
    
    std::string fsNameName = (fsType == "3fs") ? "EXT3" : "EXT2";

    return "Sistema de archivos " + fsNameName + " creado exitosamente en partición '" + partName + "'. "
           "Inodos: " + std::to_string(inodeCount) + ", Bloques: " + std::to_string(blockCount);
}


inline std::string cmd_loss(const std::map<std::string, std::string>& params) {
    if (params.find("id") == params.end())
        return "ERROR loss: Parámetro -id es obligatorio.";

    std::string id = params.at("id");
    MountedPart* mp = getMountedById(id);
    if (!mp) return "ERROR loss: Participón montada no encontrada.";

    FILE* file = fopen(mp->path, "rb+");
    if (!file) return "ERROR loss: No se pudo abrir el disco.";

    Superblock sb;
    fseek(file, mp->part_start, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    int startZero = sb.s_bm_inode_start;
    int endZero = sb.s_block_start + (sb.s_blocks_count * (int)sizeof(FileBlock));

    char buffer[4096];
    memset(buffer, 0, 4096);
    int remaining = endZero - startZero;
    fseek(file, startZero, SEEK_SET);
    while (remaining > 0) {
        int toWrite = (remaining > 4096) ? 4096 : remaining;
        fwrite(buffer, 1, toWrite, file);
        remaining -= toWrite;
    }
    
    fclose(file);
    return "Pérdida de datos simulada exitosamente (Bitmaps e Inodos/Bloques en 0).";
}

inline std::string cmd_journaling(const std::map<std::string, std::string>& params) {
    if (params.find("id") == params.end())
        return "ERROR journaling: Parámetro -id es obligatorio.";

    std::string id = params.at("id");
    MountedPart* mp = getMountedById(id);
    if (!mp) return "ERROR journaling: Partición no montada.";

    FILE* file = fopen(mp->path, "rb");
    if (!file) return "ERROR journaling: No se pudo abrir el disco.";

    Superblock sb;
    fseek(file, mp->part_start, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    if (sb.s_filesystem_type != 3) {
        fclose(file);
        return "ERROR journaling: La partición no es EXT3.";
    }

    int journalStart = mp->part_start + (int)sizeof(Superblock);
    
    std::string result = "=== REPORTE JOURNALING ===\n";
    result += "Operación\tPath\t\t\tContenido\t\tFecha\n";
    result += "---------\t----\t\t\t---------\t\t-----\n";
    int entryCount = 0;

    fseek(file, journalStart, SEEK_SET);
    for (int i = 0; i < 50; i++) {
        Journal j;
        fread(&j, sizeof(Journal), 1, file);
        if (j.j_content.i_operation[0] != '\0') { // Si hay operacion
            char dateBuff[20];
            struct tm* tm_info = localtime(&j.j_content.i_date);
            strftime(dateBuff, 20, "%Y-%m-%d %H:%M", tm_info);

            result += std::string(j.j_content.i_operation) + "\t" + 
                      std::string(j.j_content.i_path) + "\t" + 
                      std::string(j.j_content.i_content) + "\t" + 
                      std::string(dateBuff) + "\n";
            entryCount++;
        }
    }

    fclose(file);

    if (entryCount == 0) {
        result += "(Sin entradas de journaling registradas)\n";
    }
    result += "Total de entradas: " + std::to_string(entryCount);

    return result;
}
#endif
