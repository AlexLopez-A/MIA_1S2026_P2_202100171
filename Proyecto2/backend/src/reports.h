#ifndef REPORTS_H
#define REPORTS_H

#include "structures.h"
#include "mount_manager.h"
#include "file_manager.h"
#include "utils.h"
#include <string>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <cctype>

// ==================== Report Helpers ====================

// Genera DOT y renderiza en la ruta de salida
inline bool renderDot(const std::string& dotContent, const std::string& outputPath) {
    createParentDirs(outputPath);

    // Escribe archivo DOT junto a la salida (mismo nombre con extension .dot)
    std::string dotPath = getDotPath(outputPath);
    FILE* dotFile = fopen(dotPath.c_str(), "w");
    if (!dotFile) return false;
    fwrite(dotContent.c_str(), 1, dotContent.size(), dotFile);
    fclose(dotFile);

    // Determine output format from extension
    std::string ext = outputPath.substr(outputPath.find_last_of('.') + 1);
    // Map unsupported formats to png (e.g. jpg/jpeg not available in all Graphviz builds)
    std::string fmt = ext;
    if (fmt == "jpg" || fmt == "jpeg") fmt = "png";
    std::string cmd = "dot -T" + fmt + " \"" + dotPath + "\" -o \"" + outputPath + "\" 2>&1";
    int ret = system(cmd.c_str());
    return (ret == 0);
}

// ==================== MBR Report ====================
inline std::string reportMBR(const std::string& diskPath, const std::string& outputPath) {
    FILE* file = fopen(diskPath.c_str(), "rb");
    if (!file) return "ERROR rep: No se pudo abrir el disco '" + diskPath + "'.";

    MBR mbr;
    fseek(file, 0, SEEK_SET);
    fread(&mbr, sizeof(MBR), 1, file);

    std::stringstream dot;
    dot << "digraph MBR {\n";
    dot << "  node [shape=plaintext];\n";
    dot << "  mbr [label=<\n";
    dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
    dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4FC3F7\"><B>MBR REPORT</B></TD></TR>\n";
    dot << "    <TR><TD>mbr_tamano</TD><TD>" << mbr.mbr_tamano << "</TD></TR>\n";
    dot << "    <TR><TD>mbr_fecha_creacion</TD><TD>" << formatTime(mbr.mbr_fecha_creacion) << "</TD></TR>\n";
    dot << "    <TR><TD>mbr_disk_signature</TD><TD>" << mbr.mbr_disk_signature << "</TD></TR>\n";
    dot << "    <TR><TD>mbr_disk_fit</TD><TD>" << mbr.mbr_disk_fit << "</TD></TR>\n";

    auto& mounted = getMountedList();
    struct EbrRow {
        char status;
        char fit;
        int start;
        int size;
        int next;
        std::string name;
    };
    std::vector<EbrRow> ebrRows;

    auto isMounted = [&](const std::string& partName) -> bool {
        for (const auto& mp : mounted) {
            if (std::string(mp.path) == diskPath && std::string(mp.name) == partName) {
                return true;
            }
        }
        return false;
    };

    for (int i = 0; i < 4; i++) {
        Partition& p = mbr.mbr_partitions[i];
        if (p.part_status == '0' && p.part_start == -1) continue;
        dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#81C784\"><B>Particion " << (i+1) << "</B></TD></TR>\n";
        std::string pName(p.part_name, strnlen(p.part_name, 16));
        char mountedStatus = isMounted(pName) ? '1' : '0';
        dot << "    <TR><TD>part_status</TD><TD>" << mountedStatus << "</TD></TR>\n";
        dot << "    <TR><TD>part_type</TD><TD>" << p.part_type << "</TD></TR>\n";
        dot << "    <TR><TD>part_fit</TD><TD>" << p.part_fit << "</TD></TR>\n";
        dot << "    <TR><TD>part_start</TD><TD>" << p.part_start << "</TD></TR>\n";
        dot << "    <TR><TD>part_size</TD><TD>" << p.part_size << "</TD></TR>\n";
        dot << "    <TR><TD>part_name</TD><TD>" << escapeGraphviz(pName) << "</TD></TR>\n";
        dot << "    <TR><TD>part_correlative</TD><TD>" << p.part_correlative << "</TD></TR>\n";
        dot << "    <TR><TD>part_id</TD><TD>" << std::string(p.part_id, strnlen(p.part_id, 4)) << "</TD></TR>\n";

        // Si es extendida, mostrar logicas en la tabla MBR y recolectar filas para la tabla EBR inferior.
        if (std::toupper(static_cast<unsigned char>(p.part_type)) == 'E') {
            int extStart = p.part_start;
            int extEnd = p.part_start + p.part_size;
            int ebrPos = p.part_start;
            int logNum = 1;
            int guard = 0;
            while (ebrPos != -1 && guard < 256) {
                if (ebrPos < extStart || ebrPos >= extEnd) break;

                EBR ebr;
                fseek(file, ebrPos, SEEK_SET);
                fread(&ebr, sizeof(EBR), 1, file);

                std::string ebrName(ebr.part_name, strnlen(ebr.part_name, 16));
                bool isHeadEbr = (logNum == 1);
                bool hasData = (ebr.part_size > 0) || (ebr.part_next != -1) || !ebrName.empty();

                // Keep head EBR when it exists; stop when no more chained logical descriptors.
                if (!isHeadEbr && !hasData) break;

                char ebrMountedStatus = isMounted(ebrName) ? '1' : '0';
                dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#E57373\"><B>Particion Logica " << logNum << "</B></TD></TR>\n";
                dot << "    <TR><TD>part_status</TD><TD>" << ebrMountedStatus << "</TD></TR>\n";
                dot << "    <TR><TD>part_fit</TD><TD>" << ebr.part_fit << "</TD></TR>\n";
                int shownStart = (ebr.part_start == -1) ? ebrPos : ebr.part_start;
                dot << "    <TR><TD>part_start</TD><TD>" << shownStart << "</TD></TR>\n";
                dot << "    <TR><TD>part_size</TD><TD>" << ebr.part_size << "</TD></TR>\n";
                dot << "    <TR><TD>part_next</TD><TD>" << ebr.part_next << "</TD></TR>\n";
                dot << "    <TR><TD>part_name</TD><TD>" << escapeGraphviz(ebrName) << "</TD></TR>\n";

                EbrRow row;
                row.status = ebrMountedStatus;
                row.fit = ebr.part_fit;
                row.start = shownStart;
                row.size = ebr.part_size;
                row.next = ebr.part_next;
                row.name = ebrName;
                ebrRows.push_back(row);

                if (ebr.part_next == ebrPos) break;
                ebrPos = ebr.part_next;
                logNum++;
                guard++;
            }
        }
    }

    dot << "    </TABLE>\n  >];\n";

    if (!ebrRows.empty()) {
        dot << "  ebr [label=<\n";
        dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
        dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4FC3F7\"><B>EBR</B></TD></TR>\n";
        for (size_t i = 0; i < ebrRows.size(); i++) {
            const EbrRow& r = ebrRows[i];
            dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#81C784\"><B>Particion " << (i + 1) << "</B></TD></TR>\n";
            dot << "    <TR><TD>part_status</TD><TD>" << r.status << "</TD></TR>\n";
            dot << "    <TR><TD>part_type</TD><TD>p</TD></TR>\n";
            dot << "    <TR><TD>part_fit</TD><TD>" << r.fit << "</TD></TR>\n";
            dot << "    <TR><TD>part_start</TD><TD>" << r.start << "</TD></TR>\n";
            dot << "    <TR><TD>part_size</TD><TD>" << r.size << "</TD></TR>\n";
            dot << "    <TR><TD>part_name</TD><TD>" << escapeGraphviz(r.name) << "</TD></TR>\n";
        }
        dot << "    </TABLE>\n  >];\n";
        // Keep both tables in the same image, stacked vertically, without visible connectors.
        dot << "  mbr -> ebr [style=invis];\n";
    }

    dot << "}\n";
    fclose(file);

    if (!renderDot(dot.str(), outputPath))
        return "ERROR rep: No se pudo generar el reporte. ¿Está instalado Graphviz?";

    return "Reporte MBR generado en '" + outputPath + "'.";
}

// ==================== DISK Report ====================
inline std::string reportDISK(const std::string& diskPath, const std::string& outputPath) {
    FILE* file = fopen(diskPath.c_str(), "rb");
    if (!file) return "ERROR rep: No se pudo abrir el disco '" + diskPath + "'.";

    MBR mbr;
    fseek(file, 0, SEEK_SET);
    fread(&mbr, sizeof(MBR), 1, file);

    int totalSize = mbr.mbr_tamano;

    std::stringstream dot;
    auto pctStr = [](double value) {
        std::ostringstream os;
        os.setf(std::ios::fixed);
        os.precision(2);
        os << value;
        return os.str();
    };

    dot << "digraph DISK {\n";
    dot << "  node [shape=plaintext];\n";
    dot << "  disk [label=<\n";
    dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
    dot << "    <TR>\n";
    dot << "      <TD BGCOLOR=\"#90CAF9\"><B>MBR</B></TD>\n";

    // Sort partitions by start position
    struct PInfo { int start; int size; char type; std::string name; bool active; };
    std::vector<PInfo> sortedParts;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status == '1' && mbr.mbr_partitions[i].part_start != -1) {
            PInfo pi;
            pi.start = mbr.mbr_partitions[i].part_start;
            pi.size = mbr.mbr_partitions[i].part_size;
            pi.type = mbr.mbr_partitions[i].part_type;
            pi.name = std::string(mbr.mbr_partitions[i].part_name);
            pi.active = true;
            sortedParts.push_back(pi);
        }
    }
    std::sort(sortedParts.begin(), sortedParts.end(), [](const PInfo& a, const PInfo& b) {
        return a.start < b.start;
    });

    int usedAfterMBR = (int)sizeof(MBR);
    for (auto& p : sortedParts) {
        // Free space before partition
        if (p.start > usedAfterMBR) {
            double freePct = 100.0 * (p.start - usedAfterMBR) / totalSize;
            dot << "      <TD BGCOLOR=\"#E0E0E0\">Libre<BR/>" << pctStr(freePct) << "% del disco</TD>\n";
        }

        if (std::toupper(static_cast<unsigned char>(p.type)) == 'E') {
            // Extended partition with logical partitions inside
            dot << "      <TD BGCOLOR=\"#A5D6A7\">\n";
            dot << "        <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
            dot << "        <TR><TD COLSPAN=\"10\" BGCOLOR=\"#66BB6A\"><B>Extendida</B></TD></TR>\n";
            dot << "        <TR>\n";

            // Buscar la particion extendida y leer EBRs
            int ebrPos = p.start;
            int lastEnd = p.start;
            while (ebrPos != -1) {
                EBR ebr;
                fseek(file, ebrPos, SEEK_SET);
                fread(&ebr, sizeof(EBR), 1, file);
                if (ebr.part_mount == '0' && ebr.part_size == 0) break;

                // Free space inside extended
                if (ebrPos > lastEnd) {
                    double freePct = 100.0 * (ebrPos - lastEnd) / totalSize;
                    dot << "          <TD BGCOLOR=\"#E0E0E0\">Libre<BR/>" << pctStr(freePct) << "%</TD>\n";
                }

                double pct = 100.0 * ebr.part_size / totalSize;
                dot << "          <TD BGCOLOR=\"#FFF176\">EBR</TD>\n";
                dot << "          <TD BGCOLOR=\"#FFD54F\">" << escapeGraphviz(std::string(ebr.part_name));
                dot << "<BR/>Logica<BR/>" << pctStr(pct) << "%</TD>\n";

                lastEnd = ebrPos + ebr.part_size;
                ebrPos = ebr.part_next;
            }

            // Free space at end of extended
            if (lastEnd < p.start + p.size) {
                double freePct = 100.0 * (p.start + p.size - lastEnd) / totalSize;
                dot << "          <TD BGCOLOR=\"#E0E0E0\">Libre<BR/>" << pctStr(freePct) << "%</TD>\n";
            }

            dot << "        </TR>\n";
            dot << "        </TABLE>\n";
            dot << "      </TD>\n";
        } else {
            double pct = 100.0 * p.size / totalSize;
            dot << "      <TD BGCOLOR=\"#81C784\">" << escapeGraphviz(p.name);
            dot << "<BR/>Primaria<BR/>" << pctStr(pct) << "% del disco</TD>\n";
        }

        usedAfterMBR = p.start + p.size;
    }

    // Free space at end
    if (usedAfterMBR < totalSize) {
        double freePct = 100.0 * (totalSize - usedAfterMBR) / totalSize;
        dot << "      <TD BGCOLOR=\"#E0E0E0\">Libre<BR/>" << pctStr(freePct) << "% del disco</TD>\n";
    }

    dot << "    </TR>\n";
    dot << "    </TABLE>\n  >];\n}\n";
    fclose(file);

    if (!renderDot(dot.str(), outputPath))
        return "ERROR rep: No se pudo generar el reporte DISK.";

    return "Reporte DISK generado en '" + outputPath + "'.";
}

// ==================== Superblock Report ====================
inline std::string reportSB(MountedPart* mp, const std::string& outputPath) {
    FILE* file = fopen(mp->path, "rb");
    if (!file) return "ERROR rep: No se pudo abrir el disco '" + std::string(mp->path) + "'.";

    Superblock sb;
    fseek(file, mp->part_start, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);
    fclose(file);

    std::stringstream dot;
    dot << "digraph SB {\n";
    dot << "  node [shape=plaintext];\n";
    dot << "  sb [label=<\n";
    dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
    dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#CE93D8\"><B>SUPERBLOCK REPORT</B></TD></TR>\n";
    dot << "    <TR><TD>s_filesystem_type</TD><TD>" << sb.s_filesystem_type << "</TD></TR>\n";
    dot << "    <TR><TD>s_inodes_count</TD><TD>" << sb.s_inodes_count << "</TD></TR>\n";
    dot << "    <TR><TD>s_blocks_count</TD><TD>" << sb.s_blocks_count << "</TD></TR>\n";
    dot << "    <TR><TD>s_free_inodes_count</TD><TD>" << sb.s_free_inodes_count << "</TD></TR>\n";
    dot << "    <TR><TD>s_free_blocks_count</TD><TD>" << sb.s_free_blocks_count << "</TD></TR>\n";
    dot << "    <TR><TD>s_mtime</TD><TD>" << formatTime(sb.s_mtime) << "</TD></TR>\n";
    dot << "    <TR><TD>s_umtime</TD><TD>" << formatTime(sb.s_umtime) << "</TD></TR>\n";
    dot << "    <TR><TD>s_mnt_count</TD><TD>" << sb.s_mnt_count << "</TD></TR>\n";
    dot << "    <TR><TD>s_magic</TD><TD>0x" << std::hex << sb.s_magic << std::dec << "</TD></TR>\n";
    dot << "    <TR><TD>s_inode_size</TD><TD>" << sb.s_inode_size << "</TD></TR>\n";
    dot << "    <TR><TD>s_block_size</TD><TD>" << sb.s_block_size << "</TD></TR>\n";
    dot << "    <TR><TD>s_first_ino</TD><TD>" << sb.s_first_ino << "</TD></TR>\n";
    dot << "    <TR><TD>s_first_blo</TD><TD>" << sb.s_first_blo << "</TD></TR>\n";
    dot << "    <TR><TD>s_bm_inode_start</TD><TD>" << sb.s_bm_inode_start << "</TD></TR>\n";
    dot << "    <TR><TD>s_bm_block_start</TD><TD>" << sb.s_bm_block_start << "</TD></TR>\n";
    dot << "    <TR><TD>s_inode_start</TD><TD>" << sb.s_inode_start << "</TD></TR>\n";
    dot << "    <TR><TD>s_block_start</TD><TD>" << sb.s_block_start << "</TD></TR>\n";
    dot << "    </TABLE>\n  >];\n}\n";

    if (!renderDot(dot.str(), outputPath))
        return "ERROR rep: No se pudo generar el reporte SB.";

    return "Reporte SB generado en '" + outputPath + "'.";
}

// ==================== Inode Report ====================
inline std::string reportInode(MountedPart* mp, const std::string& outputPath) {
    FILE* file = fopen(mp->path, "rb");
    if (!file) return "ERROR rep: No se pudo abrir el disco '" + std::string(mp->path) + "'.";

    Superblock sb;
    fseek(file, mp->part_start, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::stringstream dot;
    dot << "digraph Inodes {\n";
    dot << "  rankdir=LR;\n";
    dot << "  node [shape=plaintext];\n";

    std::vector<int> usedInodes;

    for (int i = 0; i < sb.s_inodes_count; i++) {
        char bm;
        fseek(file, sb.s_bm_inode_start + i, SEEK_SET);
        fread(&bm, 1, 1, file);
        if (bm != '1') continue;

        usedInodes.push_back(i);
        Inode inode;
        fseek(file, sb.s_inode_start + i * (int)sizeof(Inode), SEEK_SET);
        fread(&inode, sizeof(Inode), 1, file);

        dot << "  inode" << i << " [label=<\n";
        dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
        dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4FC3F7\"><B>Inode " << i << "</B></TD></TR>\n";
        dot << "    <TR><TD>i_uid</TD><TD>" << inode.i_uid << "</TD></TR>\n";
        dot << "    <TR><TD>i_gid</TD><TD>" << inode.i_gid << "</TD></TR>\n";
        dot << "    <TR><TD>i_size</TD><TD>" << inode.i_size << "</TD></TR>\n";
        dot << "    <TR><TD>i_atime</TD><TD>" << formatTime(inode.i_atime) << "</TD></TR>\n";
        dot << "    <TR><TD>i_ctime</TD><TD>" << formatTime(inode.i_ctime) << "</TD></TR>\n";
        dot << "    <TR><TD>i_mtime</TD><TD>" << formatTime(inode.i_mtime) << "</TD></TR>\n";
        dot << "    <TR><TD>i_type</TD><TD>" << (inode.i_type == '0' ? "Carpeta" : "Archivo") << "</TD></TR>\n";
        dot << "    <TR><TD>i_perm</TD><TD>" << std::string(inode.i_perm, 3) << "</TD></TR>\n";
        for (int j = 0; j < 15; j++) {
            dot << "    <TR><TD>i_block[" << j << "]</TD><TD>" << inode.i_block[j] << "</TD></TR>\n";
        }
        dot << "    </TABLE>\n  >];\n";
    }

    // Conectar inodos
    for (size_t i = 1; i < usedInodes.size(); i++) {
        dot << "  inode" << usedInodes[i-1] << " -> inode" << usedInodes[i] << ";\n";
    }

    dot << "}\n";
    fclose(file);

    if (!renderDot(dot.str(), outputPath))
        return "ERROR rep: No se pudo generar el reporte INODE.";

    return "Reporte INODE generado en '" + outputPath + "'.";
}

// ==================== Block Report ====================
inline std::string reportBlock(MountedPart* mp, const std::string& outputPath) {
    FILE* file = fopen(mp->path, "rb");
    if (!file) return "ERROR rep: No se pudo abrir el disco '" + std::string(mp->path) + "'.";

    Superblock sb;
    fseek(file, mp->part_start, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::stringstream dot;
    dot << "digraph Blocks {\n";
    dot << "  rankdir=LR;\n";
    dot << "  node [shape=plaintext];\n";

    std::vector<int> usedBlocks;

    // Determinar que inodos usan que bloques para identificar el tipo de bloque
    for (int i = 0; i < sb.s_inodes_count; i++) {
        char bm;
        fseek(file, sb.s_bm_inode_start + i, SEEK_SET);
        fread(&bm, 1, 1, file);
        if (bm != '1') continue;

        Inode inode;
        fseek(file, sb.s_inode_start + i * (int)sizeof(Inode), SEEK_SET);
        fread(&inode, sizeof(Inode), 1, file);

        bool isDir = (inode.i_type == '0');

        // Procesar bloques directos
        for (int j = 0; j < 12; j++) {
            if (inode.i_block[j] == -1) continue;
            int blk = inode.i_block[j];

            if (isDir) {
                DirBlock db;
                fseek(file, sb.s_block_start + blk * (int)sizeof(FileBlock), SEEK_SET);
                fread(&db, sizeof(DirBlock), 1, file);

                dot << "  block" << blk << " [label=<\n";
                dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
                dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#81C784\"><B>Dir Block " << blk << "</B></TD></TR>\n";
                for (int k = 0; k < 4; k++) {
                    std::string bname(db.b_content[k].b_name, strnlen(db.b_content[k].b_name, 12));
                    dot << "    <TR><TD>" << escapeGraphviz(bname) << "</TD><TD>" << db.b_content[k].b_inodo << "</TD></TR>\n";
                }
                dot << "    </TABLE>\n  >];\n";
            } else {
                FileBlock fb;
                fseek(file, sb.s_block_start + blk * (int)sizeof(FileBlock), SEEK_SET);
                fread(&fb, sizeof(FileBlock), 1, file);

                std::string content(fb.b_content, strnlen(fb.b_content, 64));
                dot << "  block" << blk << " [label=<\n";
                dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
                dot << "    <TR><TD BGCOLOR=\"#FFD54F\"><B>File Block " << blk << "</B></TD></TR>\n";
                dot << "    <TR><TD>" << escapeGraphviz(content) << "</TD></TR>\n";
                dot << "    </TABLE>\n  >];\n";
            }
            usedBlocks.push_back(blk);
        }

        // Procesar bloques indirectos (i_block[12])
        if (inode.i_block[12] != -1) {
            int pblk = inode.i_block[12];
            PointerBlock pb;
            fseek(file, sb.s_block_start + pblk * (int)sizeof(FileBlock), SEEK_SET);
            fread(&pb, sizeof(PointerBlock), 1, file);

            dot << "  block" << pblk << " [label=<\n";
            dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
            dot << "    <TR><TD BGCOLOR=\"#CE93D8\"><B>Pointer Block " << pblk << "</B></TD></TR>\n";
            for (int k = 0; k < 16; k++) {
                dot << "    <TR><TD>" << pb.b_pointers[k] << "</TD></TR>\n";
            }
            dot << "    </TABLE>\n  >];\n";
            usedBlocks.push_back(pblk);

            for (int k = 0; k < 16; k++) {
                if (pb.b_pointers[k] == -1) continue;
                int sblk = pb.b_pointers[k];
                if (isDir) {
                    DirBlock db;
                    fseek(file, sb.s_block_start + sblk * (int)sizeof(FileBlock), SEEK_SET);
                    fread(&db, sizeof(DirBlock), 1, file);
                    dot << "  block" << sblk << " [label=<\n";
                    dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
                    dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#81C784\"><B>Dir Block " << sblk << "</B></TD></TR>\n";
                    for (int m = 0; m < 4; m++) {
                        std::string bname(db.b_content[m].b_name, strnlen(db.b_content[m].b_name, 12));
                        dot << "    <TR><TD>" << escapeGraphviz(bname) << "</TD><TD>" << db.b_content[m].b_inodo << "</TD></TR>\n";
                    }
                    dot << "    </TABLE>\n  >];\n";
                } else {
                    FileBlock fb;
                    fseek(file, sb.s_block_start + sblk * (int)sizeof(FileBlock), SEEK_SET);
                    fread(&fb, sizeof(FileBlock), 1, file);
                    std::string content(fb.b_content, strnlen(fb.b_content, 64));
                    dot << "  block" << sblk << " [label=<\n";
                    dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
                    dot << "    <TR><TD BGCOLOR=\"#FFD54F\"><B>File Block " << sblk << "</B></TD></TR>\n";
                    dot << "    <TR><TD>" << escapeGraphviz(content) << "</TD></TR>\n";
                    dot << "    </TABLE>\n  >];\n";
                }
                usedBlocks.push_back(sblk);
            }
        }
    }

    // Conectar bloques
    std::sort(usedBlocks.begin(), usedBlocks.end());
    for (size_t i = 1; i < usedBlocks.size(); i++) {
        dot << "  block" << usedBlocks[i-1] << " -> block" << usedBlocks[i] << ";\n";
    }

    dot << "}\n";
    fclose(file);

    if (!renderDot(dot.str(), outputPath))
        return "ERROR rep: No se pudo generar el reporte BLOCK.";

    return "Reporte BLOCK generado en '" + outputPath + "'.";
}

// ==================== Bitmap Inodes Report ====================
inline std::string reportBMInode(MountedPart* mp, const std::string& outputPath) {
    FILE* file = fopen(mp->path, "rb");
    if (!file) return "ERROR rep: No se pudo abrir el disco '" + std::string(mp->path) + "'.";

    Superblock sb;
    fseek(file, mp->part_start, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::string txt;
    for (int i = 0; i < sb.s_inodes_count; i++) {
        char bm;
        fseek(file, sb.s_bm_inode_start + i, SEEK_SET);
        fread(&bm, 1, 1, file);
        txt += (bm == '1') ? "1" : "0";
        if ((i + 1) % 20 == 0) txt += "\n";
        else txt += " ";
    }

    fclose(file);

    // Escribir como archivo de texto
    createParentDirs(outputPath);
    FILE* out = fopen(outputPath.c_str(), "w");
    if (!out) return "ERROR rep: No se pudo crear archivo de reporte.";
    fwrite(txt.c_str(), 1, txt.size(), out);
    fclose(out);

    return "Reporte BM_INODE generado en '" + outputPath + "'.";
}

// ==================== Bitmap Blocks Report ====================
inline std::string reportBMBlock(MountedPart* mp, const std::string& outputPath) {
    FILE* file = fopen(mp->path, "rb");
    if (!file) return "ERROR rep: No se pudo abrir el disco '" + std::string(mp->path) + "'.";

    Superblock sb;
    fseek(file, mp->part_start, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::string txt;
    for (int i = 0; i < sb.s_blocks_count; i++) {
        char bm;
        fseek(file, sb.s_bm_block_start + i, SEEK_SET);
        fread(&bm, 1, 1, file);
        txt += (bm == '1') ? "1" : "0";
        if ((i + 1) % 20 == 0) txt += "\n";
        else txt += " ";
    }

    fclose(file);

    createParentDirs(outputPath);
    FILE* out = fopen(outputPath.c_str(), "w");
    if (!out) return "ERROR rep: No se pudo crear archivo de reporte.";
    fwrite(txt.c_str(), 1, txt.size(), out);
    fclose(out);

    return "Reporte BM_BLOCK generado en '" + outputPath + "'.";
}

// ==================== Tree Report ====================
// Auxiliar recursivo para construir el arbol
inline void buildTree(FILE* file, Superblock& sb, int inodeIdx, std::stringstream& dot,
                       std::stringstream& edges, const std::string& prefix) {
    Inode inode;
    fseek(file, sb.s_inode_start + inodeIdx * (int)sizeof(Inode), SEEK_SET);
    fread(&inode, sizeof(Inode), 1, file);

    // Inode node
    dot << "  inode" << inodeIdx << " [label=<\n";
    dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
    dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4FC3F7\"><B>Inode " << inodeIdx << "</B></TD></TR>\n";
    dot << "    <TR><TD>i_uid</TD><TD>" << inode.i_uid << "</TD></TR>\n";
    dot << "    <TR><TD>i_gid</TD><TD>" << inode.i_gid << "</TD></TR>\n";
    dot << "    <TR><TD>i_size</TD><TD>" << inode.i_size << "</TD></TR>\n";
    dot << "    <TR><TD>i_type</TD><TD>" << (inode.i_type == '0' ? "Carpeta" : "Archivo") << "</TD></TR>\n";
    dot << "    <TR><TD>i_perm</TD><TD>" << std::string(inode.i_perm, 3) << "</TD></TR>\n";
    for (int j = 0; j < 15; j++) {
        dot << "    <TR><TD>i_block[" << j << "]</TD><TD>" << inode.i_block[j] << "</TD></TR>\n";
    }
    dot << "    </TABLE>\n  >];\n";

    if (inode.i_type == '0') {
        // Directorio: procesar bloques directos
        for (int b = 0; b < 12; b++) {
            if (inode.i_block[b] == -1) continue;
            int blk = inode.i_block[b];

            DirBlock db;
            fseek(file, sb.s_block_start + blk * (int)sizeof(FileBlock), SEEK_SET);
            fread(&db, sizeof(DirBlock), 1, file);

            dot << "  block" << blk << " [label=<\n";
            dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
            dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#81C784\"><B>Dir Block " << blk << "</B></TD></TR>\n";
            for (int k = 0; k < 4; k++) {
                std::string bname(db.b_content[k].b_name, strnlen(db.b_content[k].b_name, 12));
                dot << "    <TR><TD>" << escapeGraphviz(bname) << "</TD><TD>" << db.b_content[k].b_inodo << "</TD></TR>\n";
            }
            dot << "    </TABLE>\n  >];\n";

            edges << "  inode" << inodeIdx << " -> block" << blk << ";\n";

            // Recurse into children (skip . and ..)
            for (int k = 0; k < 4; k++) {
                if (db.b_content[k].b_inodo == -1) continue;
                std::string bname(db.b_content[k].b_name, strnlen(db.b_content[k].b_name, 12));
                if (bname == "." || bname == "..") continue;
                edges << "  block" << blk << " -> inode" << db.b_content[k].b_inodo << ";\n";
                buildTree(file, sb, db.b_content[k].b_inodo, dot, edges, prefix + "/" + bname);
            }
        }
    } else {
        // Archivo: mostrar bloques
        for (int b = 0; b < 12; b++) {
            if (inode.i_block[b] == -1) continue;
            int blk = inode.i_block[b];
            FileBlock fb;
            fseek(file, sb.s_block_start + blk * (int)sizeof(FileBlock), SEEK_SET);
            fread(&fb, sizeof(FileBlock), 1, file);

            std::string content(fb.b_content, strnlen(fb.b_content, 64));
            dot << "  block" << blk << " [label=<\n";
            dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
            dot << "    <TR><TD BGCOLOR=\"#FFD54F\"><B>File Block " << blk << "</B></TD></TR>\n";
            dot << "    <TR><TD>" << escapeGraphviz(content) << "</TD></TR>\n";
            dot << "    </TABLE>\n  >];\n";

            edges << "  inode" << inodeIdx << " -> block" << blk << ";\n";
        }
    }
}

inline std::string reportTree(MountedPart* mp, const std::string& outputPath) {
    FILE* file = fopen(mp->path, "rb");
    if (!file) return "ERROR rep: No se pudo abrir el disco '" + std::string(mp->path) + "'.";

    Superblock sb;
    fseek(file, mp->part_start, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::stringstream dot, edges;
    dot << "digraph Tree {\n";
    dot << "  rankdir=LR;\n";
    dot << "  node [shape=plaintext];\n";

    buildTree(file, sb, 0, dot, edges, "");

    dot << edges.str();
    dot << "}\n";
    fclose(file);

    if (!renderDot(dot.str(), outputPath))
        return "ERROR rep: No se pudo generar el reporte TREE.";

    return "Reporte TREE generado en '" + outputPath + "'.";
}

// ==================== Reporte de Archivo ====================
inline std::string reportFile(MountedPart* mp, const std::string& outputPath, const std::string& filePath) {
    FILE* file = fopen(mp->path, "rb");
    if (!file) return "ERROR rep: No se pudo abrir el disco '" + std::string(mp->path) + "'.";

    Superblock sb;
    fseek(file, mp->part_start, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    std::vector<std::string> parts = splitPath(filePath);
    int currentInode = 0;
    for (auto& part : parts) {
        currentInode = findInDir(file, sb, currentInode, part);
        if (currentInode == -1) {
            fclose(file);
            return "ERROR rep: No se encontró '" + filePath + "'.";
        }
    }

    std::string content = readFileContent(file, sb, currentInode);
    fclose(file);

    // Escribir contenido como reporte de texto
    createParentDirs(outputPath);
    FILE* out = fopen(outputPath.c_str(), "w");
    if (!out) return "ERROR rep: No se pudo crear archivo de reporte.";
    fwrite(content.c_str(), 1, content.size(), out);
    fclose(out);

    return "Reporte FILE generado en '" + outputPath + "'.";
}

// ==================== LS Report ====================
inline void buildLSRows(FILE* file, Superblock& sb, int inodeIdx, const std::string& usersContent,
                          std::stringstream& rows, const std::string& currentPath) {
    Inode inode;
    fseek(file, sb.s_inode_start + inodeIdx * (int)sizeof(Inode), SEEK_SET);
    fread(&inode, sizeof(Inode), 1, file);

    // Buscar nombre de propietario y grupo desde uid/gid
    std::string ownerName = "?", groupName = "?";
    std::istringstream stream(usersContent);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string sId, sType, sArg1, sArg2;
        std::getline(ls, sId, ',');
        std::getline(ls, sType, ',');
        std::getline(ls, sArg1, ',');
        if (sType == "G" && sId != "0") {
            int gid = std::stoi(trim(sId));
            if (gid == inode.i_gid) groupName = trim(sArg1);
        } else if (sType == "U" && sId != "0") {
            std::getline(ls, sArg2, ',');
            int uid = std::stoi(trim(sId));
            if (uid == inode.i_uid) ownerName = trim(sArg2);
        }
    }

    // Permission string
    std::string permStr;
    int owner = inode.i_perm[0] - '0';
    int group = inode.i_perm[1] - '0';
    int other = inode.i_perm[2] - '0';
    auto permBits = [](int v) -> std::string {
        std::string s;
        s += (v & 4) ? 'r' : '-';
        s += (v & 2) ? 'w' : '-';
        s += (v & 1) ? 'x' : '-';
        return s;
    };
    permStr = (inode.i_type == '0' ? "d" : "-") + permBits(owner) + permBits(group) + permBits(other);

    rows << "    <TR>";
    rows << "<TD>" << permStr << "</TD>";
    rows << "<TD>" << ownerName << "</TD>";
    rows << "<TD>" << groupName << "</TD>";
    rows << "<TD>" << inode.i_size << "</TD>";
    rows << "<TD>" << formatTime(inode.i_mtime) << "</TD>";
    rows << "<TD>" << currentPath << "</TD>";
    rows << "</TR>\n";

    // If directory, recurse
    if (inode.i_type == '0') {
        for (int b = 0; b < 12; b++) {
            if (inode.i_block[b] == -1) continue;
            DirBlock db;
            fseek(file, sb.s_block_start + inode.i_block[b] * (int)sizeof(FileBlock), SEEK_SET);
            fread(&db, sizeof(DirBlock), 1, file);
            for (int k = 0; k < 4; k++) {
                if (db.b_content[k].b_inodo == -1) continue;
                std::string name(db.b_content[k].b_name, strnlen(db.b_content[k].b_name, 12));
                if (name == "." || name == "..") continue;
                buildLSRows(file, sb, db.b_content[k].b_inodo, usersContent, rows,
                            currentPath + "/" + name);
            }
        }
    }
}

inline std::string reportLS(MountedPart* mp, const std::string& outputPath, const std::string& dirPath) {
    FILE* file = fopen(mp->path, "rb");
    if (!file) return "ERROR rep: No se pudo abrir el disco '" + std::string(mp->path) + "'.";

    Superblock sb;
    fseek(file, mp->part_start, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, file);

    // Navegar al destino
    std::vector<std::string> parts = splitPath(dirPath);
    int currentInode = 0;
    for (auto& part : parts) {
        currentInode = findInDir(file, sb, currentInode, part);
        if (currentInode == -1) {
            fclose(file);
            return "ERROR rep: No se encontró '" + dirPath + "'.";
        }
    }

    // Leer users.txt para obtener nombres
    std::string usersContent = readUsersFile(file, sb);

    std::stringstream rows;
    buildLSRows(file, sb, currentInode, usersContent, rows, dirPath.empty() ? "/" : dirPath);

    fclose(file);

    std::stringstream dot;
    dot << "digraph LS {\n";
    dot << "  node [shape=plaintext];\n";
    dot << "  ls [label=<\n";
    dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
    dot << "    <TR><TD COLSPAN=\"6\" BGCOLOR=\"#4FC3F7\"><B>LS REPORT</B></TD></TR>\n";
    dot << "    <TR><TD><B>Permisos</B></TD><TD><B>Owner</B></TD><TD><B>Grupo</B></TD>";
    dot << "<TD><B>Size</B></TD><TD><B>Fecha</B></TD><TD><B>Nombre</B></TD></TR>\n";
    dot << rows.str();
    dot << "    </TABLE>\n  >];\n}\n";

    if (!renderDot(dot.str(), outputPath))
        return "ERROR rep: No se pudo generar el reporte LS.";

    return "Reporte LS generado en '" + outputPath + "'.";
}

// ==================== REP command ====================
inline std::string cmd_rep(const std::map<std::string, std::string>& params) {
    if (params.find("name") == params.end())
        return "ERROR rep: Parámetro -name es obligatorio.";
    if (params.find("path") == params.end())
        return "ERROR rep: Parámetro -path es obligatorio.";
    if (params.find("id") == params.end())
        return "ERROR rep: Parámetro -id es obligatorio.";

    std::string name = toLower(params.at("name"));
    std::string outputPath = params.at("path");
    std::string id = params.at("id");

    MountedPart* mp = getMountedById(id);
    if (!mp)
        return "ERROR rep: No se encontró la partición montada con ID '" + id + "'.";

    std::string diskPath = std::string(mp->path);

    // Parametro opcional ruta (para reportes file y ls)
    // Acepta tanto -ruta como -path_file_ls
    std::string ruta;
    if (params.find("ruta") != params.end()) {
        ruta = params.at("ruta");
    } else if (params.find("path_file_ls") != params.end()) {
        ruta = params.at("path_file_ls");
    }

    if (name == "mbr") {
        return reportMBR(diskPath, outputPath);
    } else if (name == "disk") {
        return reportDISK(diskPath, outputPath);
    } else if (name == "inode") {
        return reportInode(mp, outputPath);
    } else if (name == "block") {
        return reportBlock(mp, outputPath);
    } else if (name == "bm_inode") {
        return reportBMInode(mp, outputPath);
    } else if (name == "bm_block") {
        return reportBMBlock(mp, outputPath);
    } else if (name == "tree") {
        return reportTree(mp, outputPath);
    } else if (name == "sb") {
        return reportSB(mp, outputPath);
    } else if (name == "file") {
        if (ruta.empty())
            return "ERROR rep: El reporte FILE necesita el parámetro -ruta.";
        return reportFile(mp, outputPath, ruta);
    } else if (name == "ls") {
        if (ruta.empty()) ruta = "/";
        return reportLS(mp, outputPath, ruta);
    }

    return "ERROR rep: Tipo de reporte '" + name + "' no reconocido.";
}

#endif // REPORTS_H
