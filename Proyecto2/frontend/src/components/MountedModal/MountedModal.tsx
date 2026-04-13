import React, { useEffect, useState } from 'react';
import type { MountedPartition } from '../../types';
import { getMountedPartitions } from '../../services/api';
import './MountedModal.css';

interface MountedModalProps {
  isOpen: boolean;
  onClose: () => void;
}

const MountedModal: React.FC<MountedModalProps> = ({ isOpen, onClose }) => {
  const [partitions, setPartitions] = useState<MountedPartition[]>([]);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    if (isOpen) {
      loadPartitions();
    }
  }, [isOpen]);

  const loadPartitions = async () => {
    setLoading(true);
    try {
      const data = await getMountedPartitions();
      setPartitions(data);
    } catch {
      setPartitions([]);
    }
    setLoading(false);
  };

  if (!isOpen) return null;

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-content" onClick={e => e.stopPropagation()}>
        <div className="modal-header">
          <h2>
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/>
            </svg>
            Particiones Montadas
          </h2>
          <button className="modal-close" onClick={onClose}>
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <line x1="18" y1="6" x2="6" y2="18" />
              <line x1="6" y1="6" x2="18" y2="18" />
            </svg>
          </button>
        </div>
        <div className="modal-body">
          {loading ? (
            <div className="modal-loading">
              <div className="spinner" />
              <p>Cargando particiones...</p>
            </div>
          ) : partitions.length === 0 ? (
            <div className="modal-empty">
              <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" opacity="0.3">
                <circle cx="12" cy="12" r="10" />
                <line x1="12" y1="8" x2="12" y2="12" />
                <line x1="12" y1="16" x2="12.01" y2="16" />
              </svg>
              <p>No hay particiones montadas</p>
              <span>Use el comando <code>mount</code> para montar una partición</span>
            </div>
          ) : (
            <table className="mounted-table">
              <thead>
                <tr>
                  <th>ID</th>
                  <th>Disco</th>
                  <th>Partición</th>
                  <th>Estado</th>
                </tr>
              </thead>
              <tbody>
                {partitions.map((p, i) => (
                  <tr key={i}>
                    <td><code>{p.id}</code></td>
                    <td>{p.diskPath}</td>
                    <td>{p.partitionName}</td>
                    <td>
                      <span className="status-badge mounted">Montada</span>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
        <div className="modal-footer">
          <button className="action-btn refresh-btn" onClick={loadPartitions}>
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <polyline points="23 4 23 10 17 10" />
              <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10" />
            </svg>
            Actualizar
          </button>
        </div>
      </div>
    </div>
  );
};

export default MountedModal;
