import { useEffect, useState } from 'react';
import { apiGet, apiPost } from '../hooks/useApi';
import { useRole } from '../hooks/useAuth';

export default function InterfaceDiagnostics() {
  const [status, setStatus] = useState(null);
  const [error, setError] = useState(null);
  const canDev = useRole('Developer');

  const load = async () => {
    try {
      setStatus(await apiGet('/api/network/multicast'));
      setError(null);
    } catch (err) {
      setError(err.message);
    }
  };

  useEffect(() => {
    load();
  }, []);

  const toggleGroup = async (iface, group, nic, join) => {
    const path = join ? '/api/network/multicast/join' : '/api/network/multicast/leave';
    await apiPost(path, { interface: iface, group, nic });
    load();
  };

  return (
    <div className="card">
      <div className="flex-between">
        <h2>Interface Diagnostics</h2>
        {error && <span className="badge danger">{error}</span>}
      </div>
      {status ? (
        <div className="table-scroll">
          <table>
            <thead>
              <tr>
                <th>Interface</th>
                <th>Groups</th>
                <th>Actions</th>
              </tr>
            </thead>
            <tbody>
              {status.map((row) => (
                <tr key={row.name}>
                  <td>{row.name}</td>
                  <td>
                    <div className="status-row">
                      {row.groups.map((g) => (
                        <span key={g.group} className="badge info">
                          {g.group}
                        </span>
                      ))}
                    </div>
                  </td>
                  <td>
                    <div className="button-row">
                      <button
                        onClick={() => toggleGroup(row.name, '239.0.0.1', row.nic, true)}
                        disabled={!canDev}
                        className="touch-target"
                      >
                        Join sample
                      </button>
                      <button
                        onClick={() => toggleGroup(row.name, '239.0.0.1', row.nic, false)}
                        disabled={!canDev}
                        className="touch-target"
                      >
                        Leave sample
                      </button>
                    </div>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      ) : (
        <p style={{ color: 'var(--muted)' }}>Loading interface status…</p>
      )}
    </div>
  );
}
