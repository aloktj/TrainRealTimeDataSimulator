import { useEffect, useState } from 'react';
import { apiGet, apiPost } from '../hooks/useApi';
import { useRole } from '../hooks/useAuth';

export default function MdPanel() {
  const [sessionId, setSessionId] = useState('');
  const [status, setStatus] = useState(null);
  const [comId, setComId] = useState('');
  const [error, setError] = useState(null);
  const canDev = useRole('Developer');

  const load = async () => {
    if (!sessionId) return;
    try {
      setStatus(await apiGet(`/api/md/session/${sessionId}`));
    } catch (err) {
      setError(err.message);
    }
  };

  useEffect(() => {
    const id = setInterval(load, 5000);
    return () => clearInterval(id);
  });

  const create = async () => {
    const resp = await apiPost(`/api/md/${comId}/request`);
    setSessionId(resp.sessionId || resp.sessionId === 0 ? resp.sessionId : resp);
    setError(null);
  };

  const send = async () => {
    if (!comId) return;
    const resp = await apiPost(`/api/md/${comId}/request`);
    setSessionId(resp.sessionId || resp.sessionId === 0 ? resp.sessionId : resp);
    load();
  };

  return (
    <div className="card">
      <div className="flex-between">
        <h2>MD Sessions</h2>
        {error && <span className="badge danger">{error}</span>}
      </div>
      <div className="grid-halves">
        <label>
          <div>ComID</div>
          <input value={comId} onChange={(e) => setComId(e.target.value)} disabled={!canDev} />
        </label>
        <label>
          <div>Session ID</div>
          <input value={sessionId} onChange={(e) => setSessionId(e.target.value)} disabled />
        </label>
        <div className="button-row" style={{ gridColumn: '1 / -1' }}>
          <button onClick={create} disabled={!canDev || !comId} className="touch-target primary">
            Create Request
          </button>
          <button onClick={send} disabled={!canDev || !sessionId} className="touch-target">
            Send MD Request
          </button>
        </div>
      </div>
      {status && (
        <div className="table-scroll" style={{ marginTop: '0.5rem' }}>
          <table>
            <tbody>
              <tr>
                <th>Role</th>
                <td>{status.role}</td>
              </tr>
              <tr>
                <th>State</th>
                <td><span className="badge info">{status.state}</span></td>
              </tr>
              <tr>
                <th>Stats</th>
                <td className="status-row">
                  <span>TX {status.stats.txCount}</span>
                  <span>RX {status.stats.rxCount}</span>
                  <span>Timeouts {status.stats.timeoutCount}</span>
                </td>
              </tr>
              <tr>
                <th>Request</th>
                <td className="code-block">{status.exchange?.request?.hex || 'N/A'}</td>
              </tr>
              <tr>
                <th>Response</th>
                <td className="code-block">{status.exchange?.response?.hex || 'N/A'}</td>
              </tr>
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}
