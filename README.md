# Real-time Market Data System

A full-stack real-time messaging and market data demo with a C++ backend and React frontend.

## Features

- Live WebSocket stream (Boost.Beast) + HTTP API for stats/publish/bulk flood
- Lock-free shared-memory ring buffer for messages
- Interactive React UI with sparkline, Flood/Slow/Drain/Reset controls
- Counters: published, processed, dropped; adjustable processing delay

## Prerequisites

- macOS or Linux
- C++20 toolchain (clang++/g++) and CMake 3.15+
- Boost headers (1.74+)
- Node.js 16+ and npm

## Project layout

```
.
├── backend/       # C++ server
│   ├── src/
│   └── CMakeLists.txt
├── frontend/      # React app
│   ├── src/
│   └── package.json
└── README.md
```

## Backend: build & run locally

```bash
brew install cmake boost     # macOS deps
cd backend
mkdir -p build && cd build
cmake .. && make -j
./backend            # serves on 0.0.0.0:8080
```

### API quick reference

- GET `/api/stats` → `{ buffer_size, buffer_capacity, is_full, is_empty, published_count, processed_count, dropped_count, processing_delay_ms }`
- POST `/api/publish` `{ symbol, price, volume }`
- POST `/api/publish_bulk` `{ count, symbol, price, volume }` (server adds small jitter)
- GET `/api/processing_delay?ms=NNN`
- GET `/api/reset_counters`
- WS `/ws` (market data stream)

## Frontend: dev & build locally

```bash
cd frontend
npm install
npm start     # http://localhost:3000
```

Config: `frontend/src/config.js`
- `REACT_APP_API_URL`, `REACT_APP_WS_URL` (used by Render/Netlify builds)
- `POLL_INTERVAL` (250ms), `MAX_MESSAGES` (100)
- `FLOOD` (count/volume/delay), `FLOOD_TICKERS` (30+ symbols)

## Deploy (Render + Static Site)

Backend (Docker Web Service)
- Root Directory: `backend`
- Port: `8080`
- Health Check: `/api/stats`
- Free plan OK

Frontend (Static Site)
- Root Directory: `frontend`
- Build Command: `npm ci && npm run build`
- Publish Directory: `build`
- Environment:
  - `REACT_APP_API_URL = https://<your-backend>.onrender.com/api`
  - `REACT_APP_WS_URL  = wss://<your-backend>.onrender.com/ws`

## Tips

- If stats change in steps, that reflects `processing_delay_ms` versus polling interval
- High “Dropped” means the buffer is full (reduce flood count or increase drain speed)
- Recent Messages persist across refresh via localStorage; Reset clears the cache 