// Tipos para el simulador del sistema de archivos EXT2

export interface CommandResponse {
  success: boolean;
  message: string;
  output: string;
  reportPath?: string;
  reportDot?: string;
}

export interface MountedPartition {
  id: string;
  path: string;
  name: string;
  status: 'mounted' | 'logged';
  currentUser?: string;
}

export interface MountedPartitionRef {
  id: string;
  name: string;
  status: 'mounted' | 'logged';
}

export interface DiskSummary {
  path: string;
  fileName: string;
  sizeBytes: number;
  sizeHuman: string;
  fit: string;
  mountedCount: number;
  mountedPartitions: MountedPartitionRef[];
}

export interface PartitionSummary {
  name: string;
  type: 'primary' | 'extended' | 'logical';
  fit: string;
  sizeBytes: number;
  sizeHuman: string;
  start: number;
  status: 'unmounted' | 'mounted' | 'logged';
  mountId: string;
}

export interface DiskPartitionsResponse {
  disk: {
    path: string;
    fileName: string;
    sizeBytes: number;
    sizeHuman: string;
    fit: string;
  };
  partitions: PartitionSummary[];
}

export interface FsNode {
  name: string;
  path: string;
  type: 'directory' | 'file';
  permissions: string;
  sizeBytes?: number;
  sizeHuman?: string;
  children?: FsNode[];
}

export interface FileTreeResponse {
  partitionId: string;
  root: FsNode;
  truncated: boolean;
}

export interface FileContentResponse {
  partitionId: string;
  path: string;
  name: string;
  sizeBytes: number;
  sizeHuman: string;
  permissions: string;
  content: string;
}

export interface JournalEntry {
  index: number;
  operation: string;
  path: string;
  content: string;
  date: string;
}

export interface JournalTableResponse {
  partitionId: string;
  filesystemType: number;
  message: string;
  entries: JournalEntry[];
}

export interface ConsoleEntry {
  id: number;
  type: 'command' | 'output' | 'error' | 'comment' | 'info';
  content: string;
  timestamp: Date;
}

export interface ReportData {
  id: string;
  name: string;
  type: string;
  dot?: string;
  svg?: string;
  imagePath?: string;
}

export interface GeneratedReport {
  path: string;
  fileName: string;
  extension: string;
  createdAt: Date;
}
