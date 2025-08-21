#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <deque>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>
#include "message_bus.hpp"
#include "shared_memory.hpp"
#include "market_data/finnhub_client.hpp"
#include "market_data/replay_engine.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

// Helper function to convert time_point to int64_t
int64_t time_point_to_int64(const std::chrono::system_clock::time_point& tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()).count();
}

// Helper function to convert int64_t to time_point
std::chrono::system_clock::time_point int64_to_time_point(int64_t ms) {
    return std::chrono::system_clock::time_point(
        std::chrono::milliseconds(ms));
}

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
    WebSocketSession(tcp::socket socket, std::shared_ptr<lockfree::MessageBus> message_bus)
        : ws_(std::move(socket))
        , message_bus_(message_bus) {
    }

    void start(const http::request<http::string_body>& req) {
        // Set suggested timeout settings for the websocket
        ws_.set_option(websocket::stream_base::timeout::suggested(
            beast::role_type::server));

        // Send text frames
        ws_.text(true);

        // Set a decorator to change the Server of the response
        ws_.set_option(websocket::stream_base::decorator(
            [](websocket::response_type& res) {
                res.set(http::field::server, "Market Data Server");
                res.set(http::field::access_control_allow_origin, "*");
                res.set(http::field::access_control_allow_headers, "*");
                res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
            }));

        // Accept the websocket handshake
        ws_.async_accept(req,
            beast::bind_front_handler(
                &WebSocketSession::on_accept,
                shared_from_this()));
    }

private:
    void on_accept(beast::error_code ec) {
        if (ec) {
            std::cerr << "WebSocket accept error: " << ec.message() << std::endl;
            return;
        }
        
        std::cout << "WebSocket connection established" << std::endl;
        
        // Subscribe to market data
        message_bus_->subscribe<lockfree::MarketData>("market_data", 
            [self = shared_from_this()](const lockfree::MarketData& data) {
                json message = {
                    {"type", "market_data"},
                    {"symbol", data.symbol},
                    {"price", data.price},
                    {"volume", data.volume},
                    {"seq", data.seq},
                    {"timestamp", data.timestamp},
                    {"source", data.source}
                };
                self->send_text(message.dump());
            });

        do_read();
    }

    // Queue a message to be written on the WebSocket from the io_context thread
    void send_text(std::string message) {
        auto self = shared_from_this();
        net::post(ws_.get_executor(), [self, msg = std::move(message)]() mutable {
            if (!self->ws_.is_open()) return;
            self->outgoing_messages_.push_back(std::move(msg));
            if (!self->write_in_progress_) {
                self->write_in_progress_ = true;
                self->ws_.async_write(
                    net::buffer(self->outgoing_messages_.front()),
                    beast::bind_front_handler(&WebSocketSession::on_write, self));
            }
        });
    }

    void do_read() {
        if (!ws_.is_open()) return;

        ws_.async_read(
            buffer_,
            beast::bind_front_handler(
                &WebSocketSession::on_read,
                shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        if (ec == websocket::error::closed) {
            std::cout << "WebSocket connection closed" << std::endl;
            return;
        }
        if (ec) {
            std::cerr << "WebSocket read error: " << ec.message() << std::endl;
            return;
        }

        // Discard client message and continue reading. Writes are handled by send_text queue.
        buffer_.consume(buffer_.size());
        do_read();
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        if (ec) {
            std::cerr << "WebSocket write error: " << ec.message() << std::endl;
            return;
        }
        // Remove the message that was just sent and send next if queued
        if (!outgoing_messages_.empty()) {
            outgoing_messages_.pop_front();
        }
        if (!outgoing_messages_.empty()) {
            ws_.async_write(
                net::buffer(outgoing_messages_.front()),
                beast::bind_front_handler(&WebSocketSession::on_write, shared_from_this()));
        } else {
            write_in_progress_ = false;
        }
    }

    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    std::shared_ptr<lockfree::MessageBus> message_bus_;
    std::deque<std::string> outgoing_messages_;
    bool write_in_progress_ = false;
};

class HttpServer {
public:
    HttpServer(net::io_context& ioc, const std::string& address, unsigned short port,
              std::shared_ptr<lockfree::MessageBus> message_bus)
        : acceptor_(ioc)
        , message_bus_(message_bus) {
        std::cout << "[HttpServer] Constructor: starting" << std::endl;
        beast::error_code ec;

        std::cout << "[HttpServer] Opening acceptor..." << std::endl;
        // Open the acceptor
        acceptor_.open(tcp::v4(), ec);
        if (ec) {
            std::cerr << "[HttpServer] Failed to open acceptor: " << ec.message() << std::endl;
            throw std::runtime_error("Failed to open acceptor: " + ec.message());
        }

        std::cout << "[HttpServer] Setting reuse address..." << std::endl;
        // Allow address reuse
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) {
            std::cerr << "[HttpServer] Failed to set reuse address: " << ec.message() << std::endl;
            throw std::runtime_error("Failed to set reuse address: " + ec.message());
        }

        std::cout << "[HttpServer] Binding to address..." << std::endl;
        // Bind to the server address
        acceptor_.bind(tcp::endpoint(net::ip::make_address(address), port), ec);
        if (ec) {
            std::cerr << "[HttpServer] Failed to bind: " << ec.message() << std::endl;
            throw std::runtime_error("Failed to bind: " + ec.message());
        }

        std::cout << "[HttpServer] Listening..." << std::endl;
        // Start listening for connections
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) {
            std::cerr << "[HttpServer] Failed to start listening: " << ec.message() << std::endl;
            throw std::runtime_error("Failed to start listening: " + ec.message());
        }
        std::cout << "[HttpServer] Constructor: finished" << std::endl;
    }

    void start() {
        do_accept();
    }

private:
    // Helper struct to hold socket, buffer, and request during async_read
    struct SessionHolder : public std::enable_shared_from_this<SessionHolder> {
        tcp::socket socket;
        std::shared_ptr<beast::flat_buffer> buffer;
        std::shared_ptr<http::request_parser<http::string_body>> parser;
        std::shared_ptr<lockfree::MessageBus> message_bus;
        HttpServer* server;
        SessionHolder(tcp::socket s, std::shared_ptr<lockfree::MessageBus> mb, HttpServer* srv)
            : socket(std::move(s)), buffer(std::make_shared<beast::flat_buffer>()), parser(std::make_shared<http::request_parser<http::string_body>>()), message_bus(mb), server(srv) {}
        void start() {
            auto self = shared_from_this();
            http::async_read_header(socket, *buffer, *parser,
                [self](beast::error_code ec, std::size_t) mutable {
                    if (!ec) {
                        auto& req = self->parser->get();
                        std::cout << "[SessionHolder] Incoming request: method=" << req.method_string()
                                  << ", target=" << req.target() << ", version=" << req.version() << std::endl;
                        std::cout << "[SessionHolder] Headers:" << std::endl;
                        for (const auto& field : req) {
                            std::cout << "  " << field.name_string() << ": " << field.value() << std::endl;
                        }
                        if (websocket::is_upgrade(req)) {
                            std::cout << "WebSocket upgrade request detected" << std::endl;
                            std::string target = std::string(req.target());
                            if (target == "/ws" || target == "/ws/" || target.rfind("/ws?", 0) == 0) {
                                std::cout << "Creating WebSocket session for " << target << std::endl;
                                auto ws_session = std::make_shared<WebSocketSession>(std::move(self->socket), self->message_bus);
                                ws_session->start(req);
                                // Continue accepting new connections even after upgrading to WebSocket
                                self->server->do_accept();
                                return;
                            } else {
                                std::cout << "Invalid WebSocket target: " << req.target() << std::endl;
                            }
                        }
                        // If not a WebSocket upgrade, read the full HTTP body and handle as HTTP
                        http::async_read(self->socket, *self->buffer, *self->parser,
                            [self](beast::error_code ec2, std::size_t) mutable {
                                if (!ec2) {
                                    auto http_session = std::make_shared<HttpSession>(std::move(self->socket), self->message_bus,
                                        std::make_shared<http::request<http::string_body>>(self->parser->get()), self->buffer);
                                    http_session->start();
                                }
                                self->server->do_accept();
                            });
                        return;
                    }
                    self->server->do_accept();
                });
        }
    };

    void do_accept() {
        acceptor_.async_accept(
            [this](beast::error_code ec, tcp::socket socket) {
                std::cout << "[HttpServer] do_accept lambda called" << std::endl;
                if (!ec) {
                    // Use SessionHolder to keep socket alive during async_read
                    std::make_shared<SessionHolder>(std::move(socket), message_bus_, this)->start();
                } else {
                    std::cerr << "[HttpServer] do_accept error: " << ec.message() << std::endl;
                    do_accept();
                }
            });
    }

    class HttpSession : public std::enable_shared_from_this<HttpSession> {
    public:
        HttpSession(tcp::socket socket, std::shared_ptr<lockfree::MessageBus> message_bus, std::shared_ptr<http::request<http::string_body>> req, std::shared_ptr<beast::flat_buffer> buffer)
            : socket_(std::move(socket))
            , buffer_(std::move(buffer))
            , req_(std::move(req))
            , res_()
            , message_bus_(message_bus) {
            std::cout << "[HttpSession] Constructor called" << std::endl;
        }

        void start() {
            std::cout << "[HttpSession] start() called" << std::endl;
            try {
                handle_request();
            } catch (const std::exception& e) {
                std::cerr << "[HttpSession] Exception in start: " << e.what() << std::endl;
            }
        }

    private:
        void handle_request() {
            std::cout << "[HttpSession] handle_request for target: " << req_->target() << std::endl;
            try {
                res_.version(req_->version());
                res_.keep_alive(false);
                res_.set(http::field::access_control_allow_origin, "*");
                res_.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
                res_.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");

                // Handle API endpoints
                if (req_->target().starts_with("/api/")) {
                    // Preflight CORS handling
                    if (req_->method() == http::verb::options) {
                        res_.result(http::status::no_content);
                        res_.set(http::field::content_type, "application/json");
                        res_.body() = "";
                        write_response();
                        return;
                    }
                    if (req_->target() == "/api/stats") {
                        handle_stats();
                    } else if (req_->target() == "/api/publish") {
                        handle_publish();
                    } else if (req_->target() == "/api/publish_bulk") {
                        handle_publish_bulk();
                    } else if (req_->target().starts_with("/api/processing_delay")) {
                        handle_processing_delay();
                    } else if (req_->target() == "/api/reset_counters") {
                        handle_reset_counters();
                    } else {
                        res_.result(http::status::not_found);
                        res_.set(http::field::content_type, "application/json");
                        res_.body() = json{{"error", "Not found"}}.dump();
                    }
                } else {
                    res_.result(http::status::not_found);
                    res_.set(http::field::content_type, "text/plain");
                    res_.body() = "Not found";
                }
                write_response();
            } catch (const std::exception& e) {
                std::cerr << "[HttpSession] Exception in handle_request: " << e.what() << std::endl;
            }
        }

        void handle_stats() {
            res_.result(http::status::ok);
            res_.set(http::field::content_type, "application/json");
            
            json stats = {
                {"buffer_size", message_bus_->get_size()},
                {"buffer_capacity", message_bus_->get_capacity()},
                {"is_full", message_bus_->is_full()},
                {"is_empty", message_bus_->is_empty()},
                {"published_count", message_bus_->get_published_count()},
                {"processed_count", message_bus_->get_processed_count()},
                {"dropped_count", message_bus_->get_dropped_count()},
                {"processing_delay_ms", message_bus_->get_processing_delay_ms()}
            };
            
            res_.body() = stats.dump();
            res_.prepare_payload();
        }

        void handle_publish() {
            if (req_->method() != http::verb::post) {
                res_.result(http::status::method_not_allowed);
                res_.set(http::field::content_type, "application/json");
                res_.body() = json{{"error", "Method not allowed"}}.dump();
                res_.prepare_payload();
                return;
            }

            try {
                std::cout << "[HttpSession] handle_publish: raw body length=" << req_->body().size() << std::endl;
                json data = json::parse(req_->body());
                std::cout << "[HttpSession] handle_publish: parsed JSON ok" << std::endl;
                
                lockfree::MarketData market_data;
                strncpy(market_data.symbol, data["symbol"].get<std::string>().c_str(), sizeof(market_data.symbol) - 1);
                market_data.symbol[sizeof(market_data.symbol) - 1] = '\0';
                market_data.price = data["price"].get<double>();
                market_data.volume = data["volume"].get<double>();
                market_data.timestamp = time_point_to_int64(std::chrono::system_clock::now());
                strncpy(market_data.source, "HTTP_API", sizeof(market_data.source) - 1);
                market_data.source[sizeof(market_data.source) - 1] = '\0';

                if (message_bus_->publish("market_data", market_data)) {
                    std::cout << "[HttpSession] handle_publish: publish success" << std::endl;
                    res_.result(http::status::ok);
                    res_.set(http::field::content_type, "application/json");
                    res_.body() = json{{"status", "success"}}.dump();
                } else {
                    std::cout << "[HttpSession] handle_publish: buffer full" << std::endl;
                    res_.result(http::status::service_unavailable);
                    res_.set(http::field::content_type, "application/json");
                    res_.body() = json{{"error", "Buffer full"}}.dump();
                }
                res_.prepare_payload();
            } catch (const std::exception& e) {
                res_.result(http::status::bad_request);
                res_.set(http::field::content_type, "application/json");
                res_.body() = json{{"error", e.what()}}.dump();
                res_.prepare_payload();
                std::cerr << "[HttpSession] handle_publish exception: " << e.what() << std::endl;
            }
        }

        void handle_publish_bulk() {
            if (req_->method() != http::verb::post) {
                res_.result(http::status::method_not_allowed);
                res_.set(http::field::content_type, "application/json");
                res_.body() = json{{"error", "Method not allowed"}}.dump();
                res_.prepare_payload();
                return;
            }
            try {
                json data = json::parse(req_->body());
                const int count = data.value("count", 100);
                const std::string symbol = data.value("symbol", std::string("BULK"));
                const double base_price = data.value("price", 100.0);
                const double base_volume = data.value("volume", 1.0);
                int success = 0;
                int dropped = 0;
                for (int i = 0; i < count; ++i) {
                    lockfree::MarketData md{};
                    strncpy(md.symbol, symbol.c_str(), sizeof(md.symbol) - 1);
                    // Add small jitter to price and volume for realism
                    double jitter = ((std::rand() % 201) - 100) / 10000.0; // +/-1.00%
                    md.price = base_price * (1.0 + jitter);
                    md.volume = std::max(1.0, base_volume + (std::rand() % 5));
                    md.timestamp = time_point_to_int64(std::chrono::system_clock::now());
                    strncpy(md.source, "HTTP_API", sizeof(md.source) - 1);
                    if (message_bus_->publish("market_data", md)) success++; else dropped++;
                }
                res_.result(http::status::ok);
                res_.set(http::field::content_type, "application/json");
                res_.body() = json{{"status", "success"}, {"published", success}, {"dropped", dropped}}.dump();
                res_.prepare_payload();
            } catch (const std::exception& e) {
                res_.result(http::status::bad_request);
                res_.set(http::field::content_type, "application/json");
                res_.body() = json{{"error", e.what()}}.dump();
                res_.prepare_payload();
            }
        }

        void handle_processing_delay() {
            try {
                // /api/processing_delay?ms=100
                auto target = std::string(req_->target());
                auto pos = target.find("ms=");
                int ms = 0;
                if (pos != std::string::npos) {
                    ms = std::stoi(target.substr(pos + 3));
                }
                message_bus_->set_processing_delay_ms(ms);
                res_.result(http::status::ok);
                res_.set(http::field::content_type, "application/json");
                res_.body() = json{{"status", "ok"}, {"processing_delay_ms", ms}}.dump();
                res_.prepare_payload();
            } catch (...) {
                res_.result(http::status::bad_request);
                res_.set(http::field::content_type, "application/json");
                res_.body() = json{{"error", "invalid parameter"}}.dump();
                res_.prepare_payload();
            }
        }

        void handle_reset_counters() {
            message_bus_->reset_counters();
            res_.result(http::status::ok);
            res_.set(http::field::content_type, "application/json");
            json payload = {
                {"status", "ok"},
                {"published_count", message_bus_->get_published_count()},
                {"processed_count", message_bus_->get_processed_count()},
                {"dropped_count", message_bus_->get_dropped_count()}
            };
            res_.body() = payload.dump();
            res_.prepare_payload();
        }

        void write_response() {
            auto self = shared_from_this();
            std::cout << "[HttpSession] write_response called" << std::endl;
            try {
                // Ensure content-length is set for string bodies
                if (res_.need_eof()) {
                    res_.prepare_payload();
                }
                http::async_write(socket_, res_,
                    [self](beast::error_code ec, std::size_t) {
                        std::cout << "[HttpSession] async_write completed, shutting down socket" << std::endl;
                        self->socket_.shutdown(tcp::socket::shutdown_both, ec);
                    });
            } catch (const std::exception& e) {
                std::cerr << "[HttpSession] Exception in write_response: " << e.what() << std::endl;
            }
        }

        tcp::socket socket_;
        std::shared_ptr<beast::flat_buffer> buffer_;
        std::shared_ptr<http::request<http::string_body>> req_;
        http::response<http::string_body> res_;
        std::shared_ptr<lockfree::MessageBus> message_bus_;
    };

    tcp::acceptor acceptor_;
    std::shared_ptr<lockfree::MessageBus> message_bus_;
};

int main() {
    std::cout << "[main] Starting main()" << std::endl;
    try {
        std::cout << "[main] Creating MessageBus..." << std::endl;
        auto message_bus = std::make_shared<lockfree::MessageBus>("market_data_bus", 256 * 1024);
        std::cout << "[main] MessageBus created" << std::endl;

        std::cout << "[main] Starting message processing thread..." << std::endl;
        std::atomic<bool> should_continue{true};
        std::thread message_thread([&message_bus, &should_continue]() {
            message_bus->process_messages(should_continue);
        });
        std::cout << "[main] Message processing thread started" << std::endl;

        std::cout << "[main] Creating io_context and HttpServer..." << std::endl;
        net::io_context ioc;
        // Keep io_context alive even when there are brief gaps with no pending async operations
        auto work_guard = net::make_work_guard(ioc);
        HttpServer server(ioc, "0.0.0.0", 8080, message_bus);
        std::cout << "[main] HttpServer created" << std::endl;
        server.start();

        std::cout << "Server started on port 8080" << std::endl;

        // Run the I/O service
        ioc.run();
        std::cout << "[main] io_context finished running" << std::endl;

        // Cleanup
        should_continue = false;
        if (message_thread.joinable()) {
            message_thread.join();
        }
        std::cout << "[main] Exiting main() normally" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[main] Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
} 