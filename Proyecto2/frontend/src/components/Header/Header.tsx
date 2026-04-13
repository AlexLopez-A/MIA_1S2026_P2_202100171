import React from 'react';
import './Header.css';

interface HeaderProps {
  onShowMounted: () => void;
  onShowReports: () => void;
  reportsCount: number;
}

const Header: React.FC<HeaderProps> = ({ onShowMounted, onShowReports, reportsCount }) => {
  return (
    <header className="header">
      <div className="header-left">
        <div className="header-logo">
          <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
            <rect x="2" y="2" width="20" height="20" rx="2" />
            <path d="M7 8h10M7 12h10M7 16h6" />
          </svg>
        </div>
        <div className="header-title">
          <h1>EXT2 File System Simulator</h1>
          <span className="header-subtitle">Manejo e Implementación de Archivos</span>
        </div>
      </div>
      <div className="header-right">
        <button className="header-btn header-btn-secondary" onClick={onShowReports} title="Ver reportes generados">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
            <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
            <polyline points="14 2 14 8 20 8" />
          </svg>
          Reportes ({reportsCount})
        </button>
        <button className="header-btn" onClick={onShowMounted} title="Ver particiones montadas">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
            <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/>
          </svg>
          Particiones
        </button>
      </div>
    </header>
  );
};

export default Header;
