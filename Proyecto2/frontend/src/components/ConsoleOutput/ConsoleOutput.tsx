import React, { useRef, useEffect } from 'react';
import type { ConsoleEntry } from '../../types';
import './ConsoleOutput.css';

interface ConsoleOutputProps {
  entries: ConsoleEntry[];
  onClear: () => void;
}

const ConsoleOutput: React.FC<ConsoleOutputProps> = ({ entries, onClear }) => {
  const bottomRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [entries]);

  const getEntryIcon = (type: ConsoleEntry['type']) => {
    switch (type) {
      case 'command':
        return <span className="entry-prefix command-prefix">{'>'}</span>;
      case 'output':
        return <span className="entry-prefix output-prefix">{'→'}</span>;
      case 'error':
        return <span className="entry-prefix error-prefix">{'✕'}</span>;
      case 'comment':
        return <span className="entry-prefix comment-prefix">{'#'}</span>;
      case 'info':
        return <span className="entry-prefix info-prefix">{'✓'}</span>;
      default:
        return null;
    }
  };

  const formatTime = (date: Date) => {
    return date.toLocaleTimeString('es-GT', {
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    });
  };

  return (
    <div className="console-output-panel">
      <div className="panel-header">
        <div className="panel-title">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
            <rect x="2" y="3" width="20" height="18" rx="2" />
            <path d="M8 10l4 4-4 4" />
          </svg>
          Salida de Consola
          {entries.length > 0 && (
            <span className="entry-count">{entries.length}</span>
          )}
        </div>
        <div className="panel-actions">
          <button
            className="action-btn clear-btn"
            onClick={onClear}
            disabled={entries.length === 0}
            title="Limpiar consola"
          >
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <polyline points="3 6 5 6 21 6" />
              <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />
            </svg>
            Limpiar
          </button>
        </div>
      </div>
      <div className="console-body">
        {entries.length === 0 ? (
          <div className="console-empty">
            <svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" opacity="0.3">
              <rect x="2" y="3" width="20" height="18" rx="2" />
              <path d="M8 10l4 4-4 4" />
            </svg>
            <p>La salida de los comandos aparecerá aquí</p>
          </div>
        ) : (
          entries.map(entry => (
            <div key={entry.id} className={`console-entry entry-${entry.type}`}>
              <span className="entry-time">{formatTime(entry.timestamp)}</span>
              {getEntryIcon(entry.type)}
              <pre className="entry-content">{entry.content}</pre>
            </div>
          ))
        )}
        <div ref={bottomRef} />
      </div>
    </div>
  );
};

export default ConsoleOutput;
