.PHONY: build
build:
	gcc -I/usr/local/include -g -o bin/dashboard-monitor src/c/dashboard-monitor.c -lzmq -lpthread
	gcc -I/usr/local/include -g -o bin/dashboard-server src/c/dashboard-server.c -lzmq -lpthread
