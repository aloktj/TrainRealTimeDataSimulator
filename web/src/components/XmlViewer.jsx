import { useEffect, useState } from 'react';
import { apiGet } from '../hooks/useApi';

export default function XmlViewer() {
  const [config, setConfig] = useState(null);
  const [error, setError] = useState(null);

  const load = async () => {
    try {
      setConfig(await apiGet('/api/config/detail'));
      setError(null);
    } catch (err) {
      setError(err.message);
    }
  };

  useEffect(() => {
    load();
  }, []);

  return (
    <div className="card">
      <div className="flex-between">
        <h2>XML / Config Visual</h2>
        {error && <span className="badge danger">{error}</span>}
      </div>
      {config ? (
        <div className="code-block" style={{ maxHeight: 260 }}>
          {JSON.stringify(config, null, 2)}
        </div>
      ) : (
        <p style={{ color: 'var(--muted)' }}>Loading configuration…</p>
      )}
    </div>
  );
}
