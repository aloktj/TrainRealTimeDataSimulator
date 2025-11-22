import { useEffect, useMemo, useState } from 'react';
import { apiGet, apiPost } from '../hooks/useApi';
import { useRole } from '../hooks/useAuth';

function ElementRow({ element, idx, onAction, readOnly, locked }) {
  const [raw, setRaw] = useState(element.raw);
  useEffect(() => setRaw(element.raw), [element.raw]);
  const canEdit = !readOnly && !locked;

  return (
    <tr>
      <td>{element.name}</td>
      <td>{element.type}</td>
      <td>{element.arraySize}</td>
      <td>
        <textarea
          rows={2}
          value={raw.join ? raw.join(',') : raw}
          onChange={(e) => setRaw(e.target.value.split(',').map((x) => Number(x.trim()) || 0))}
          disabled={!canEdit}
        />
      </td>
      <td>
        <div className="button-row">
          <button
            onClick={() => onAction('set', idx, raw)}
            disabled={!canEdit}
            className="touch-target primary"
          >
            SET
          </button>
          <button onClick={() => onAction('clear', idx)} disabled={!canEdit} className="touch-target">
            CLEAR
          </button>
        </div>
      </td>
    </tr>
  );
}

export default function DatasetEditor() {
  const [dataSetId, setDataSetId] = useState('');
  const [dataset, setDataset] = useState(null);
  const [error, setError] = useState(null);
  const canDev = useRole('Developer');

  const load = async () => {
    if (!dataSetId) return;
    try {
      setDataset(await apiGet(`/api/datasets/${dataSetId}`));
      setError(null);
    } catch (err) {
      setError(err.message);
    }
  };

  useEffect(() => {
    load();
  }, [dataSetId]);

  const handleAction = async (action, idx, payload) => {
    const path = `/api/datasets/${dataSetId}/elements/${idx}`;
    if (action === 'set') {
      await apiPost(path, { raw: payload });
    } else if (action === 'clear') {
      await apiPost(path, { clear: true });
    }
    load();
  };

  const clearAll = async () => {
    await apiPost(`/api/datasets/${dataSetId}/clear_all`, {});
    load();
  };

  const lock = async (flag) => {
    await apiPost(`/api/datasets/${dataSetId}/lock`, { locked: flag });
    load();
  };

  const statusBadge = useMemo(() => {
    if (!dataset) return null;
    return (
      <span className={`badge ${dataset.status === 'Active' ? 'success' : 'warn'}`}>
        {dataset.status}
      </span>
    );
  }, [dataset]);

  return (
    <div className="card">
      <div className="flex-between">
        <h2>Dataset Editor</h2>
        {error && <span className="badge danger">{error}</span>}
      </div>
      <div className="grid-halves">
        <label>
          <div>Dataset ID</div>
          <input value={dataSetId} onChange={(e) => setDataSetId(e.target.value)} />
        </label>
        {dataset && (
          <div className="status-row" style={{ marginTop: '1.6rem' }}>
            <span className="badge info">{dataset.name}</span>
            {statusBadge}
            <span className={`badge ${dataset.isOutgoing ? 'success' : 'warn'}`}>
              {dataset.isOutgoing ? 'Editable' : 'Read-only'}
            </span>
          </div>
        )}
        <div className="button-row" style={{ gridColumn: '1 / -1' }}>
          <button onClick={load} className="touch-target">Refresh</button>
          <button onClick={() => lock(!(dataset?.locked))} disabled={!dataset || !canDev} className="touch-target">
            {dataset?.locked ? 'UNLOCK' : 'LOCK'}
          </button>
          <button onClick={clearAll} disabled={!dataset || !canDev || dataset?.readOnly || dataset?.locked} className="touch-target">
            CLEAR ALL
          </button>
        </div>
      </div>
      {dataset && (
        <div className="table-scroll" style={{ marginTop: '0.75rem' }}>
          <table>
            <thead>
              <tr>
                <th>Name</th>
                <th>Type</th>
                <th>Size</th>
                <th>Raw Bytes</th>
                <th>Actions</th>
              </tr>
            </thead>
            <tbody>
              {dataset.values.map((elem, idx) => (
                <ElementRow
                  key={idx}
                  idx={idx}
                  element={elem}
                  onAction={handleAction}
                  readOnly={dataset.readOnly || !canDev}
                  locked={dataset.locked}
                />
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}
