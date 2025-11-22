import { useEffect, useState } from 'react';
import { apiGet, apiPost } from '../hooks/useApi';
import { useRole } from '../hooks/useAuth';

export default function TimeSyncPanel() {
  const [state, setState] = useState(null);
  const [convertResult, setConvertResult] = useState(null);
  const [error, setError] = useState(null);
  const [form, setForm] = useState({ seconds: '', nanoseconds: 0 });
  const [offsets, setOffsets] = useState({ ntpOffsetUs: 0, ptpOffsetUs: 0 });
  const canDev = useRole('Developer');

  const load = async () => {
    try {
      const res = await apiGet('/api/time/sync');
      setState(res);
      setOffsets({ ntpOffsetUs: res.ntpOffsetUs, ptpOffsetUs: res.ptpOffsetUs });
      setError(null);
    } catch (err) {
      setError(err.message);
    }
  };

  useEffect(() => {
    load();
  }, []);

  const updateOffsets = async (evt) => {
    evt.preventDefault();
    await apiPost('/api/sim/time', offsets);
    load();
  };

  const convert = async (evt) => {
    evt.preventDefault();
    const secs = Number(form.seconds);
    if (Number.isNaN(secs)) return;
    const nanos = Number(form.nanoseconds) || 0;
    const res = await apiPost('/api/time/convert', { seconds: secs, nanoseconds: nanos });
    setConvertResult(res);
  };

  return (
    <div className="card">
      <div className="flex-between">
        <h2>Time Sync</h2>
        <div className="button-row">
          <button onClick={load} className="touch-target">Refresh</button>
          {error && <span className="badge danger">{error}</span>}
        </div>
      </div>

      <div className="grid-2">
        <div>
          <h3>Offsets</h3>
          <form onSubmit={updateOffsets} className="stacked">
            <label>
              NTP offset (µs)
              <input
                type="number"
                value={offsets.ntpOffsetUs}
                onChange={(e) => setOffsets({ ...offsets, ntpOffsetUs: Number(e.target.value) })}
                disabled={!canDev}
              />
            </label>
            <label>
              PTP offset (µs)
              <input
                type="number"
                value={offsets.ptpOffsetUs}
                onChange={(e) => setOffsets({ ...offsets, ptpOffsetUs: Number(e.target.value) })}
                disabled={!canDev}
              />
            </label>
            <button type="submit" className="touch-target" disabled={!canDev}>Update offsets</button>
          </form>
          {state && (
            <p className="muted">
              Last poll: <span className="monospace">{state.now.iso}</span>
            </p>
          )}
        </div>

        <div>
          <h3>TRDP Timestamp Converter</h3>
          <form onSubmit={convert} className="grid-2">
            <label>
              Seconds
              <input type="number" value={form.seconds} onChange={(e) => setForm({ ...form, seconds: e.target.value })} required />
            </label>
            <label>
              Nanoseconds
              <input type="number" value={form.nanoseconds} onChange={(e) => setForm({ ...form, nanoseconds: e.target.value })} />
            </label>
            <div className="grid-span-2 flex-end">
              <button type="submit" className="touch-target">Convert</button>
            </div>
          </form>
          {convertResult && (
            <div className="stacked" style={{ marginTop: '0.5rem' }}>
              <div className="monospace">UTC: {convertResult.utcIso}</div>
              <div className="monospace">NTP: {convertResult.ntpAdjustedIso}</div>
              <div className="monospace">PTP: {convertResult.ptpAdjustedIso}</div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

