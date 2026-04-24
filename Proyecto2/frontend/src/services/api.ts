import axios from 'axios';
import type {
  CommandResponse,
  DiskPartitionsResponse,
  DiskSummary,
  FileContentResponse,
  FileTreeResponse,
  JournalTableResponse,
  MountedPartition,
} from '../types';

const API_BASE_URL = 'http://3.139.63.218:8080/api';

export const executeCommand = async (command: string) => {
  const response = await axios.post<CommandResponse>(`${API_BASE_URL}/execute`, { command });
  return response.data;
};

export const executeScript = async (script: string) => {
  const response = await axios.post(`${API_BASE_URL}/execute-script`, { script });
  return response.data;
};

export const getMountedPartitions = async () => {
  const response = await axios.get<MountedPartition[]>(`${API_BASE_URL}/mounted`);
  return response.data;
};

export const getDisks = async () => {
  const response = await axios.get<DiskSummary[]>(`${API_BASE_URL}/disks`);
  return response.data;
};

export const getDiskPartitions = async (path: string) => {
  const response = await axios.get<DiskPartitionsResponse>(`${API_BASE_URL}/disks/partitions`, { params: { path } });
  return response.data;
};

export const getFsTree = async (id: string, path: string = '/') => {
  const response = await axios.get<FileTreeResponse>(`${API_BASE_URL}/fs/tree`, { params: { id, path } });
  return response.data;
};

export const getFsFileContent = async (id: string, path: string) => {
  const response = await axios.get<FileContentResponse>(`${API_BASE_URL}/fs/file-content`, { params: { id, path } });
  return response.data;
};

export const getFileTree = async (path: string = '/') => {
  const response = await axios.get(`${API_BASE_URL}/tree`, { params: { path } });
  return response.data;
};

export const getJournal = async (id: string = '') => {
  const response = await axios.get(`${API_BASE_URL}/journal`, { params: { id } });
  return response.data;
};

export const getJournalTable = async (id: string) => {
  const response = await axios.get<JournalTableResponse>(`${API_BASE_URL}/journal-table`, { params: { id } });
  return response.data;
};

export const getReportContent = async (path: string) => {
  const response = await axios.get<ArrayBuffer>(`${API_BASE_URL}/report`, {
    params: { path },
    responseType: 'arraybuffer',
  });

  return {
    data: response.data,
    contentType: response.headers['content-type'] || 'application/octet-stream',
  };
};

export const getReportFile = getReportContent;
