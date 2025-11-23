import { createContext, useContext, useEffect, useState } from 'react';
import { apiGet, apiPost } from './useApi';

const AuthContext = createContext(null);

export function AuthProvider({ children }) {
  const [session, setSession] = useState(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);

  useEffect(() => {
    apiGet('/api/auth/session')
      .then(setSession)
      .catch(() => setSession(null))
      .finally(() => setLoading(false));
  }, []);

  const login = async (username, password) => {
    setError(null);
    const data = await apiPost('/api/auth/login', { username, password });
    setSession(data);
  };

  const logout = async () => {
    await apiPost('/api/auth/logout');
    setSession(null);
  };

  const updateTheme = async (theme) => {
    const resp = await apiPost('/api/ui/theme', { theme });
    setSession((prev) => (prev ? { ...prev, theme: resp.theme } : prev));
  };

  const value = { session, loading, error, setError, login, logout, updateTheme };
  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth() {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error('useAuth must be used inside AuthProvider');
  return ctx;
}

export function useRole(required) {
  const { session } = useAuth();
  if (!session) return false;
  if (session.role === 'Admin') return true;
  if (required === 'Viewer') return true;
  if (required === 'Developer') return session.role === 'Developer';
  return session.role === required;
}
