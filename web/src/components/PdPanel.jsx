import { useEffect, useState } from 'react';
import { apiGet, apiPost } from '../hooks/useApi';
import { useRole } from '../hooks/useAuth';

export default function PdPanel() {
  const [rows, setRows] = useState([]);
  const [error, setError] = useState(null);
  const canEdit = useRole('Developer');

  const load = async () => {
    try {
      setRows(await apiGet('/api/pd/status'));
    } catch (err) {
      setError(err.message);
    }
  };

  useEffect(() => {
    load();
    const id = setInterval(load, 4000);
    return () => clearInterval(id);
  }, []);

  const toggle = async (comId, enabled) => {
    await apiPost(`/api/pd/${comId}/enable`, { enabled });
    load();
  };

  return (
    <div className="card">
      <div className="flex-between">
        <h2>PD Dashboard</h2>
        {error && <span className="badge danger">{error}</span>}
      </div>
      <div className="table-scroll">
        <table>
          <thead>
            <tr>
              <th>Name</th>
              <th>Direction</th>
              <th>Status</th>
              <th>Stats</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((row) => (
              <tr key={row.comId}>
                <td>{row.name}</td>
                <td>
                  <span className={`badge ${row.direction === 'PUBLISH' ? 'success' : 'info'}`}>
                    {row.direction}
                  </span>
                </td>
                <td>
                  <span className={`badge ${row.enabled ? 'success' : 'warn'}`}>
                    {row.locked ? 'Locked' : row.enabled ? 'Active' : 'Disabled'}
                  </span>
                </td>
                <td>
                  <div className="status-row" style={{ color: 'var(--muted)', fontSize: '0.85rem' }}>
                    <span>TX {row.stats.txCount}</span>
                    <span>RX {row.stats.rxCount}</span>
                    <span>Timeouts {row.stats.timeoutCount}</span>
                  </div>
                </td>
                <td>
                  <div className="button-row">
                    <button
                      className="touch-target"
                      onClick={() => toggle(row.comId, !row.enabled)}
                      disabled={!canEdit}
                    >
                      {row.enabled ? 'Disable' : 'Enable'}
                    </button>
                    <span className={`badge ${row.redundantActive ? 'success' : 'warn'}`}>
                      {row.redundantActive ? `Channel ${row.activeChannel}` : 'Standby'}
                    </span>
                  </div>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
