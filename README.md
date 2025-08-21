# Real-time Market Data System

A full-stack real-time messaging and market data demo with a C++ backend and React frontend.

## Features

- Real-time market data streaming via WebSocket
- Thread-safe message bus with shared memory ring buffer
- HTTP API for stats and publishing (including bulk flood)
- Modern React frontend with live buffer sparkline, flood controls, and recent messages
- Counters: published, processed, dropped; adjustable processing delay

## Prerequisites

- macOS or Linux
- C++20 toolchain (clang++ or g++)
- CMake 3.15+
- Boost headers (1.74+)
- Node.js 16+ and npm

## Project layout

```
.
├── backend/       # C++ server (Boost.Beast)
│   ├── src/
│   └── CMakeLists.txt
├── frontend/      # React app (create-react-app)
│   ├── src/
│   └── package.json
└── README.md
```

## Backend: build & run

```bash
# deps (macOS)
brew install cmake boost

cd backend
mkdir -p build && cd build
cmake ..
make -j
./backend
# server listens on 0.0.0.0:8080
```

### Backend APIs

- GET `/api/stats` → `{ buffer_size, buffer_capacity, is_full, is_empty, published_count, processed_count, dropped_count, processing_delay_ms }`
- POST `/api/publish` `{ symbol, price, volume }`
- POST `/api/publish_bulk` `{ count, symbol, price, volume }` (server adds small price/volume jitter per message)
- GET `/api/processing_delay?ms=NNN` (0 to drain fast; e.g., 250ms ≈ ~4 msgs/sec)
- GET `/api/reset_counters` (zeros published/processed/dropped)
- WebSocket `/ws` (server broadcasts normalized market data)

## Frontend: dev & build

```bash
cd frontend
npm install
npm start
# http://localhost:3000
```

- Config file: `frontend/src/config.js`
  - `API_URL`, `WS_URL` (defaults to localhost)
  - `POLL_INTERVAL` (set to 250ms for smooth sparkline)
  - `MAX_MESSAGES` (recent messages cap)
  - `FLOOD` (count, volume, delay)
  - `FLOOD_TICKERS` (30+ realistic symbols rotated during flood)

### UI controls

- Flood: publishes `FLOOD.COUNT` messages interleaved across all tickers with light price/volume jitter
- Slow: sets processing delay to `FLOOD.DELAY_MS`
- Drain: sets processing delay to 0
- Reset: zeros backend counters and clears local sparkline and recent messages

### Notes

- Recent Messages are kept in browser memory by default (cleared on refresh). For persistence across reloads, add a small localStorage cache or a backend `/api/recent` endpoint.
- If the ring buffer is full, new publishes are dropped; see the Dropped counter.

## Deployment

### Push to GitHub

```bash
git init
git branch -M main
cat > .gitignore <<'EOF'
backend/build/
**/build/
**/cmake-build*/
**/*.o
**/*.a
frontend/node_modules/
frontend/.cache/
frontend/build/
.vscode/
.DS_Store
EOF
git add .
git commit -m "Initial commit"
git remote add origin https://github.com/<your-username>/<repo>.git
git push -u origin main
```

### Host frontend

- Vercel or Netlify
  - `cd frontend && npm run build`
  - Deploy `frontend/build`
  - Set API/WS URLs to your backend domain (https)

### Host backend

- Fly.io or Render via Docker
  - Create a Dockerfile that builds with CMake and exposes 8080
  - Deploy and ensure port 8080 is open
  - Keep CORS enabled (already set to `*`)

## Troubleshooting

- Proxy errors in CRA dev server usually mean the backend isn’t running; start backend on 8080
- If stats appear to change in steps: that reflects `processing_delay_ms` vs `POLL_INTERVAL`
- High Dropped count means the ring is saturated (reduce flood count or increase drain speed)

## License

MIT 