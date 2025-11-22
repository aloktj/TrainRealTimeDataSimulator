import { useEffect, useState } from 'react';
import { apiGet } from '../hooks/useApi';

export default function OverviewPanel() {
  const [snapshot, setSnapshot] = useState(null);
  const [error, setError] = useState(null);

  const load = async () => {
    try {
      setSnapshot(await apiGet('/api/ui/overview'));
      setError(null);
    } catch (err) {
      setError(err.message);
    }
  };

  useEffect(() => {
    load();
    const id = setInterval(load, 5000);
    return () => clearInterval(id);
  }, []);

  return (
    <div className="card">
      <div className="flex-between">
        <h2>Operations Dashboard</h2>
        {error && <span className="badge danger">{error}</span>}
      </div>
      {snapshot ? (
        <div className="grid-halves">
          <div>
            <h3>Configuration</h3>
            <div className="status-row">
              <span className="badge info">{snapshot.config.hostName}</span>
              <span className="badge warn">DataSets {snapshot.config.dataSets}</span>
              <span className="badge success">PD {snapshot.config.pdTelegrams}</span>
              <span className="badge success">MD {snapshot.config.mdTelegrams}</span>
            </div>
          </div>
          <div>
            <h3>Threads</h3>
            <div className="status-row">
              {Object.entries(snapshot.metrics.threads || {}).map(([k, v]) => (
                <span key={k} className={`badge ${v ? 'success' : 'danger'}`}>
                  {k}: {v ? 'On' : 'Off'}
                </span>
              ))}
            </div>
          </div>
          <div>
            <h3>Recent Events</h3>
            <div className="logs" style={{ maxHeight: 140 }}>
              {(snapshot.events || []).map((ev) => (
                <div key={ev.timestampMs}>
                  [{ev.severity}] {ev.component}: {ev.message}
                </div>
              ))}
            </div>
          </div>
          <div>
            <h3>PD Active</h3>
            <div className="status-row">
              {(snapshot.pd || []).slice(0, 4).map((pd) => (
                <span key={pd.comId} className={`badge ${pd.enabled ? 'success' : 'warn'}`}>
                  {pd.name}
                </span>
              ))}
            </div>
          </div>
        </div>
      ) : (
        <p style={{ color: 'var(--muted)' }}>Loading snapshot…</p>
      )}
    </div>
  );
}
