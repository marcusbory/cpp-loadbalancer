// Simple round-robin HTTP load balancer for localhost backends.
//
// Usage:
//   1. Start backends (separate terminals):
//        ./backend_server 9001 backend-1
//        ./backend_server 9002 backend-2
//        ./backend_server 9003 backend-3
//   2. Start the load balancer:
//        ./load_balancer
//   3. Send requests through the balancer:
//        curl http://127.0.0.1:8080/
//
// The balancer listens on port 8080 and forwards to 9001/9002/9003 in round-robin order.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace load_balancer
{

    constexpr int kListenPort = 8080;
    constexpr int kBacklog = 16;
    constexpr int kBufferSize = 4096;

    struct Backend
    {
        std::string host;
        int port;
    };

    // Round-robin state: pick the next backend on each request.
    size_t g_next_backend = 0;

    const std::vector<Backend> kBackends = {
        {"127.0.0.1", 9001},
        {"127.0.0.1", 9002},
        {"127.0.0.1", 9003},
    };

    bool sendAll(int fd, const char *data, size_t length)
    {
        while (length > 0)
        {
            ssize_t sent = send(fd, data, length, 0);
            if (sent <= 0)
            {
                return false;
            }
            data += sent;
            length -= static_cast<size_t>(sent);
        }
        return true;
    }

    bool relayUntilClose(int from_fd, int to_fd)
    {
        char buffer[kBufferSize];
        while (true)
        {
            const ssize_t bytes_read = recv(from_fd, buffer, sizeof(buffer), 0);
            if (bytes_read == 0)
            {
                return true;
            }
            if (bytes_read < 0)
            {
                return false;
            }
            if (!sendAll(to_fd, buffer, static_cast<size_t>(bytes_read)))
            {
                return false;
            }
        }
    }

    // Read one HTTP request from the client (headers, plus any body declared by Content-Length).
    bool readHttpRequest(int client_fd, std::string *request)
    {
        request->clear();
        char buffer[kBufferSize];

        while (request->find("\r\n\r\n") == std::string::npos)
        {
            const ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes_read <= 0)
            {
                return false;
            }
            request->append(buffer, static_cast<size_t>(bytes_read));
        }

        const size_t header_end = request->find("\r\n\r\n");
        const std::string headers = request->substr(0, header_end);

        // Optional: read request body if Content-Length is present.
        size_t content_length = 0;
        const std::string marker = "Content-Length:";
        const size_t marker_pos = headers.find(marker);
        if (marker_pos != std::string::npos)
        {
            content_length = static_cast<size_t>(std::stoul(headers.substr(marker_pos + marker.size())));
        }

        const size_t body_received = request->size() - (header_end + 4);
        while (body_received + content_length > request->size())
        {
            const ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes_read <= 0)
            {
                return false;
            }
            request->append(buffer, static_cast<size_t>(bytes_read));
        }

        return true;
    }

    int connectToBackend(const Backend &backend)
    {
        const int backend_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (backend_fd < 0)
        {
            return -1;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(backend.port));

        if (inet_pton(AF_INET, backend.host.c_str(), &address.sin_addr) <= 0)
        {
            close(backend_fd);
            return -1;
        }

        if (connect(backend_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
        {
            close(backend_fd);
            return -1;
        }

        return backend_fd;
    }

    // Try backends starting at `start_index`, wrapping around on failure.
    int connectWithFailover(size_t start_index, Backend *chosen_backend)
    {
        for (size_t attempt = 0; attempt < kBackends.size(); ++attempt)
        {
            const size_t index = (start_index + attempt) % kBackends.size();
            const Backend &candidate = kBackends[index];

            const int backend_fd = connectToBackend(candidate);
            if (backend_fd >= 0)
            {
                *chosen_backend = candidate;
                g_next_backend = (index + 1) % kBackends.size();
                return backend_fd;
            }

            std::cerr << "Backend " << candidate.host << ':' << candidate.port
                      << " unavailable: " << std::strerror(errno) << '\n';
        }
        return -1;
    }

    void sendErrorResponse(int client_fd, int status_code, const std::string &message)
    {
        const std::string body = message + "\n";
        const std::string response =
            "HTTP/1.1 " + std::to_string(status_code) + " Service Unavailable\r\n"
                                                        "Content-Type: text/plain\r\n"
                                                        "Connection: close\r\n"
                                                        "Content-Length: " +
            std::to_string(body.size()) + "\r\n\r\n" + body;
        sendAll(client_fd, response.c_str(), response.size());
    }

    void handleClient(int client_fd)
    {
        const size_t start_index = g_next_backend;
        Backend chosen_backend{};
        const int backend_fd = connectWithFailover(start_index, &chosen_backend);

        if (backend_fd < 0)
        {
            sendErrorResponse(client_fd, 503, "No healthy backends available");
            close(client_fd);
            return;
        }

        std::cout << "Forwarding request to " << chosen_backend.host << ':'
                  << chosen_backend.port << '\n';

        std::string request;
        if (!readHttpRequest(client_fd, &request))
        {
            std::cerr << "Failed to read client request\n";
            close(client_fd);
            close(backend_fd);
            return;
        }

        if (!sendAll(backend_fd, request.c_str(), request.size()))
        {
            std::cerr << "Failed while forwarding client request\n";
            close(client_fd);
            close(backend_fd);
            return;
        }

        shutdown(backend_fd, SHUT_WR);

        if (!relayUntilClose(backend_fd, client_fd))
        {
            std::cerr << "Failed while forwarding backend response\n";
        }

        close(client_fd);
        close(backend_fd);
    }

} // namespace

int main()
{
    const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(load_balancer::kListenPort);

    if (bind(server_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, load_balancer::kBacklog) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    std::cout << "Load balancer listening on http://127.0.0.1:" << load_balancer::kListenPort << '\n';
    std::cout << "Backends:\n";
    for (const load_balancer::Backend &backend : load_balancer::kBackends)
    {
        std::cout << "  - http://" << backend.host << ':' << backend.port << '\n';
    }

    while (true)
    {
        const int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }
        load_balancer::handleClient(client_fd);
    }
}
