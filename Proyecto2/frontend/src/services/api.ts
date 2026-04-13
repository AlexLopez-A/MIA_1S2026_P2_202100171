import axios from 'axios';
import type { CommandResponse, MountedPartition } from '../types';

const API_BASE_URL = import.meta.env.VITE_API_URL || 'http://localhost:8080';

const api = axios.create({
  baseURL: API_BASE_URL,
  headers: {
    'Content-Type': 'application/json',
  },
});

export const executeCommand = async (command: string): Promise<CommandResponse> => {
  try {
    const response = await api.post<CommandResponse>('/api/execute', { command });
    return response.data;
  } catch (error: any) {
    if (error.response) {
      return {
        success: false,
        message: error.response.data?.message || 'Error del servidor',
        output: error.response.data?.output || '',
      };
    }
    return {
      success: false,
      message: 'Error de conexión con el backend. Asegúrese de que el servidor esté corriendo.',
      output: '',
    };
  }
};

export const executeScript = async (script: string): Promise<CommandResponse[]> => {
  try {
    const response = await api.post<CommandResponse[]>('/api/execute-script', { script });
    return response.data;
  } catch (error: any) {
    if (error.response) {
      return [{
        success: false,
        message: error.response.data?.message || 'Error del servidor',
        output: error.response.data?.output || '',
      }];
    }
    return [{
      success: false,
      message: 'Error de conexión con el backend.',
      output: '',
    }];
  }
};

export const getMountedPartitions = async (): Promise<MountedPartition[]> => {
  try {
    const response = await api.get<MountedPartition[]>('/api/mounted');
    return response.data;
  } catch {
    return [];
  }
};

export const getReport = async (reportPath: string): Promise<string> => {
  try {
    const response = await api.get('/api/report', { params: { path: reportPath } });
    return response.data;
  } catch {
    return '';
  }
};

export interface ReportFileResponse {
  data: ArrayBuffer;
  contentType: string;
}

export const getReportFile = async (reportPath: string): Promise<ReportFileResponse | null> => {
  try {
    const response = await api.get<ArrayBuffer>('/api/report', {
      params: { path: reportPath },
      responseType: 'arraybuffer',
    });

    const contentType = response.headers['content-type'] || 'application/octet-stream';

    return {
      data: response.data,
      contentType,
    };
  } catch {
    return null;
  }
};

export default api;
