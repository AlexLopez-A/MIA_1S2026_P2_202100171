#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <ctime>
#include <cstring>

// ==================== MBR & Partitions ====================

struct Partition {
    char part_status;        // 0=inactive, 1=active
    char part_type;          // P=primary, E=extended
    char part_fit;           // B=best, F=first, W=worst
    int  part_start;         // byte where partition begins
    int  part_size;          // partition size in bytes (part_s)
    char part_name[16];      // partition name
    int  part_correlative;   // correlativo de partición (-1 hasta montar)
    char part_id[4];         // ID generado al montar partición

    Partition() {
        part_status = '0';
        part_type = 'P';
        part_fit = 'W';
        part_start = -1;
        part_size = 0;
        memset(part_name, 0, 16);
        part_correlative = -1;
        memset(part_id, 0, 4);
    }
};

struct MBR {
    int  mbr_tamano;           // disk total size in bytes
    time_t mbr_fecha_creacion; // creation date
    int  mbr_disk_signature;   // unique random signature
    char mbr_disk_fit;         // B/F/W
    Partition mbr_partitions[4];

    MBR() {
        mbr_tamano = 0;
        mbr_fecha_creacion = 0;
        mbr_disk_signature = 0;
        mbr_disk_fit = 'F';
    }
};

struct EBR {
    char part_mount;       // 0=inactive, 1=active (mount status)
    char part_fit;         // B/F/W
    int  part_start;       // byte where this EBR starts
    int  part_size;        // logical partition size (including EBR)
    int  part_next;        // next EBR start (-1 if none)
    char part_name[16];

    EBR() {
        part_mount = '0';
        part_fit = 'W';
        part_start = -1;
        part_size = 0;
        part_next = -1;
        memset(part_name, 0, 16);
    }
};

// ==================== EXT2 Superblock ====================

struct Superblock {
    int  s_filesystem_type;    // 2 = ext2, 3 = ext3
    int  s_inodes_count;       // total inodes
    int  s_blocks_count;       // total blocks
    int  s_free_inodes_count;  // free inodes
    int  s_free_blocks_count;  // free blocks
    time_t s_mtime;            // last mount time
    time_t s_umtime;           // last unmount time
    int  s_mnt_count;          // mount count
    int  s_magic;              // 0xEF53
    int  s_inode_size;         // sizeof(Inode)
    int  s_block_size;         // sizeof(FileBlock) = 64
    int  s_first_ino;          // first free inode
    int  s_first_blo;          // first free block
    int  s_bm_inode_start;     // bitmap inodes start
    int  s_bm_block_start;     // bitmap blocks start
    int  s_inode_start;        // inode table start
    int  s_block_start;        // block area start

    Superblock() {
        s_filesystem_type = 2;
        s_inodes_count = 0;
        s_blocks_count = 0;
        s_free_inodes_count = 0;
        s_free_blocks_count = 0;
        s_mtime = 0;
        s_umtime = 0;
        s_mnt_count = 0;
        s_magic = 0xEF53;
        s_inode_size = 0;
        s_block_size = 64;
        s_first_ino = 0;
        s_first_blo = 0;
        s_bm_inode_start = 0;
        s_bm_block_start = 0;
        s_inode_start = 0;
        s_block_start = 0;
    }
};

// ==================== Inodes ====================

struct Inode {
    int    i_uid;         // user id owner
    int    i_gid;         // group id owner
    int    i_size;        // file size in bytes
    time_t i_atime;       // last access time
    time_t i_ctime;       // creation time
    time_t i_mtime;       // last modification time
    int    i_block[15];   // 12 direct + 1 simple indirect + 1 double + 1 triple
    char   i_type;        // '0'=folder, '1'=file
    char   i_perm[3];     // permissions e.g. "664"

    Inode() {
        i_uid = 0;
        i_gid = 0;
        i_size = 0;
        i_atime = 0;
        i_ctime = 0;
        i_mtime = 0;
        for (int i = 0; i < 15; i++) i_block[i] = -1;
        i_type = '0';
        i_perm[0] = '6'; i_perm[1] = '6'; i_perm[2] = '4';
    }
};

// ==================== Blocks ====================

// Entrada de contenido de bloque de directorio
struct DirContent {
    char b_name[12];
    int  b_inodo;

    DirContent() {
        memset(b_name, 0, 12);
        b_inodo = -1;
    }
};

// Bloque de directorio (64 bytes): 4 entradas de 16 bytes cada una
struct DirBlock {
    DirContent b_content[4];

    DirBlock() {}
};

// Bloque de archivo (64 bytes)
struct FileBlock {
    char b_content[64];

    FileBlock() {
        memset(b_content, 0, 64);
    }
};

// Bloque de apuntadores (64 bytes): 16 apuntadores
struct PointerBlock {
    int b_pointers[16];

    PointerBlock() {
        for (int i = 0; i < 16; i++) b_pointers[i] = -1;
    }
};

// ==================== Info de Montaje (runtime) ====================

struct MountedPart {
    char id[10];           // p. ej. "711A"
    char path[200];        // ruta del archivo de disco
    char name[16];         // nombre de la particion
    int  part_start;       // byte de inicio de la particion
    bool logged_in;        // hay un usuario logueado en este montaje?
    char current_user[20]; // usuario actualmente logueado
    int  current_uid;      // id del usuario logueado
    int  current_gid;      // id del grupo del usuario logueado

    MountedPart() {
        memset(id, 0, 10);
        memset(path, 0, 200);
        memset(name, 0, 16);
        part_start = 0;
        logged_in = false;
        memset(current_user, 0, 20);
        current_uid = 0;
        current_gid = 0;
    }
};

#endif // STRUCTURES_H
