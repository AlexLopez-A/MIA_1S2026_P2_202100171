import { useState, useCallback } from 'react';
import Header from './components/Header/Header';
import CommandInput from './components/CommandInput/CommandInput';
import ConsoleOutput from './components/ConsoleOutput/ConsoleOutput';
import MountedModal from './components/MountedModal/MountedModal';
import ReportViewer from './components/ReportViewer/ReportViewer';
import Explorer from './components/Explorer/Explorer';
import { useConsole } from './hooks/useConsole';
import type { GeneratedReport } from './types';
import './App.css';

const REPORT_PATH_REGEX = /generado en ['\"]([^'\"]+)['\"]/i;

const getFileExtension = (path: string): string => {
  const parts = path.split('.');
  return parts.length > 1 ? parts[parts.length - 1].toLowerCase() : '';
};

const buildReportEntry = (path: string): GeneratedReport => ({
  path,
  fileName: path.split('/').pop() || path,
  extension: getFileExtension(path),
  createdAt: new Date(),
});

function App() {
  const { entries, isExecuting, executeInput, addEntry, clearConsole } = useConsole();
  const [showMounted, setShowMounted] = useState(false);
  const [showReports, setShowReports] = useState(false);
  const [generatedReports, setGeneratedReports] = useState<GeneratedReport[]>([]);
  const [showExplorer, setShowExplorer] = useState(false);

  const handleExecute = useCallback(async (input: string) => {
    const results = await executeInput(input);

    if (!results || results.length === 0) {
      return;
    }

    const detectedPaths = new Set<string>();

    for (const result of results) {
      const possibleOutputs = [result.message, result.output].filter(Boolean) as string[];
      for (const text of possibleOutputs) {
        const match = text.match(REPORT_PATH_REGEX);
        if (match?.[1]) {
          detectedPaths.add(match[1]);
        }
      }
    }

    if (detectedPaths.size > 0) {
      setGeneratedReports((prev) => {
        const next = [...prev];
        for (const path of detectedPaths) {
          if (!next.some((report) => report.path === path)) {
            next.unshift(buildReportEntry(path));
          }
        }
        return next;
      });

      addEntry('info', `Se detectaron ${detectedPaths.size} reporte(s). Abra la opcion Reportes para visualizarlos.`);
    }
  }, [executeInput, addEntry]);

  const handleLoadScript = useCallback((content: string) => {
    addEntry('info', `Script cargado correctamente (${content.split('\n').length} lineas)`);
  }, [addEntry]);

  return (
    <div className="app">
      <Header
        onShowMounted={() => setShowMounted(true)}
        onShowReports={() => setShowReports(true)}
        reportsCount={generatedReports.length}
      />
      
      <div style={{padding: '5px', background: '#242424', borderBottom: '1px solid #444', display: 'flex', justifyContent: 'flex-start'}}>
         <button 
            style={{padding: '6px 12px', cursor: 'pointer', background: showExplorer ? '#3b82f6' : '#1a1a1a', color: 'white', border: '1px solid #444', borderRadius: '4px'}}
            onClick={() => setShowExplorer(!showExplorer)}
         >
            {showExplorer ? "Volver a Consola" : "Abrir Explorador Web (Visual)"}
         </button>
      </div>

      <main className="main-content">
        {showExplorer ? (
           <Explorer />
        ) : (
          <div className="workspace">
            <div className="panel left-panel">
              <CommandInput
                onExecute={handleExecute}
                onLoadScript={handleLoadScript}
                isExecuting={isExecuting}
              />
            </div>
            <div className="panel-divider" />
            <div className="panel right-panel">
              <ConsoleOutput
                entries={entries}
                onClear={clearConsole}
              />
            </div>
          </div>
        )}
      </main>

      <footer className="app-footer">
        <span>EXT2 File System Simulator - Manejo e Implementacion de Archivos</span>
        <span className="footer-status">
          <span className={`status-dot ${isExecuting ? 'active' : 'idle'}`} />
          {isExecuting ? 'Ejecutando...' : 'Listo'}
        </span>
      </footer>

      <MountedModal
        isOpen={showMounted}
        onClose={() => setShowMounted(false)}
      />

      <ReportViewer
        isOpen={showReports}
        reports={generatedReports}
        onClose={() => setShowReports(false)}
      />
    </div>
  );
}

export default App;
