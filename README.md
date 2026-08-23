# Load Balancer Project in C++

This project is a simple C++ TCP server + L7 load balancer.

## Backend Server TODO

[x] Setup
[ ] Add varying loads to server

## Load Balancer TODO

[x] Setup + Failover
[ ] Health Checks

Algorithms:
[ ] Weighted Round Robin 
[ ] Least Connections
[ ] Concurrent handling (`std::thread`)
[ ] Sticky sessions (same client, same backend)