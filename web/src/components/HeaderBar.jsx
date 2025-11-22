import { useAuth } from '../hooks/useAuth';

export default function HeaderBar({ onToggleTheme, theme }) {
  const { session, logout } = useAuth();
  return (
    <header>
      <div>
        <div style={{ fontWeight: 700 }}>Train RT Data Simulator</div>
        <div style={{ color: 'var(--muted)', fontSize: '0.9rem' }}>Dashboards & Control Panels</div>
      </div>
      {session && (
        <div className="status-row">
          <span className="badge info">{session.role}</span>
          <button onClick={onToggleTheme} className="touch-target">
            Toggle {theme === 'light' ? 'dark' : 'light'}
          </button>
          <button onClick={logout} className="touch-target">Sign out</button>
        </div>
      )}
    </header>
  );
}
