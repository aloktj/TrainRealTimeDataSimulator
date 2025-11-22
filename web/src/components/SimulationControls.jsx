import { useEffect, useState } from 'react';
import { apiGet, apiPost } from '../hooks/useApi';
import { useRole } from '../hooks/useAuth';

export default function SimulationControls() {
  const [state, setState] = useState(null);
  const [error, setError] = useState(null);
  const [registerForm, setRegisterForm] = useState({ name: '', path: '' });
  const canDev = useRole('Developer');

  const loadState = async () => {
    try {
      setState(await apiGet('/api/sim/state'));
      setError(null);
    } catch (err) {
      setError(err.message);
    }
  };

  useEffect(() => {
    loadState();
  }, []);

  const applyStress = async (evt) => {
    evt.preventDefault();
    const form = new FormData(evt.target);
    await apiPost('/api/sim/stress', {
      enabled: form.get('enabled') === 'on',
      pdCycleUs: Number(form.get('pdCycleUs')),
      pdBurst: Number(form.get('pdBurst')),
      mdBurst: Number(form.get('mdBurst')),
      mdIntervalUs: Number(form.get('mdIntervalUs'))
    });
    loadState();
  };

  const applyRedundancy = async (evt) => {
    evt.preventDefault();
    const form = new FormData(evt.target);
    await apiPost('/api/sim/redundancy', {
      forceSwitch: form.get('forceSwitch') === 'on',
      busFailure: form.get('busFailure') === 'on',
      failedChannel: Number(form.get('failedChannel'))
    });
    loadState();
  };

  const registerInstance = async (evt) => {
    evt.preventDefault();
    await apiPost('/api/sim/instances/register', registerForm);
    setRegisterForm({ name: '', path: '' });
    loadState();
  };

  const activate = async (name) => {
    await apiPost('/api/sim/instances/activate', { name });
    loadState();
  };

  return (
    <div className="card">
      <div className="flex-between">
        <h2>Simulation Orchestrator</h2>
        <div className="button-row">
          <button onClick={loadState} className="touch-target">Refresh</button>
          {error && <span className="badge danger">{error}</span>}
        </div>
      </div>

      <form onSubmit={applyStress} className="stacked">
        <div className="flex-between">
          <h3>Stress Mode</h3>
          <label>
            <input type="checkbox" name="enabled" defaultChecked={state?.stress?.enabled} disabled={!canDev} /> Enable
          </label>
        </div>
        <div className="grid-2">
          <label>
            PD cycle override (µs)
            <input type="number" name="pdCycleUs" min="1000" max="10000000" defaultValue={state?.stress?.pdCycleOverrideUs || 0} disabled={!canDev} />
          </label>
          <label>
            PD burst telegrams (≤1000)
            <input type="number" name="pdBurst" min="0" max="1000" defaultValue={state?.stress?.pdBurstTelegrams || 0} disabled={!canDev} />
          </label>
          <label>
            MD burst size (≤1000)
            <input type="number" name="mdBurst" min="0" max="1000" defaultValue={state?.stress?.mdBurst || 0} disabled={!canDev} />
          </label>
          <label>
            MD interval (µs, min 1000)
            <input type="number" name="mdIntervalUs" min="1000" defaultValue={state?.stress?.mdIntervalUs || 1000} disabled={!canDev} />
          </label>
        </div>
        <button type="submit" className="touch-target" disabled={!canDev}>Apply stress settings</button>
      </form>

      <hr />

      <form onSubmit={applyRedundancy} className="stacked">
        <h3>Redundancy &amp; Fault Injection</h3>
        <div className="grid-2">
          <label>
            <input type="checkbox" name="forceSwitch" defaultChecked={state?.redundancy?.forceSwitch} disabled={!canDev} /> Force channel switch
          </label>
          <label>
            <input type="checkbox" name="busFailure" defaultChecked={state?.redundancy?.busFailure} disabled={!canDev} /> Simulate bus failure
          </label>
          <label>
            Failed channel index
            <input type="number" name="failedChannel" min="0" max="3" defaultValue={state?.redundancy?.failedChannel || 0} disabled={!canDev} />
          </label>
        </div>
        <button type="submit" className="touch-target" disabled={!canDev}>Apply redundancy</button>
      </form>

      <hr />

      <div className="stacked">
        <h3>Virtual Instances</h3>
        <div className="table-scroll">
          <table>
            <thead>
              <tr>
                <th>Name</th>
                <th>Path</th>
                <th>Status</th>
                <th>Action</th>
              </tr>
            </thead>
            <tbody>
              {(state?.instances || []).map((inst) => (
                <tr key={inst.name}>
                  <td>{inst.name}</td>
                  <td className="monospace">{inst.path}</td>
                  <td>{inst.active ? 'Active' : 'Idle'}</td>
                  <td>
                    <button onClick={() => activate(inst.name)} className="touch-target" disabled={!canDev || inst.active}>
                      Activate
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <form onSubmit={registerInstance} className="grid-2">
          <label>
            Name
            <input
              type="text"
              value={registerForm.name}
              onChange={(e) => setRegisterForm({ ...registerForm, name: e.target.value })}
              required
              disabled={!canDev}
            />
          </label>
          <label>
            XML path
            <input
              type="text"
              value={registerForm.path}
              onChange={(e) => setRegisterForm({ ...registerForm, path: e.target.value })}
              required
              disabled={!canDev}
            />
          </label>
          <div className="grid-span-2 flex-end">
            <button type="submit" className="touch-target" disabled={!canDev}>Register instance</button>
          </div>
        </form>
      </div>
    </div>
  );
}

