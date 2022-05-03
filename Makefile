CFLAGS = -Wall -Iinclude

SERVER_PORT = 12345
SERVER_IP   = 127.0.0.1
CLIENT_IP   = # Complete manually (/by tester)

all: server subscriber

# Compile `server.c`
server: server.c -lm

# Compile `subscriber.c`
subscriber: subscriber.c

.PHONY: clean run_server run_subscriber

# Run the server
run_server:
	./server ${SERVER_PORT}

# Run the client
run_subscriber:
	./subscriber ${CLIENT_IP} ${SERVER_IP} ${SERVER_PORT}

clean:
	rm -f server subscriber
