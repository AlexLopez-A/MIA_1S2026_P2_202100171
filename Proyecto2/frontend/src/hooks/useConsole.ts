import { useState, useCallback, useRef } from 'react';
import type { ConsoleEntry, CommandResponse } from '../types';
import { executeCommand } from '../services/api';

let entryIdCounter = 0;

export function useConsole() {
  const [entries, setEntries] = useState<ConsoleEntry[]>([]);
  const [isExecuting, setIsExecuting] = useState(false);
  const entriesRef = useRef<ConsoleEntry[]>([]);

  const addEntry = useCallback((type: ConsoleEntry['type'], content: string) => {
    const entry: ConsoleEntry = {
      id: ++entryIdCounter,
      type,
      content,
      timestamp: new Date(),
    };
    entriesRef.current = [...entriesRef.current, entry];
    setEntries([...entriesRef.current]);
  }, []);

  const clearConsole = useCallback(() => {
    entriesRef.current = [];
    setEntries([]);
  }, []);

  const processCommandLine = useCallback(async (line: string): Promise<CommandResponse | null> => {
    const trimmed = line.trim();

    // Linea vacia
    if (!trimmed) return null;

    // Linea de comentario
    if (trimmed.startsWith('#')) {
      addEntry('comment', trimmed);
      return null;
    }

    // Es un comando
    addEntry('command', trimmed);

    try {
      const response = await executeCommand(trimmed);
      if (response.success) {
        if (response.output) {
          addEntry('output', response.output);
        }
        if (response.message) {
          addEntry('info', response.message);
        }
      } else {
        addEntry('error', response.message || 'Error ejecutando el comando');
        if (response.output) {
          addEntry('output', response.output);
        }
      }
      return response;
    } catch (err: any) {
      addEntry('error', `Error: ${err.message}`);
      return null;
    }
  }, [addEntry]);

  const executeInput = useCallback(async (input: string) => {
    if (isExecuting) return;
    setIsExecuting(true);

    const lines = input.split('\n');
    const results: CommandResponse[] = [];

    for (const line of lines) {
      const result = await processCommandLine(line);
      if (result) {
        results.push(result);
      }
    }

    setIsExecuting(false);
    return results;
  }, [isExecuting, processCommandLine]);

  return {
    entries,
    isExecuting,
    executeInput,
    addEntry,
    clearConsole,
  };
}
