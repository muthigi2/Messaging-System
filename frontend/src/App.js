import React, { useState, useEffect, useRef, useCallback } from 'react';
import config from './config';
import './App.css';

function App() {
  const [messages, setMessages] = useState([]);
  const [stats, setStats] = useState({});
  const [history, setHistory] = useState([]); // buffer size history
  const [formData, setFormData] = useState(config.DEFAULT_FORM);
  const [connected, setConnected] = useState(false);
  const [submitting, setSubmitting] = useState(false);
  const ws = useRef(null);
  const lastMsgKeyRef = useRef('');

  // Hydrate messages from localStorage on initial load
  useEffect(() => {
    try {
      const cached = JSON.parse(localStorage.getItem('recentMessages') || '[]');
      if (Array.isArray(cached) && cached.length) {
        setMessages(cached);
      }
    } catch {}
  }, []);

  // Persist messages to localStorage whenever they change
  useEffect(() => {
    try {
      localStorage.setItem('recentMessages', JSON.stringify(messages.slice(0, config.MAX_MESSAGES)));
    } catch {}
  }, [messages]);

  useEffect(() => {
    let reconnectTimeout;
    let isConnecting = false;
    let reconnectAttempts = 0;
    const MAX_RECONNECT_ATTEMPTS = 5;

    const connectWebSocket = () => {
      if (isConnecting) {
        console.log('Already attempting to connect...');
        return;
      }

      if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
        console.log('Max reconnection attempts reached. Please refresh the page.');
        return;
      }

      // Ensure only one socket is active
      try {
        if (ws.current && (ws.current.readyState === WebSocket.OPEN || ws.current.readyState === WebSocket.CONNECTING)) {
          ws.current.onopen = null;
          ws.current.onmessage = null;
          ws.current.onclose = null;
          ws.current.onerror = null;
          ws.current.close(1000, 'reconnect');
        }
      } catch {}

      isConnecting = true;
      console.log(`Attempting to connect to WebSocket... (Attempt ${reconnectAttempts + 1}/${MAX_RECONNECT_ATTEMPTS})`);

      try {
        ws.current = new WebSocket(config.WS_URL);

        ws.current.onopen = () => {
          console.log('Connected to WebSocket');
          setConnected(true);
          isConnecting = false;
          reconnectAttempts = 0; // Reset reconnect attempts on successful connection
        };

        ws.current.onmessage = (event) => {
          try {
            const data = JSON.parse(event.data);
            if (data.type === 'market_data') {
              const key = String(data.seq || '')
                || `${data.symbol}|${data.price}|${data.volume}|${data.timestamp}`;
              if (key !== lastMsgKeyRef.current) {
                lastMsgKeyRef.current = key;
                // Keep all messages, but avoid placing same symbol back-to-back at the top
                setMessages(prev => {
                  if (!prev.length || prev[0].symbol !== data.symbol) {
                    return [data, ...prev].slice(0, config.MAX_MESSAGES);
                  }
                  // Find first position where symbol differs and insert there
                  const insertAt = prev.findIndex(m => m.symbol !== data.symbol);
                  if (insertAt === -1) {
                    // All same; append to the end (still keeps them, not dropped)
                    return [...prev.slice(0, config.MAX_MESSAGES - 1), data];
                  }
                  const next = [...prev];
                  next.splice(insertAt, 0, data);
                  return next.slice(0, config.MAX_MESSAGES);
                });
              }
            }
          } catch (error) {
            console.error('Error parsing WebSocket message:', error);
          }
        };

        ws.current.onclose = (event) => {
          console.log('WebSocket closed:', event.code, event.reason);
          setConnected(false);
          isConnecting = false;

          // Only attempt to reconnect if the connection was not closed intentionally
          if (event.code !== 1000) {
            reconnectAttempts++;
            console.log(`Reconnecting in ${config.RECONNECT_DELAY}ms... (Attempt ${reconnectAttempts}/${MAX_RECONNECT_ATTEMPTS})`);
            reconnectTimeout = setTimeout(connectWebSocket, config.RECONNECT_DELAY);
          }
        };

        ws.current.onerror = (error) => {
          console.error('WebSocket error:', error);
          setConnected(false);
          isConnecting = false;
        };
      } catch (error) {
        console.error('Error creating WebSocket:', error);
        isConnecting = false;
        reconnectAttempts++;
        console.log(`Reconnecting in ${config.RECONNECT_DELAY}ms... (Attempt ${reconnectAttempts}/${MAX_RECONNECT_ATTEMPTS})`);
        reconnectTimeout = setTimeout(connectWebSocket, config.RECONNECT_DELAY);
      }
    };

    // Initial connection
    connectWebSocket();

    // Fetch initial stats
    fetchStats();

    // Set up stats polling
    const statsInterval = setInterval(fetchStats, config.POLL_INTERVAL);

    return () => {
      if (reconnectTimeout) {
        clearTimeout(reconnectTimeout);
      }
      if (ws.current) {
        ws.current.close(1000, 'Component unmounting');
      }
      clearInterval(statsInterval);
    };
  }, []);

  const fetchStats = async () => {
    if (fetchStats.inFlight) return;
    fetchStats.inFlight = true;
    try {
      const response = await fetch(`${config.API_URL}/stats`);
      const data = await response.json();
      setStats(data);
      setHistory(prev => {
        const next = [...prev, data.buffer_size ?? 0].slice(-config.HISTORY_POINTS);
        return next;
      });
    } catch (error) {
      console.error('Error fetching stats:', error);
    } finally {
      fetchStats.inFlight = false;
    }
  };

  const handleSubmit = async (e) => {
    e.preventDefault();
    if (submitting) return;
    setSubmitting(true);
    
    // Convert price and volume to numbers
    const payload = {
      symbol: formData.symbol,
      price: Number(formData.price),
      volume: Number(formData.volume)
    };

    try {
      const response = await fetch(`${config.API_URL}/publish`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(payload),
      });

      let result = null;
      const text = await response.text();
      try { result = text ? JSON.parse(text) : null; } catch (e) { /* ignore parse error */ }

      if (response.ok) {
        console.log('Publish result:', result || text);
        // Clear form
        setFormData(config.DEFAULT_FORM);
      } else {
        const msg = (result && (result.error || result.message)) || text || 'Unknown error';
        alert('Error publishing message: ' + msg);
      }
    } catch (error) {
      console.error('Error publishing message:', error);
      alert('Error publishing message. Please try again.');
    } finally {
      // Slight cooldown to avoid immediate double-submits
      setTimeout(() => setSubmitting(false), 300);
    }
  };

  const handleChange = (e) => {
    const { name, value } = e.target;
    setFormData(prev => ({
      ...prev,
      [name]: value
    }));
  };

  return (
    <div className="App">
      <header className="App-header">
        <h1>Real-time Market Data</h1>
        <div className="connection-status">
          Status: {connected ? 'Connected' : 'Disconnected'}
        </div>
      </header>

      <div className="container">
        <div className="stats-panel">
          <div className="card-title">
            <h2>Buffer Stats</h2>
            <span className="badge">live</span>
          </div>
          <div className="stats-grid">
            <div className="stat-item">
              <span>Size:</span>
              <span>{stats.buffer_size || 0}</span>
            </div>
            <div className="stat-item">
              <span>Capacity:</span>
              <span>{stats.buffer_capacity || 0}</span>
            </div>
            <div className="stat-item">
              <span>Full:</span>
              <span>{stats.is_full ? 'Yes' : 'No'}</span>
            </div>
            <div className="stat-item">
              <span>Empty:</span>
              <span>{stats.is_empty ? 'Yes' : 'No'}</span>
            </div>
            <div className="stat-item">
              <span>Dropped:</span>
              <span>{stats.dropped_count || 0}</span>
            </div>
          </div>
          <Sparkline data={history} max={stats.buffer_capacity || 1} />
          <div className="controls">
            <button className="primary" onClick={flood}>Flood</button>
            <button className="warn" onClick={slow}>Slow</button>
            <button className="success" onClick={drain}>Drain</button>
            <button className="muted" onClick={() => {
              resetCounters(setHistory, setMessages, () => { lastMsgKeyRef.current = ''; });
            }}>Reset</button>
          </div>
        </div>

        <div className="publish-panel">
          <h2>Publish Message</h2>
          <form onSubmit={handleSubmit}>
            <div className="form-group">
              <label>Symbol:</label>
              <input
                type="text"
                name="symbol"
                value={formData.symbol}
                onChange={handleChange}
                required
                placeholder="e.g., AAPL"
              />
            </div>
            <div className="form-group">
              <label>Price:</label>
              <input
                type="number"
                name="price"
                value={formData.price}
                onChange={handleChange}
                step="0.01"
                required
                placeholder="e.g., 150.25"
              />
            </div>
            <div className="form-group">
              <label>Volume:</label>
              <input
                type="number"
                name="volume"
                value={formData.volume}
                onChange={handleChange}
                step="0.01"
                required
                placeholder="e.g., 1000"
              />
            </div>
            <button type="submit" disabled={submitting || !formData.symbol || formData.price === '' || formData.volume === ''}>Publish</button>
          </form>
        </div>

        <div className="messages-panel">
          <h2>Recent Messages</h2>
          <div className="messages-list">
            {messages.map((msg) => (
              <div key={msg.seq ?? `${msg.symbol}-${msg.timestamp}-${msg.price}-${msg.volume}`}
                   className="message-item">
                <span className="symbol">{msg.symbol}</span>
                <span className="price">${msg.price.toFixed(2)}</span>
                <span className="volume">{msg.volume.toFixed(2)}</span>
                <span className="timestamp">
                  {new Date(Number(msg.timestamp)).toLocaleTimeString()}
                </span>
                <span className="source">{msg.source}</span>
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}

// Minimal sparkline SVG component
function Sparkline({ data, max }) {
  const height = 40;
  const width = 260;
  const points = data.length ? data : [0];
  const step = width / Math.max(points.length - 1, 1);
  const path = points
    .map((v, i) => {
      const x = i * step;
      const y = height - (Math.min(v, max) / Math.max(max, 1)) * height;
      return `${i === 0 ? 'M' : 'L'} ${x.toFixed(1)} ${y.toFixed(1)}`;
    })
    .join(' ');
  const area = `${path} L ${width} ${height} L 0 ${height} Z`;
  return (
    <svg className="sparkline" viewBox={`0 0 ${width} ${height}`}>
      <defs>
        <linearGradient id="sparkGrad" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#3b82f6" stopOpacity="0.6" />
          <stop offset="100%" stopColor="#3b82f6" stopOpacity="0.05" />
        </linearGradient>
      </defs>
      <path d={area} fill="url(#sparkGrad)" stroke="none" />
      <path d={path} fill="none" stroke="#60a5fa" strokeWidth="2">
        <animate attributeName="stroke-dasharray" values="0,1000;1000,0" dur="2s" repeatCount="indefinite" />
      </path>
    </svg>
  );
}

// UI control handlers
async function apiGet(path) {
  try { await fetch(path); } catch {}
}

function flood() {
  apiGet(`${config.API_URL.replace('/api','')}/api/processing_delay?ms=${config.FLOOD.DELAY_MS}`);
  // Interleave across ALL tickers in small micro-batches over a few rounds
  const tickers = config.FLOOD_TICKERS;
  const N = Math.max(1, tickers.length);
  const total = Math.max(1, config.FLOOD.COUNT);
  const perBatch = 1; // send exactly 1 per symbol per round to interleave strictly
  const rounds = Math.max(1, Math.ceil(total / (N * perBatch)));
  let remaining = total;

  const runRound = (roundIdx) => {
    if (roundIdx >= rounds || remaining <= 0) return;
    const requests = [];
    for (let i = 0; i < N && remaining > 0; i++) {
      const base = tickers[(roundIdx + i) % N];
      const count = Math.min(perBatch, remaining);
      remaining -= count;
      const jitter = (Math.random() - 0.5) * 0.02 * base.price; // +/-2%
      const price = Number((base.price + jitter).toFixed(2));
      const volume = Math.max(1, Math.round(config.FLOOD.VOLUME + (Math.random() - 0.5) * 4)); // vary 1-5
      requests.push(
        fetch(`${config.API_URL}/publish_bulk`, {
          method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ count, symbol: base.symbol, price, volume })
        })
      );
    }
    Promise.allSettled(requests).finally(() => {
      // slight delay to let server interleave
      setTimeout(() => runRound(roundIdx + 1), 20);
    });
  };

  runRound(0);
}

function slow() {
  apiGet(`${config.API_URL.replace('/api','')}/api/processing_delay?ms=${config.FLOOD.DELAY_MS}`);
}

function drain() {
  apiGet(`${config.API_URL.replace('/api','')}/api/processing_delay?ms=0`);
}

async function resetCounters(setHistory, setMessages, clearKey) {
  try {
    await fetch(`${config.API_URL.replace('/api','')}/api/reset_counters`);
  } catch {}
  setHistory([]);
  if (typeof setMessages === 'function') setMessages([]);
  if (typeof clearKey === 'function') clearKey();
  try { localStorage.removeItem('recentMessages'); } catch {}
}

export default App; 