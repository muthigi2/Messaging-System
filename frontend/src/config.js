const config = {
    // WebSocket server URL
    WS_URL: 'ws://localhost:8080/ws',
    
    // HTTP API server URL
    API_URL: 'http://localhost:8080/api',
    
    // Stats polling interval in milliseconds
    POLL_INTERVAL: 250,
    
    // Maximum number of messages to display
    MAX_MESSAGES: 100,
    
    // Reconnection delay in milliseconds
    RECONNECT_DELAY: 5000,

    // Sparkline history points
    HISTORY_POINTS: 60,

    // Flood defaults (uses existing endpoints)
    FLOOD: {
        COUNT: 5000,
        SYMBOL: 'LOAD',
        PRICE: 101.23,
        VOLUME: 1,
        DELAY_MS: 250
    },

    // Realistic ticker set used by Flood button (rotated)
    FLOOD_TICKERS: [
        { symbol: 'AAPL', price: 192.4 },
        { symbol: 'MSFT', price: 425.3 },
        { symbol: 'GOOGL', price: 168.7 },
        { symbol: 'AMZN', price: 182.1 },
        { symbol: 'META', price: 524.6 },
        { symbol: 'NVDA', price: 875.2 },
        { symbol: 'TSLA', price: 172.9 },
        { symbol: 'NFLX', price: 643.8 },
        { symbol: 'AMD', price: 168.1 },
        { symbol: 'AVGO', price: 1345.0 },
        { symbol: 'CRM', price: 283.2 },
        { symbol: 'ORCL', price: 134.9 },
        { symbol: 'ADBE', price: 520.4 },
        { symbol: 'INTC', price: 34.7 },
        { symbol: 'CSCO', price: 47.8 },
        { symbol: 'SHOP', price: 74.5 },
        { symbol: 'UBER', price: 70.6 },
        { symbol: 'ABNB', price: 148.3 },
        { symbol: 'SNOW', price: 146.2 },
        { symbol: 'PLTR', price: 26.8 },
        { symbol: 'SQ', price: 82.1 },
        { symbol: 'PYPL', price: 66.5 },
        { symbol: 'COIN', price: 212.7 },
        { symbol: 'IBM', price: 182.0 },
        { symbol: 'SAP', price: 192.9 },
        { symbol: 'ASML', price: 939.4 },
        { symbol: 'TXN', price: 187.3 },
        { symbol: 'QCOM', price: 184.6 },
        { symbol: 'INTU', price: 627.2 },
        { symbol: 'NOW', price: 780.5 }
    ],
    
    // Default form values
    DEFAULT_FORM: {
        symbol: '',
        price: '',
        volume: ''
    }
};

export default config; 