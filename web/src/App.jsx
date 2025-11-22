import { useEffect, useState } from 'react';
import HeaderBar from './components/HeaderBar';
import LoginPanel from './components/LoginPanel';
import OverviewPanel from './components/OverviewPanel';
import PdPanel from './components/PdPanel';
import MdPanel from './components/MdPanel';
import DatasetEditor from './components/DatasetEditor';
import XmlViewer from './components/XmlViewer';
import LogViewer from './components/LogViewer';
import InterfaceDiagnostics from './components/InterfaceDiagnostics';
import { useAuth } from './hooks/useAuth';
import './styles/theme.css';

export default function App() {
  const { session, loading, updateTheme } = useAuth();
  const [theme, setTheme] = useState('dark');

  useEffect(() => {
    const applied = session?.theme || 'dark';
    setTheme(applied);
    document.body.className = applied === 'light' ? 'light' : '';
  }, [session]);

  const toggleTheme = () => {
    const next = theme === 'light' ? 'dark' : 'light';
    setTheme(next);
    document.body.className = next === 'light' ? 'light' : '';
    updateTheme(next);
  };

  if (loading) return <p style={{ padding: '2rem', color: 'var(--muted)' }}>Checking session…</p>;
  if (!session) return <LoginPanel />;

  return (
    <div className="app-shell">
      <HeaderBar onToggleTheme={toggleTheme} theme={theme} />
      <main>
        <div className="dashboard-grid">
          <OverviewPanel />
          <PdPanel />
          <MdPanel />
          <DatasetEditor />
          <XmlViewer />
          <LogViewer />
          <InterfaceDiagnostics />
        </div>
      </main>
    </div>
  );
}
