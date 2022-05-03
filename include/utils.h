#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define ll long long

/* MACRO for stopping the execution of the program in case of an error */
#define DIE(assertion, call_description)	\
	do {									\
		if (assertion) {					\
			fprintf(stderr, "(%s, %d): ",	\
					__FILE__, __LINE__);	\
			perror(call_description);		\
			exit(EXIT_FAILURE);				\
		}									\
	} while(0)


/* Don't let the compiler to add paddings */
#pragma pack(1)


/* UDP messages constants (+1 for the null terminator) */
#define BUFF_LEN		50 + 1 + 1500 + 1   // Maximum size of a `data chunk`
#define TOPIC_SIZE		49 			  + 1	// Maximum size of a `topic name`
#define PAYLOAD_SIZE	1500 		  + 1	// Maximum size of a `payload` 

/* Structure of an UDP message */
typedef struct udp_msg {
	char 	topic[TOPIC_SIZE];
	uint8_t type;
	char 	payload[PAYLOAD_SIZE];
} UDP_msg;


/* Types of the TCP message */
typedef enum {	
	INT,
	SHORT_REAL,
	FLOAT,
	STRING
} msg_type;

/* TCP messages constants (+1 for the null terminator) */
#define IP_LEN					3 * 4 + 3 				+ 1	// 4 groups of 3 + 3 dots + '\0' (e.g: "127.0.0.1")
#define MAX_DIGITS_TCP_MSG_LEN 	9 		  				+ 1
#define TCP_MSG_SIZE			sizeof(TCP_msg) 		+ 1

/* Structure of a TCP message */
/*`Size of the message` allows me to receive the packet in chunks (TCP is stream oriented)
 * -> First I will send the `size` and then the actual message
 * -> When  I will receive the message, firstly I will get the `size` and then read packets, until I get `size` dim
 */
typedef struct tcp_msg {
	char 	 size[MAX_DIGITS_TCP_MSG_LEN]; 	// Size of the message
	char	 ip[IP_LEN];					// `IP_CLIENT_UDP`
	uint16_t port;							// `PORT_CLIENT_UDP`

	bool 	 from_server;					// Tell if it is an repsonse (err msg) from the server (not from the UDP clients)
	UDP_msg  udp_msg;						// Received msg from the UDP client with IP `ip` and PORT `port`
} TCP_msg;


#define INITIAL_MAX_TCPS	10		// Initial number of stored TCP messages for a client
	
/* Structure of a Topic */
typedef struct topic {
	char 	name[TOPIC_SIZE];		// Name of the topic
	uint8_t sf;						// Store-and-forward (0 - disabled | 1 - enabled)
	bool 	subscribed;				// Tell if the client is still subscribed to this topic

	int 	num_of_tcps;			// Current number of stored TCP messages
	int 	max_tcps;				// Maximum number of stored TCP messages
	TCP_msg **tcps;					// List of TCP messages for storing packets when a client disconnects
} Topic;


/* Clients constants (+1 for the null terminator) */
#define ID_CLIENT_LEN		10 + 1	// Maximum length of an `ID client`
#define INITIAL_MAX_TOPICS 	10		// Initial capacity of `topics` list

/* Structure of a TCP Client */
typedef struct client {
	char 	id[ID_CLIENT_LEN];	// ID of the client
	bool 	connected;			// Client is/isn't connected to the server
	int 	socket;				// Socket through which the client is connected to the server

	int 	num_of_topics;		// Current number of topics at which the client is subscribed
	int 	max_topics;			// Maximum number of topics at which a client can subscribe 
	Topic 	**topics;			// List of subscribed topics
} Client;


/* Actions constants */
#define ACTION_TYPE_LEN 	50
#define SUBSCRIBE_ACTION 	"subscribe"
#define UNSUBSCRIBE_ACTION 	"unsubscribe"
#define EXIT_ACTION 		"exit"

/* Structure of an Action */
/*
 * e.g.: subscribe   <TOPIC> <SF>
 * e.g.: unsubscribe <TOPIC>
 * e.g.: exit 
 */
typedef struct action {
	char 	type[ACTION_TYPE_LEN];
	char 	topic[TOPIC_SIZE];
	uint8_t sf;
} Action;


#define BACKLOG					10		// Maximum number of `waiting clients`
#define INITIAL_CAP_SUBS_LIST	10		// Initial capacity of `subscribers` list
#define VERBOSE_TRUE			"true"  // Print additional messages


/* Restore to the original padding settings of the compiler  */
#pragma pack()


/* Function definitions */

/* Send a `TCP_msg` with payload `buffer` to the client with socket `client_sockt`*/
void 	 respose_with_err_msg(const char *buffer, int client_sock);

/**
 * Return a pointer to a client, given a client ID `id`
 * (or NULL if client with ID `id` not found)
*/
Client  *get_client_by_id(const char *id);

/**
 * Return a pointer to a client, given a client connection socket `sock`
 * (or NULL if client with socket `sock` not found)
*/
Client  *get_client_by_socket(int sock);

/* Add a new client the list of subscribers */
void	 add_new_client(const char *id, int req_tcp_socket);

/* Reconnect an old subscriber */
void 	 reconnect_old_sub(Client *client, int req_tcp_socket);

/* Disconnect a client with socket `sock` from the server */
void 	 disconnect_client(int sock);

/* Check if a client is already subscribed to a given topic */
int 	 check_already_sub_topic(Action *action, int sock);

/* Subscribe a client to a given topic */
void 	 subscribe_to_topic(Action *action, int sock);

/* Unsubscribe a client from a given topic */
void 	 unsubscribe_from_topic(Action *action, int sock);

/* Convert and UDP `INT` payload to TCP payload */
int 	 convert_to_int(UDP_msg *udp_msg, TCP_msg *tcp_msg);

/* Convert and UDP `SHORT-REAL` payload to TCP payload */
void 	 convert_to_short_real(UDP_msg *udp_msg, TCP_msg *tcp_msg);

/* Convert and UDP `FLOAT` payload to TCP payload */
int 	 convert_to_float(UDP_msg *udp_msg, TCP_msg *tcp_msg);

/* Convert and UDP `STRING` payload to TCP payload */
void 	 convert_to_string(UDP_msg *udp_msg, TCP_msg *tcp_msg);

/* Convert an UDP message to a TCP message */
TCP_msg *UDP_to_TCP(UDP_msg *udp_msg, struct sockaddr_in udp_addr);

/* Send a TCP message to a connected client */
void 	 send_tcp_msg_to_conn_client(Client *client, TCP_msg *tcp_msg);

/* Store a TCP message for a client (when client set SF = 1) */
void 	 store_tcp_msg_to_unconn_client(Client *client, int topic_idx, TCP_msg *tcp_msg);

/* Send a TCP message to all clients subscribed to a specific `topic` (written in the `tcp_msg` structure) */
void 	 send_tcp_msg(TCP_msg *tcp_msg);

/* Free the allocated memory */
void 	 dealloc_memory();

/* Close all opened sockets */
void 	 close_sockets(int fdmax, fd_set read_fds);

#endif
