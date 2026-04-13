import React, { useEffect, useMemo, useRef, useState } from 'react';
import type { GeneratedReport } from '../../types';
import { getReportFile } from '../../services/api';
import './ReportViewer.css';

interface ReportViewerProps {
  isOpen: boolean;
  reports: GeneratedReport[];
  onClose: () => void;
}

type PreviewMode = 'empty' | 'loading' | 'text' | 'image' | 'pdf' | 'error';

const ReportViewer: React.FC<ReportViewerProps> = ({ isOpen, reports, onClose }) => {
  const [selectedPath, setSelectedPath] = useState('');
  const [mode, setMode] = useState<PreviewMode>('empty');
  const [textContent, setTextContent] = useState('');
  const [resourceUrl, setResourceUrl] = useState('');
  const [mimeType, setMimeType] = useState('');
  const [errorMessage, setErrorMessage] = useState('');
  const [rawData, setRawData] = useState<ArrayBuffer | null>(null);
  const activeObjectUrlRef = useRef('');

  const sortedReports = useMemo(() => reports, [reports]);

  useEffect(() => {
    if (!isOpen) {
      return;
    }

    if (sortedReports.length === 0) {
      setSelectedPath('');
      setMode('empty');
      return;
    }

    if (!selectedPath || !sortedReports.some((report) => report.path === selectedPath)) {
      setSelectedPath(sortedReports[0].path);
    }
  }, [isOpen, sortedReports, selectedPath]);

  useEffect(() => {
    if (!isOpen || !selectedPath) {
      return;
    }

    let isCancelled = false;

    const clearObjectUrl = () => {
      if (activeObjectUrlRef.current) {
        URL.revokeObjectURL(activeObjectUrlRef.current);
        activeObjectUrlRef.current = '';
      }
    };

    const loadPreview = async () => {
      setMode('loading');
      setErrorMessage('');
      setTextContent('');
      setRawData(null);
      setMimeType('');
      setResourceUrl('');
      clearObjectUrl();

      const response = await getReportFile(selectedPath);
      if (!response) {
        if (!isCancelled) {
          setMode('error');
          setErrorMessage('No se pudo cargar el reporte desde el backend.');
        }
        return;
      }

      if (isCancelled) {
        return;
      }

      const lowerMimeType = response.contentType.toLowerCase();
      setMimeType(lowerMimeType);
      setRawData(response.data);

      if (lowerMimeType.startsWith('image/')) {
        const url = URL.createObjectURL(new Blob([response.data], { type: lowerMimeType }));
        activeObjectUrlRef.current = url;
        setResourceUrl(url);
        setMode('image');
        return;
      }

      if (lowerMimeType.includes('application/pdf')) {
        const url = URL.createObjectURL(new Blob([response.data], { type: 'application/pdf' }));
        activeObjectUrlRef.current = url;
        setResourceUrl(url);
        setMode('pdf');
        return;
      }

      const decodedText = new TextDecoder('utf-8').decode(new Uint8Array(response.data));
      setTextContent(decodedText);
      setMode('text');
    };

    loadPreview();

    return () => {
      isCancelled = true;
      clearObjectUrl();
    };
  }, [isOpen, selectedPath]);

  const selectedReport = sortedReports.find((report) => report.path === selectedPath) || null;

  const handleDownload = () => {
    if (!selectedReport || !rawData) {
      return;
    }

    const blob = new Blob([rawData], { type: mimeType || 'application/octet-stream' });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = selectedReport.fileName;
    anchor.click();
    URL.revokeObjectURL(url);
  };

  if (!isOpen) return null;

  return (
    <div className="report-viewer-overlay" onClick={onClose}>
      <div className="report-viewer-content" onClick={(e) => e.stopPropagation()}>
        <div className="report-header">
          <h3>
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
              <polyline points="14 2 14 8 20 8" />
            </svg>
            Reportes Generados
          </h3>
          <div className="report-actions">
            <button className="action-btn" onClick={handleDownload} disabled={!rawData || !selectedReport}>
              Descargar
            </button>
            <button className="modal-close" onClick={onClose}>
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                <line x1="18" y1="6" x2="6" y2="18" />
                <line x1="6" y1="6" x2="18" y2="18" />
              </svg>
            </button>
          </div>
        </div>

        <div className="report-layout">
          <aside className="report-list">
            {sortedReports.length === 0 ? (
              <div className="report-empty-list">Aun no hay reportes detectados.</div>
            ) : (
              sortedReports.map((report) => (
                <button
                  key={`${report.path}-${report.createdAt.toISOString()}`}
                  className={`report-item ${report.path === selectedPath ? 'selected' : ''}`}
                  onClick={() => setSelectedPath(report.path)}
                >
                  <span className="report-item-name">{report.fileName}</span>
                  <span className="report-item-ext">.{report.extension || 'file'}</span>
                </button>
              ))
            )}
          </aside>

          <section className="report-preview">
            {mode === 'empty' && <div className="report-state">Seleccione un reporte para visualizarlo.</div>}
            {mode === 'loading' && <div className="report-state">Cargando reporte...</div>}
            {mode === 'error' && <div className="report-state report-error">{errorMessage}</div>}

            {mode === 'text' && (
              <pre className="report-text-preview">{textContent || 'Archivo de texto vacio.'}</pre>
            )}

            {mode === 'image' && resourceUrl && (
              <div className="report-image-wrapper">
                <img src={resourceUrl} alt={selectedReport?.fileName || 'Reporte'} className="report-image-preview" />
              </div>
            )}

            {mode === 'pdf' && resourceUrl && (
              <iframe src={resourceUrl} title={selectedReport?.fileName || 'Reporte PDF'} className="report-pdf-preview" />
            )}
          </section>
        </div>
      </div>
    </div>
  );
};

export default ReportViewer;
