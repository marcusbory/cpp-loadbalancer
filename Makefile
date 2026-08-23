CXX = c++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -O2

.PHONY: all clean run-backends run-lb test

all: load_balancer backend_server

load_balancer: load_balancer.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

backend_server: backend_server.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f load_balancer backend_server

# Helper targets for quick local experiments.
run-backends:
	@echo "Run these in separate terminals:"
	@echo "  ./backend_server 9001 backend-1"
	@echo "  ./backend_server 9002 backend-2"
	@echo "  ./backend_server 9003 backend-3"

run-lb:
	./load_balancer

test:
	@echo "Sending 6 requests through the load balancer..."
	@for i in 1 2 3 4 5 6; do \
		echo "--- request $$i ---"; \
		curl -s http://127.0.0.1:8080/; \
	done
