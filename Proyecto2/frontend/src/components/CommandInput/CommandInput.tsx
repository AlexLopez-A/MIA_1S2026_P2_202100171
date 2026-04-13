import React, { useRef, useEffect, useState, useCallback } from 'react';
import './CommandInput.css';

interface CommandInputProps {
  onExecute: (input: string) => void;
  onLoadScript: (content: string) => void;
  isExecuting: boolean;
}

const CommandInput: React.FC<CommandInputProps> = ({ onExecute, onLoadScript, isExecuting }) => {
  const [input, setInput] = useState('');
  const [lineCount, setLineCount] = useState(1);
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const lineNumbersRef = useRef<HTMLDivElement>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const updateLineNumbers = useCallback(() => {
    const lines = input.split('\n').length;
    setLineCount(lines);
  }, [input]);

  useEffect(() => {
    updateLineNumbers();
  }, [input, updateLineNumbers]);

  const handleScroll = () => {
    if (textareaRef.current && lineNumbersRef.current) {
      lineNumbersRef.current.scrollTop = textareaRef.current.scrollTop;
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && e.ctrlKey) {
      e.preventDefault();
      handleExecute();
    }
    if (e.key === 'Tab') {
      e.preventDefault();
      const textarea = textareaRef.current;
      if (textarea) {
        const start = textarea.selectionStart;
        const end = textarea.selectionEnd;
        const newValue = input.substring(0, start) + '  ' + input.substring(end);
        setInput(newValue);
        setTimeout(() => {
          textarea.selectionStart = textarea.selectionEnd = start + 2;
        }, 0);
      }
    }
  };

  const handleExecute = () => {
    if (!input.trim() || isExecuting) return;
    onExecute(input);
  };

  const handleLoadScript = () => {
    fileInputRef.current?.click();
  };

  const handleFileChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;

    if (!file.name.endsWith('.smia')) {
      alert('Solo se permiten archivos con extensión .smia');
      return;
    }

    const reader = new FileReader();
    reader.onload = (ev) => {
      const content = ev.target?.result as string;
      setInput(content);
      onLoadScript(content);
    };
    reader.readAsText(file);
    e.target.value = '';
  };

  const handleClear = () => {
    setInput('');
    textareaRef.current?.focus();
  };

  const lineNumbers = Array.from({ length: lineCount }, (_, i) => i + 1);

  return (
    <div className="command-input-panel">
      <div className="panel-header">
        <div className="panel-title">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
            <polyline points="4 17 10 11 4 5" />
            <line x1="12" y1="19" x2="20" y2="19" />
          </svg>
          Entrada de Comandos
        </div>
        <div className="panel-actions">
          <input
            ref={fileInputRef}
            type="file"
            accept=".smia"
            onChange={handleFileChange}
            style={{ display: 'none' }}
          />
          <button className="action-btn load-btn" onClick={handleLoadScript} title="Cargar script .smia">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
              <polyline points="17 8 12 3 7 8" />
              <line x1="12" y1="3" x2="12" y2="15" />
            </svg>
            Cargar Script
          </button>
          <button className="action-btn clear-btn" onClick={handleClear} title="Limpiar entrada">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <polyline points="3 6 5 6 21 6" />
              <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />
            </svg>
            Limpiar
          </button>
          <button
            className={`action-btn execute-btn ${isExecuting ? 'executing' : ''}`}
            onClick={handleExecute}
            disabled={isExecuting || !input.trim()}
            title="Ejecutar (Ctrl+Enter)"
          >
            {isExecuting ? (
              <>
                <div className="spinner" />
                Ejecutando...
              </>
            ) : (
              <>
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                  <polygon points="5 3 19 12 5 21 5 3" />
                </svg>
                Ejecutar
              </>
            )}
          </button>
        </div>
      </div>
      <div className="editor-container">
        <div className="line-numbers" ref={lineNumbersRef}>
          {lineNumbers.map(num => (
            <div key={num} className="line-number">{num}</div>
          ))}
        </div>
        <textarea
          ref={textareaRef}
          className="command-textarea"
          value={input}
          onChange={(e) => setInput(e.target.value)}
          onScroll={handleScroll}
          onKeyDown={handleKeyDown}
          placeholder={"Escriba sus comandos aquí...\nEjemplo: mkdisk -size=100 -unit=M -path=/home/user/disco1.mia\n\n# Los comentarios inician con #\n# Presione Ctrl+Enter para ejecutar"}
          spellCheck={false}
        />
      </div>
      <div className="editor-footer">
        <span className="footer-info">Líneas: {lineCount}</span>
        <span className="footer-info">Ctrl+Enter para ejecutar</span>
      </div>
    </div>
  );
};

export default CommandInput;
