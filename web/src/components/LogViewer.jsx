import { useEffect, useState } from 'react';
import { apiGet } from '../hooks/useApi';

export default function LogViewer() {
  const [logs, setLogs] = useState([]);
  const [error, setError] = useState(null);

  const load = async () => {
    try {
      setLogs(await apiGet('/api/diag/log/export?max=200'));
      setError(null);
    } catch (err) {
      setError(err.message);
    }
  };

  useEffect(() => {
    load();
    const id = setInterval(load, 7000);
    return () => clearInterval(id);
  }, []);

  return (
    <div className="card">
      <div className="flex-between">
        <h2>Log Viewer</h2>
        {error && <span className="badge danger">{error}</span>}
      </div>
      <div className="logs" style={{ maxHeight: 240 }}>{logs}</div>
    </div>
  );
}
