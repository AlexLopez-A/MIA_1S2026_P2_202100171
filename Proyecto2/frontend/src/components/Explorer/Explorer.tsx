import React, { useCallback, useEffect, useMemo, useState } from 'react';
import {
  executeCommand,
  getDiskPartitions,
  getDisks,
  getFsFileContent,
  getFsTree,
  getJournalTable,
  getMountedPartitions,
} from '../../services/api';
import type {
  DiskSummary,
  FileContentResponse,
  FsNode,
  JournalTableResponse,
  MountedPartition,
  PartitionSummary,
} from '../../types';
import './Explorer.css';

type ExplorerView = 'disks' | 'partitions' | 'explorer';

export default function Explorer() {
  const [disks, setDisks] = useState<DiskSummary[]>([]);
  const [mounted, setMounted] = useState<MountedPartition[]>([]);
  const [partitions, setPartitions] = useState<PartitionSummary[]>([]);
  const [selectedDiskPath, setSelectedDiskPath] = useState('');
  const [selectedPartition, setSelectedPartition] = useState<PartitionSummary | null>(null);
  const [treeRoot, setTreeRoot] = useState<FsNode | null>(null);
  const [selectedFile, setSelectedFile] = useState<FileContentResponse | null>(null);
  const [journalData, setJournalData] = useState<JournalTableResponse | null>(null);
  const [expandedNodes, setExpandedNodes] = useState<Record<string, boolean>>({ '/': true });
  const [password, setPassword] = useState('');
  const [isLogged, setIsLogged] = useState(false);
  const [visualLogged, setVisualLogged] = useState(false);
  const [currentView, setCurrentView] = useState<ExplorerView>('disks');
  const [activePartitionId, setActivePartitionId] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  const loginMount = useMemo(() => {
    const active = mounted.find((m) => m.status === 'logged');
    return active ?? mounted[0] ?? null;
  }, [mounted]);

  const loginPartitionId = loginMount?.id ?? '';
  const hasMountedPartition = mounted.length > 0;

  const loadPartitionsByDisk = useCallback(async (diskPath: string, selectedMountId = '') => {
    try {
      const response = await getDiskPartitions(diskPath);
      setPartitions(response.partitions);

      if (selectedMountId) {
        const byMount = response.partitions.find((p) => p.mountId === selectedMountId) || null;
        setSelectedPartition(byMount);
      } else {
        setSelectedPartition(null);
      }
    } catch {
      setPartitions([]);
      setSelectedPartition(null);
    }
  }, []);

  const loadPartitionData = useCallback(async (id: string) => {
    try {
      const [treeRes, journalRes] = await Promise.all([
        getFsTree(id, '/'),
        getJournalTable(id),
      ]);

      setTreeRoot(treeRes.root);
      setJournalData(journalRes);
      setSelectedFile(null);
      setExpandedNodes({ '/': true });
    } catch {
      setError('No se pudo cargar el árbol de archivos o el journaling de la partición seleccionada.');
    }
  }, []);

  const refreshAll = useCallback(async () => {
    setLoading(true);
    setError('');

    try {
      const [diskData, mountedData] = await Promise.all([getDisks(), getMountedPartitions()]);
      setDisks(diskData);
      setMounted(mountedData);

      const active = mountedData.find((d) => d.status === 'logged');
      if (active) {
        setIsLogged(true);
        setActivePartitionId(active.id);
      } else {
        setIsLogged(false);
        setActivePartitionId('');
      }

      if (selectedDiskPath) {
        const exists = diskData.some((disk) => disk.path === selectedDiskPath);
        if (!exists) {
          setSelectedDiskPath('');
          setPartitions([]);
          setSelectedPartition(null);
          setTreeRoot(null);
          setSelectedFile(null);
          setJournalData(null);
          setCurrentView('disks');
        } else {
          await loadPartitionsByDisk(selectedDiskPath, selectedPartition?.mountId || '');
        }
      }
    } catch {
      setError('No fue posible cargar la información visual. Verifique que el backend esté ejecutándose.');
    } finally {
      setLoading(false);
    }
  }, [loadPartitionsByDisk, selectedDiskPath, selectedPartition?.mountId]);

  useEffect(() => {
    void refreshAll();
  }, [refreshAll]);

  const handleSelectDisk = async (diskPath: string) => {
    setSelectedDiskPath(diskPath);
    setSelectedPartition(null);
    setTreeRoot(null);
    setSelectedFile(null);
    setJournalData(null);
    setError('');
    setCurrentView('partitions');
    await loadPartitionsByDisk(diskPath);
  };

  const handleSelectPartition = (partition: PartitionSummary) => {
    setSelectedPartition(partition);
    setSelectedFile(null);

    if (!partition.mountId) {
      setActivePartitionId('');
      setTreeRoot(null);
      setJournalData(null);
      setError('La partición seleccionada no está montada.');
      return;
    }

    setError('');
    setActivePartitionId(partition.mountId);
    setCurrentView('explorer');
    void loadPartitionData(partition.mountId);
  };

  const handleBackToDisks = () => {
    setCurrentView('disks');
    setSelectedDiskPath('');
    setPartitions([]);
    setSelectedPartition(null);
    setTreeRoot(null);
    setSelectedFile(null);
    setJournalData(null);
    setError('');
  };

  const handleBackToPartitions = () => {
    setCurrentView('partitions');
    setSelectedFile(null);
  };

  const handleLogin = async (e: React.FormEvent) => {
    e.preventDefault();

    if (!loginPartitionId) {
      setError('No hay una partición montada para iniciar sesión.');
      return;
    }

    if (!password.trim()) {
      setError('Ingrese la contraseña para iniciar sesión.');
      return;
    }

    if (loginMount?.status === 'logged') {
      setError('');
      setVisualLogged(true);
      setIsLogged(true);
      setCurrentView('disks');
      setActivePartitionId(loginPartitionId);
      setPassword('');
      return;
    }

    const escapedPass = password.replace(/"/g, '\\"');

    try {
      const res = await executeCommand(`login -user=root -pass="${escapedPass}" -id=${loginPartitionId}`);
      const rawMessage = `${res.message || ''}\n${res.output || ''}`;
      const alreadyLogged = /ya hay una sesi[oó]n activa/i.test(rawMessage);

      if ((!res.success || (res.output && res.output.startsWith('ERROR'))) && !alreadyLogged) {
        setError(res.message || 'Error al iniciar sesión.');
      } else {
        setError('');
        setVisualLogged(true);
        setIsLogged(true);
        setCurrentView('disks');
        setActivePartitionId(loginPartitionId);
        setPassword('');
        await refreshAll();
      }
    } catch {
      setError('Error de conexión al intentar iniciar sesión.');
    }
  };

  const toggleNode = (path: string) => {
    setExpandedNodes((prev) => ({ ...prev, [path]: !prev[path] }));
  };

  const handleOpenFile = async (path: string) => {
    if (!activePartitionId) return;
    try {
      const file = await getFsFileContent(activePartitionId, path);
      setSelectedFile(file);
    } catch {
      setError('No se pudo abrir el contenido del archivo seleccionado.');
    }
  };

  const renderTree = (node: FsNode, depth = 0): React.ReactNode => {
    const isDirectory = node.type === 'directory';
    const isExpanded = !!expandedNodes[node.path];

    return (
      <div key={node.path}>
        <button
          className={`tree-row ${isDirectory ? 'folder' : 'file'} ${selectedFile?.path === node.path ? 'active' : ''}`}
          style={{ paddingLeft: `${8 + depth * 16}px` }}
          onClick={() => {
            if (isDirectory) {
              toggleNode(node.path);
            } else {
              void handleOpenFile(node.path);
            }
          }}
        >
          <span className="tree-icon">{isDirectory ? (isExpanded ? '📂' : '📁') : '📄'}</span>
          <span className="tree-name">{node.name}</span>
          <span className="tree-perms">{node.permissions}</span>
        </button>

        {isDirectory && isExpanded && node.children?.map((child) => renderTree(child, depth + 1))}
      </div>
    );
  };

  const loggedMount = mounted.find((m) => m.status === 'logged');
  const selectedDisk = disks.find((disk) => disk.path === selectedDiskPath) || null;

  if (!visualLogged) {
    return (
      <div className="explorer-gate">
        <section className="gate-card">
          <h3>Iniciar Sesión</h3>
          <p>Primero ejecute su archivo de prueba en consola. Luego inicie sesión para desbloquear el visualizador.</p>

          <form className="login-form" onSubmit={handleLogin}>
            <label>
              ID partición
              <input type="text" value={loginPartitionId || 'Sin partición montada'} readOnly />
            </label>

            <label>
              Usuario
              <input type="text" value="root" readOnly />
            </label>

            <label>
              Contraseña
              <input
                type="password"
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                placeholder="Ingrese contraseña"
                disabled={!hasMountedPartition}
              />
            </label>

            <button type="submit" disabled={!hasMountedPartition || !password.trim()}>
              Iniciar sesión
            </button>
          </form>

          {!hasMountedPartition && (
            <div className="hint-box">
              No hay particiones montadas. Ejecute primero el script para montar y formatear la partición.
            </div>
          )}

          {loading && <div className="hint-box">Cargando estado del backend...</div>}
          {error && <div className="error-box">{error}</div>}
        </section>
      </div>
    );
  }

  return (
    <div className="explorer-shell navigator-shell">
      <section className="session-bar">
        <div className="session-summary">
          <strong>Sesión:</strong>{' '}
          {loggedMount ? `${loggedMount.currentUser || 'root'} | ${loggedMount.id}` : 'Sin sesión activa'}
        </div>
        <div className="session-actions">
          {currentView === 'partitions' && (
            <button className="nav-btn" onClick={handleBackToDisks}>Regresar a discos</button>
          )}
          {currentView === 'explorer' && (
            <>
              <button className="nav-btn" onClick={handleBackToPartitions}>Regresar a particiones</button>
              <button className="nav-btn" onClick={handleBackToDisks}>Ir a discos</button>
            </>
          )}
          <button className="refresh-session-btn" onClick={() => void refreshAll()}>
            Recargar
          </button>
        </div>
      </section>

      {loading && <div className="hint-box">Cargando datos del explorador...</div>}
      {error && <div className="error-box">{error}</div>}

      {currentView === 'disks' && (
        <section className="view-card centered-view">
          <h3>Paso 1: Discos</h3>
          <p>Seleccione un disco para avanzar a sus particiones.</p>

          <div className="card-grid">
            {disks.length === 0 ? (
              <div className="empty-state">No hay discos detectados todavía.</div>
            ) : (
              disks.map((disk) => (
                <button
                  key={disk.path}
                  className="disk-item"
                  onClick={() => void handleSelectDisk(disk.path)}
                >
                  <div className="item-title">{disk.fileName}</div>
                  <div className="item-meta">Capacidad: {disk.sizeHuman}</div>
                  <div className="item-meta">Fit: {disk.fit}</div>
                  <div className="item-meta">Particiones montadas: {disk.mountedCount}</div>
                </button>
              ))
            )}
          </div>
        </section>
      )}

      {currentView === 'partitions' && (
        <section className="view-card centered-view">
          <h3>Paso 2: Particiones</h3>
          <p>{selectedDisk ? `Disco seleccionado: ${selectedDisk.fileName}` : 'Seleccione una partición para abrir el explorador.'}</p>

          <div className="card-grid">
            {partitions.length === 0 ? (
              <div className="empty-state">Este disco no tiene particiones disponibles.</div>
            ) : (
              partitions.map((part) => (
                <button
                  key={`${part.type}-${part.name}-${part.start}`}
                  className={`partition-item ${selectedPartition?.name === part.name && selectedPartition?.start === part.start ? 'selected' : ''}`}
                  onClick={() => handleSelectPartition(part)}
                >
                  <div className="item-title">{part.name}</div>
                  <div className="item-meta">Tipo: {part.type}</div>
                  <div className="item-meta">Tamaño: {part.sizeHuman}</div>
                  <div className="item-meta">Fit: {part.fit}</div>
                  <div className={`status-pill ${part.status}`}>{part.status}</div>
                </button>
              ))
            )}
          </div>
        </section>
      )}

      {currentView === 'explorer' && (
        <section className="view-card explorer-view">
          <div className="explorer-view-header">
            <h3>Paso 3: File Explorer</h3>
            <span className="partition-chip">{selectedPartition?.name || 'Partición'}</span>
          </div>

          <div className="explorer-workspace">
            <section className="tree-card">
              <p>Árbol desde / con permisos de cada nodo.</p>
              <div className="tree-container">
                {!isLogged || !treeRoot ? (
                  <div className="empty-state">Cargando árbol de la partición seleccionada...</div>
                ) : (
                  renderTree(treeRoot)
                )}
              </div>
            </section>

            <section className="content-card">
              <h3>Paso 4: Contenido de Archivo</h3>
              {selectedFile ? (
                <>
                  <div className="file-meta">
                    <span>{selectedFile.path}</span>
                    <span>{selectedFile.sizeHuman}</span>
                    <span>{selectedFile.permissions}</span>
                  </div>
                  <pre className="file-content">{selectedFile.content}</pre>
                </>
              ) : (
                <div className="empty-state">Seleccione un archivo existente para ver su contenido.</div>
              )}
            </section>
          </div>

          <section className="journal-card">
            <h3>Visor de Journaling</h3>
            {!isLogged || !activePartitionId ? (
              <div className="empty-state">Inicie sesión para visualizar journaling.</div>
            ) : journalData?.entries.length ? (
              <div className="journal-table-wrapper">
                <table className="journal-table">
                  <thead>
                    <tr>
                      <th>#</th>
                      <th>Operación</th>
                      <th>Ruta</th>
                      <th>Contenido</th>
                      <th>Fecha</th>
                    </tr>
                  </thead>
                  <tbody>
                    {journalData.entries.map((entry) => (
                      <tr key={`${entry.index}-${entry.date}`}>
                        <td>{entry.index}</td>
                        <td>{entry.operation}</td>
                        <td>{entry.path}</td>
                        <td>{entry.content}</td>
                        <td>{entry.date}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            ) : (
              <div className="empty-state">{journalData?.message || 'Sin entradas de journaling para esta partición.'}</div>
            )}
          </section>
        </section>
      )}
    </div>
  );
}
