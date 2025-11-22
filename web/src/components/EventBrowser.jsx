import { useEffect, useMemo, useState } from 'react';
import { apiGet } from '../hooks/useApi';

function StatBar({ label, value, max = 1000, unit = 'us' }) {
  const pct = Math.min(100, (value / max) * 100);
  return (
    <div className="stat-bar">
      <div className="stat-bar__label">
        {label}: {Math.round(value)} {unit}
      </div>
      <div className="stat-bar__track">
        <div className="stat-bar__fill" style={{ width: `${pct}%` }} />
      </div>
    </div>
  );
}

export default function EventBrowser() {
  const [events, setEvents] = useState([]);
  const [metrics, setMetrics] = useState(null);
  const [error, setError] = useState(null);
  const [severity, setSeverity] = useState('ALL');
  const [since, setSince] = useState('');

  const load = async () => {
    try {
      setEvents(await apiGet('/api/diag/events?max=300'));
      setMetrics(await apiGet('/api/diag/metrics'));
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

  const filteredEvents = useMemo(() => {
    const sinceTs = since ? new Date(since).getTime() : 0;
    return events.filter((ev) => {
      const matchesSeverity = severity === 'ALL' || ev.severity === severity;
      const matchesSince = !sinceTs || ev.timestampMs >= sinceTs;
      return matchesSeverity && matchesSince;
    });
  }, [events, severity, since]);

  return (
    <div className="card">
      <div className="flex-between">
        <h2>Event Browser</h2>
        {error && <span className="badge danger">{error}</span>}
      </div>
      <div className="controls-row">
        <label>
          Severity
          <select value={severity} onChange={(e) => setSeverity(e.target.value)}>
            <option value="ALL">All</option>
            <option value="DEBUG">Debug</option>
            <option value="INFO">Info</option>
            <option value="WARN">Warn</option>
            <option value="ERROR">Error</option>
            <option value="FATAL">Fatal</option>
          </select>
        </label>
        <label>
          Since
          <input
            type="datetime-local"
            value={since}
            onChange={(e) => setSince(e.target.value)}
            title="Show events after the selected timestamp"
          />
        </label>
        <span className="badge neutral">{filteredEvents.length} events</span>
      </div>
      <div className="table-scroll" style={{ maxHeight: 260 }}>
        <table>
          <thead>
            <tr>
              <th>Time</th>
              <th>Component</th>
              <th>Severity</th>
              <th>Message</th>
            </tr>
          </thead>
          <tbody>
            {filteredEvents.map((ev) => (
              <tr key={`${ev.timestampMs}-${ev.component}-${ev.message}`}>
                <td>{new Date(ev.timestampMs).toLocaleString()}</td>
                <td>{ev.component}</td>
                <td>
                  <span className={`badge ${ev.severity.toLowerCase()}`}>{ev.severity}</span>
                </td>
                <td>{ev.message}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {metrics ? (
        <div className="grid-2" style={{ marginTop: '1rem' }}>
          <div>
            <h3>Timing</h3>
            <StatBar label="PD jitter" value={metrics.pd.maxCycleJitterUs} max={200000} />
            <StatBar label="PD interarrival" value={metrics.pd.maxInterarrivalUs} max={200000} />
            <StatBar label="MD latency" value={metrics.md.maxLatencyUs} max={200000} />
          </div>
          <div>
            <h3>Error Counters</h3>
            <div className="status-row">
              <span className="badge danger">init: {metrics.trdp.initErrors}</span>
              <span className="badge danger">pub: {metrics.trdp.publishErrors}</span>
              <span className="badge danger">sub: {metrics.trdp.subscribeErrors}</span>
              <span className="badge danger">pd: {metrics.trdp.pdSendErrors}</span>
              <span className="badge danger">md req: {metrics.trdp.mdRequestErrors}</span>
              <span className="badge danger">md rep: {metrics.trdp.mdReplyErrors}</span>
            </div>
          </div>
        </div>
      ) : (
        <p style={{ color: 'var(--muted)' }}>Loading metrics…</p>
      )}
    </div>
  );
}
