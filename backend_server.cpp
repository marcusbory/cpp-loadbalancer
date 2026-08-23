// Simple HTTP backend for load-balancer practice.
//
// Usage: ./backend_server <port> <name>
// Example: ./backend_server 9001 backend-1
//
// This program is a minimal TCP server. It listens on a port, accepts one
// connection at a time, reads a single HTTP request, and sends back a plain-
// text response. There are no third-party libraries — only the C++ standard
// library and POSIX networking headers provided by macOS/Linux.
//
// Server lifecycle (same pattern used by most TCP servers):
//   socket()  -> create a network endpoint
//   bind()    -> attach it to a port (e.g. 9001)
//   listen()  -> mark it as ready to accept incoming connections
//   accept()  -> wait for a client and return a new socket for that client
//   recv/send -> read and write data on the client socket
//   close()   -> release the socket when done

// --- POSIX networking headers (part of the OS, not installed separately) ---

#include <arpa/inet.h>  // inet_pton, htons — convert IP addresses and port numbers
                        //   to the binary format the kernel expects
#include <netinet/in.h> // sockaddr_in — struct holding an IPv4 address + port
#include <sys/socket.h> // socket, bind, listen, accept, send, recv — core TCP API
#include <unistd.h>     // close — release a file descriptor (sockets are fd's)

// --- C++ standard library headers (<iostream>, <string>, etc.) ---

#include <cstdlib>  // std::atoi — parse command-line port number from text
#include <cstring>  // C string utilities (available if needed)
#include <iostream> // std::cout, std::cerr — print status and error messages
#include <string>   // std::string — convenient string type used throughout

namespace backend_server
{

    constexpr int kBacklog = 16; // Max queued connections waiting for accept()
    constexpr int kBufferSize = 4096;

    // send() may transfer fewer bytes than requested in one call.
    // Loop until the entire response is sent or an error occurs.
    bool sendAll(int fd, const std::string &data)
    {
        const char *ptr = data.c_str();
        size_t remaining = data.size();
        while (remaining > 0)
        {
            ssize_t sent = send(fd, ptr, remaining, 0);
            if (sent <= 0)
            {
                return false;
            }
            ptr += sent;
            remaining -= static_cast<size_t>(sent);
        }
        return true;
    }

    // Build a minimal HTTP/1.1 response by hand.
    // Real servers use libraries for this; here we format the raw text ourselves
    // so you can see exactly what goes over the wire.
    std::string buildResponse(const std::string &backend_name, int port)
    {
        std::string body = "Hello from " + backend_name + " (port " + std::to_string(port) + ")\n";
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: text/plain\r\n"
               "Connection: close\r\n"
               "Content-Length: " +
               std::to_string(body.size()) + "\r\n\r\n" + body;
    }

    // Handle one client connection: read its request, send a response, then close.
    // `client_fd` is the socket returned by accept() for this specific client.
    void handleClient(int client_fd, const std::string &backend_name, int port)
    {
        char buffer[kBufferSize];
        // Read one HTTP request; we ignore the path for this demo.
        recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        // Extra printing
        std::cout << "Received request: " << buffer << std::endl;

        const std::string response = buildResponse(backend_name, port);
        sendAll(client_fd, response);
        close(client_fd); // Tell the OS we are done with this connection
    }

} // namespace

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <port> <name>\n";
        std::cerr << "Example: " << argv[0] << " 9001 backend-1\n";
        return 1;
    }

    const int port = std::atoi(argv[1]);
    const std::string backend_name = argv[2];

    // AF_INET  = IPv4
    // SOCK_STREAM = reliable byte stream (TCP, as opposed to UDP)
    // Returns a file descriptor (fd), or -1 on error.
    const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket"); // perror prints the OS error message for errno
        return 1;
    }

    // Allow reusing the port immediately after the program restarts.
    // Without this, the OS may keep the port "busy" for a short time.
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Fill in the address we want to listen on.
    sockaddr_in address{};
    address.sin_family = AF_INET;                          // IPv4
    address.sin_addr.s_addr = INADDR_ANY;                  // Accept connections on any local interface
    address.sin_port = htons(static_cast<uint16_t>(port)); // Port in network byte order

    // Associate the socket with the address (host + port).
    if (bind(server_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    // Start listening; kBacklog is how many pending connections the kernel may queue.
    if (listen(server_fd, backend_server::kBacklog) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    std::cout << backend_name << " listening on http://127.0.0.1:" << port << '\n';

    // Main loop: wait for clients forever.
    while (true)
    {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        // Blocks until a client connects. Returns a new fd for talking to that client.
        const int client_fd =
            accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }
        backend_server::handleClient(client_fd, backend_name, port);
    }
}
