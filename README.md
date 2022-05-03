## Name: *Iulian-Marius Tăiatu (322CB)*
---

# General presentation

**In this documentation, I will talk about some general ideas and the workflow of the program.**

**I implemented an application of type client-server with UDP and TCP clients,
more exactly the behaviour of a server when it receives packets from clients and how the packets are send back and forth.**

**This implementation is written in C and uses the `socket.h` API.
Over the TCP protocol, I've defined some structures for manipulating the packets.**

# Structures used in the program

## `UDP message`

This structure contains 3 fields and it's used for storing messages from UDP clients.

```c
typedef struct udp_msg {
	char 	topic[TOPIC_SIZE];
	uint8_t type;
	char 	payload[PAYLOAD_SIZE];
} UDP_msg;
```

## `TCP message`

This structure is used for storing messages for TCP clients. First field is used for setting the size of the packet,
the next two are used for knowing the UDP client which provided this message. The field `from_server` is used for debugging
(when `verbose` is enabled) and the last field is an `UDP_msg` structure with the actual information.

```c
typedef struct tcp_msg {
	char 	 size[MAX_DIGITS_TCP_MSG_LEN];  // Size of the message
	char	 ip[IP_LEN];                    // `IP_CLIENT_UDP`
	uint16_t port;                          // `PORT_CLIENT_UDP`

	bool 	 from_server;                   // Tell if it is an repsonse (err msg) from the server
	UDP_msg  udp_msg;                       // Received msg from the UDP client with IP `ip` and PORT `port`
} TCP_msg;
```

## `Topic`

The following structure is used for storing a `topic` and the additional informations between a client and a topic.
It also contains a list of TCP messages for storing packets when a client disconnects from the server.

```c
typedef struct topic {
	char 	name[TOPIC_SIZE];       // Name of the topic
	uint8_t sf;                     // Store-and-forward (0 - disabled | 1 - enabled)
	bool 	subscribed;             // Tell if the client is still subscribed to this topic

	int 	num_of_tcps;            // Current number of stored TCP messages
	int 	max_tcps;               // Maximum number of stored TCP messages
	TCP_msg **tcps;                 // List of TCP messages for storing packets when a client disconnects
} Topic;
```

## `Client`

This structure is used for simulating a `client` with an ID, a current status (conn/unconn) and
a specific socket through which is connected to the server.
It also stores some informations about the topics the client is interested in (is subscribed to).

```c
typedef struct client {
	char 	id[ID_CLIENT_LEN];  // ID of the client
	bool 	connected;          // Client is/isn't connected to the server
	int 	socket;	            // Socket through which the client is connected to the server

	int 	num_of_topics;      // Current number of topics at which the client is subscribed
	int 	max_topics;         // Maximum number of topics at which a client can subscribe 
	Topic 	**topics;           // List of subscribed topics
} Client;
```

## `Action`

This last structure is used for storing informations about an `action`.
It contains the type of an action (subscribe/unsubscribe), topic to subscribe and the store-and-forward parameter (sf).

```c
typedef struct action {
	char 	type[ACTION_TYPE_LEN];
	char 	topic[TOPIC_SIZE];
	uint8_t sf;
} Action;
```

# Server functionality flow

- Get the `arguments`
- Disable `buffering`
- `Declare` sockets
    - UDP
    - TCP
    - Subscribers
- Declare and clear `sets for reading`
- `Initialize` sockets
- `Bind` sockets
- `Listen` on the TCP socket for clients
- Add UDP, TCP and STDIN sockets in the `read_fds` set
- Declare some message structures and initialize a list of `subscribers`
- Enter in a while loop waiting for messages/actions, iterating over sockets:
    - If `fd` is `STDIN`
        - If the command is `exit`, free the memory and close opened sockets (closing all client's connections).
    - If `fd` is `UDP`
        - Receive a packet from an UDP client, convert it to a TCP message and send the converted packet
		  to all clients which are subscribed to that topic.
    - If `fd` is TCP
        - Then, there is a connection request on the listener TCP socket.
        - Accept the client, disable the `Nagle's` algorithm and add the new client's socket in the `read_fds` set,
		  updating the `fdmax` (maximum value of opened fd sockets).
        - First, receive the client's ID (this is the first thing sent by the client to the server).
        - Then, check for ID duplicates (another client already has this ID)
            - If the client is a `new client`, then add it to the subscribers list
            - Otherwise, check if the client is an old subscriber trying to reconnect now,
			  or it's trying to connect for with an existing ID of another user.
    - Otherwise, then a connected client sent a command (action) to the server.
	  A client can send the actions `exit`, `subscribe <TOPIC> <SF>` or `unsubscribe <TOPIC>`.
	  After checking if the input is valid, the server resolves the command received from the user.

# Client functionality flow

- Get the `arguments`
- Disable `buffering`
- `Declare` server socket
- `Initialize` socket
- `Connect` to server
- Send client's ID
- Disable `Nagle's` algorithm
- Declare some structures
- Enter in a while loop waiting for actions:
    - If `fd` is `STDIN`
        - If the command is `exit`, close the connection
        - If the command is `subscribe` or `unsubscribe`, extract the arguments of the command,
		  create an `action` structure and send it to the server.
    - If `fd` is `sockfd`
        - First, the client receives the size of the packet.
        - Then it receives the actual message. Get the message in chunks (this is the way TCP works).
		  After that, convert the bitstream to `TCP_msg` structure and display the received message in the required format.
