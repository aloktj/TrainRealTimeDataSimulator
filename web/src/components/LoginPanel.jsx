import { useState } from 'react';
import { useAuth } from '../hooks/useAuth';

export default function LoginPanel() {
  const { login, error, setError, loading } = useAuth();
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [busy, setBusy] = useState(false);

  const submit = async (e) => {
    e.preventDefault();
    setBusy(true);
    try {
      await login(username, password);
    } catch (err) {
      setError(err.message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="card" style={{ maxWidth: 420, margin: '2rem auto' }}>
      <h2>Authenticate</h2>
      <p style={{ color: 'var(--muted)' }}>Sign in to unlock simulator controls.</p>
      <form onSubmit={submit} className="grid-halves">
        <label>
          <div>Username</div>
          <input
            required
            autoComplete="username"
            value={username}
            onChange={(e) => setUsername(e.target.value)}
            className="touch-target"
          />
        </label>
        <label>
          <div>Password</div>
          <input
            required
            type="password"
            autoComplete="current-password"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            className="touch-target"
          />
        </label>
        <div style={{ gridColumn: '1 / -1' }} className="button-row">
          <button type="submit" className="primary touch-target" disabled={busy || loading}>
            {busy ? 'Signing in…' : 'Sign in'}
          </button>
          {error && <span className="badge danger">{error}</span>}
        </div>
      </form>
    </div>
  );
}
