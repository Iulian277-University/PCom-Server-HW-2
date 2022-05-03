#include "utils.h"

// Tell if the server will send repsonses
// back to the client if an error occurs
extern bool verbose;

// List of TCP subscribers
extern Client **subscribers;
extern size_t subs_curr_cap;
extern size_t subs_max_cap;

// General usage buffer
char buffer[BUFF_LEN];


void respose_with_err_msg(const char *buffer, int client_sock)
{
    // Create a new TCP message
    TCP_msg *tcp_msg = (TCP_msg *) calloc(1, sizeof(TCP_msg));
    DIE(tcp_msg == NULL, "[ERROR]: Allocation error!\n");
    tcp_msg->from_server = true;

    // First, send the size of the message
    sprintf(tcp_msg->size, "%lu", sizeof(TCP_msg));
    send(client_sock, tcp_msg->size, MAX_DIGITS_TCP_MSG_LEN, 0);

    // Send the actual message
    strcpy(tcp_msg->udp_msg.payload, buffer);
    send(client_sock, (char *) tcp_msg, atoi(tcp_msg->size), 0);
}


Client *get_client_by_id(const char *id)
{
    for (int i = 0; i < subs_curr_cap; ++i)
        if (strcmp(subscribers[i]->id, id) == 0)
            return subscribers[i];

    return NULL;
}


Client *get_client_by_socket(int sock)
{
    for (int i = 0; i < subs_curr_cap; ++i)
        if (subscribers[i]->socket == sock)
            return subscribers[i];

    return NULL;
}


void add_new_client(const char *id, int req_tcp_socket)
{
    // Create a new client
    Client *client = (Client *) calloc(1, sizeof(Client));
    DIE(client == NULL, "[ERROR]: Allocation error!\n");

    // Complete the client's fields
    strcpy(client->id, id);
    client->socket          = req_tcp_socket;
    client->connected       = true;
    client->num_of_topics   = 0;
    client->max_topics      = INITIAL_MAX_TOPICS;

    // Alloc memory for client's topics
    client->topics = (Topic **) calloc(client->max_topics, sizeof(Topic *));
    DIE(client->topics == NULL, "[ERROR]: Allocation error!\n");

    // Reallocate memory for the list of `subscribers` if needed
    if (subs_curr_cap == subs_max_cap)
    {
        subs_max_cap *= 2;
        subscribers   = (Client **) realloc(subscribers, subs_max_cap * sizeof(Client *));
        DIE(subscribers == NULL, "[ERROR]: Reallocation error!\n");
    }

    // Add the new `client` in the `subscribers` list
    subscribers[subs_curr_cap++] = client;
}


void reconnect_old_sub(Client *client, int req_tcp_socket)
{
    // Update the fields of the `client`
    client->socket      = req_tcp_socket;
    client->connected   = true;

    // Send all the stored messaged (from UDP clients) while
    // the TCP `client` was disconnected from the server
    for (int i = 0; i < client->num_of_topics; ++i)
    {
        for (int j = 0; j < client->topics[i]->num_of_tcps; ++j)
        {
            TCP_msg *msg = client->topics[i]->tcps[j];
            // Send the size of the `msg` first and then the actual `msg`
            send(req_tcp_socket, msg->size, MAX_DIGITS_TCP_MSG_LEN, 0);
            send(req_tcp_socket, msg, atoi(msg->size), 0);
        }
    }

    // Deallocate the memory for stored TCP messages 
    for (int i = 0; i < client->num_of_topics; ++i)
    {
        for (int j = 0; j < client->topics[i]->num_of_tcps; ++j)
            free(client->topics[i]->tcps[j]);

        free(client->topics[i]->tcps);
        client->topics[i]->num_of_tcps  = 0;
        client->topics[i]->max_tcps     = INITIAL_MAX_TOPICS;
    }
}


void disconnect_client(int sock)
{
    // Iterate through the list of subscribers
    for (int i = 0; i < subs_curr_cap; ++i)
    {
        if (subscribers[i]->socket == sock)
        {
            Client *client = subscribers[i];
            printf("Client %s disconnected.\n", client->id);
            
            // Disconnect the client
            client->connected = false;

            // Alloc space for storing messages while the client will be disconnected
            for (int j = 0; j < client->num_of_topics; ++j)
            {
                if (client->topics[j]->sf == 1)
                {
                    client->topics[j]->tcps = (TCP_msg **) calloc(INITIAL_MAX_TCPS, sizeof(TCP_msg *));
                    DIE(client->topics[j]->tcps == NULL, "[ERROR]: Allocation error!\n");
                    client->topics[j]->num_of_tcps  = 0;
                    client->topics[j]->max_tcps     = INITIAL_MAX_TCPS; 
                }
            }

            // Close the socket
            close(sock);
            return;
        }
    }
}


int check_already_sub_topic(Action *action, int sock)
{
    // `client` shouldn't be NULL if the implementation
    // is correct, but for safety, we can check this
    Client *client = get_client_by_socket(sock);
    if (client == NULL)
        return 1;

    // Clear the buffer
    memset(buffer, 0, BUFF_LEN);

    // Iterate through client's subscribed topics
    for (int i = 0; i < client->num_of_topics; ++i)
    {
        if ((strcmp(client->topics[i]->name, action->topic) == 0))
        {
            // Check if the client wants to change the `SF` or re-subscribe with the same `SF`
            if (client->topics[i]->sf == action->sf && client->topics[i]->subscribed)
                sprintf(buffer, "User %s already subscribed to topic %s.\n", client->id, client->topics[i]->name);
            else if (client->topics[i]->sf != action->sf && client->topics[i]->subscribed)
            {
                if (action->sf != 0 && action->sf != 1)
                    strcpy(buffer, "SF should be 0 or 1.\n");
                else
                {
                    client->topics[i]->sf = action->sf;
                    sprintf(buffer, "User %s changed the SF of topic %s to %d.\n", client->id, client->topics[i]->name, client->topics[i]->sf);
                }
            }

            // Send a repsonse back to the client if `verbose` is enabled
            if (verbose)
                respose_with_err_msg(buffer, client->socket);

            return 1;
        }
    }
    
    return 0;
}


void subscribe_to_topic(Action *action, int sock)
{
    if (check_already_sub_topic(action, sock))
        return;

    // Chech `SF` value
    if (action->sf != 0 && action->sf != 1)
    {
        memset(buffer, 0, BUFF_LEN);
        strcpy(buffer, "SF should be 0 or 1.\n");
        if (verbose)
            respose_with_err_msg(buffer, sock);
        return;
    }

    // Create a new topic for this client identified by socket `sock`
    Topic *topic = (Topic *) calloc(1, sizeof(Topic));
    DIE(topic == NULL, "[ERROR]: Allocation error!\n");

    // Set topic's fields
    strcpy(topic->name, action->topic);
    topic->subscribed    = true;
    topic->sf           = action->sf;
    topic->tcps         = NULL;
    topic->num_of_tcps  = 0;
    topic->max_tcps     = INITIAL_MAX_TCPS;


    // Iterate through subscribers
    for (int i = 0; i < subs_curr_cap; ++i)
    {
        // Found the subscriber
        if (subscribers[i]->socket == sock)
        {

            if (subscribers[i]->num_of_topics == subscribers[i]->max_topics)
            {
                // Reallocate memory for topics list
                subscribers[i]->max_topics     *= 2;
                subscribers[i]->topics          = (Topic **) realloc(subscribers[i]->topics, subscribers[i]->max_topics * sizeof(Topic *));
                DIE(subscribers[i]->topics == NULL, "[ERROR]: Reallocation error!\n");
            }

            // Add the topic to client
            subscribers[i]->topics[subscribers[i]->num_of_topics++] = topic;
            return;
        }
    }
}


void unsubscribe_from_topic(Action *action, int sock)
{
    Client *client = get_client_by_socket(sock);
    // Iterate through the client's topics
    bool found_topic = false;
    for (int i = 0; i < client->num_of_topics; ++i)
    {
        if ((strcmp(client->topics[i]->name, action->topic) == 0) && (client->topics[i]->subscribed))
        {
            found_topic = true;
            client->topics[i]->subscribed = false;
            break;
        }
    }

    if (!found_topic && verbose)
    {
        memset(buffer, 0, BUFF_LEN);
        sprintf(buffer, "User %s isn't subscribe to topic %s, so he can't unsubscribe from it.\n", client->id, action->topic);
        respose_with_err_msg(buffer, sock);
    }
}


int convert_to_int(UDP_msg *udp_msg, TCP_msg *tcp_msg)
{
    // First byte from the payload is the `sign byte` (0/1)
    if (udp_msg->payload[0] > 1)
        return 0;

    // Complete the `type`
    tcp_msg->udp_msg.type = INT;
    
    // Let's check if the number is positive or negative
    ll sign = 1;
    if (udp_msg->payload[0])
        sign = -1;
    
    // Write the `payload` on the TCP msg (remove the sign byte from the payload)
    ll num_int = sign * ntohl(*(uint32_t *) (udp_msg->payload + 1));
    sprintf(tcp_msg->udp_msg.payload, "%lld", num_int);

    // Successfully completed the UDP msg payload
    return 1;
}


void convert_to_short_real(UDP_msg *udp_msg, TCP_msg *tcp_msg)
{
    // Complete the `type`
    tcp_msg->udp_msg.type = SHORT_REAL;

    // Write the `payload` on the TCP msg
    double num_real = 1.0 * (ntohs(*(uint16_t *) (udp_msg->payload))) / 100;
    sprintf(tcp_msg->udp_msg.payload, "%.2f", num_real);
}


int convert_to_float(UDP_msg *udp_msg, TCP_msg *tcp_msg)
{
    // First byte from the payload is the `sign byte` (0/1)
    if (udp_msg->payload[0] != 0 && udp_msg->payload[0] != 1)
        return 0;

    // Complete the `type`
    tcp_msg->udp_msg.type = FLOAT;

    // Let's check if the number is positive or negative
    double sign_float = 1;
    if (udp_msg->payload[0] == 1)
        sign_float = -1;
    
    // Write the `payload` on the TCP msg
    sign_float *= ntohl(*(uint32_t *) (udp_msg->payload + 1));
    sign_float /= pow(10, (uint8_t) udp_msg->payload[5]);
    sprintf(tcp_msg->udp_msg.payload, "%lf", sign_float);

    // Successfully completed the UDP msg payload
    return 1;
}


void convert_to_string(UDP_msg *udp_msg, TCP_msg *tcp_msg)
{
    // Complete the `type`
    tcp_msg->udp_msg.type = STRING;
    
    // Write the `payload` on the TCP msg
    strcpy(tcp_msg->udp_msg.payload, udp_msg->payload);
}


/*
    typedef struct udp_msg {
        char 	topic[TOPIC_SIZE];
        uint8_t type;                           // 0, 1, 2 or 3
        char 	payload[PAYLOAD_SIZE];
    } UDP_msg;


    typedef struct tcp_msg {
        char 	 size[MAX_DIGITS_TCP_MSG_LEN]; 	// Size of the message
        char	 ip[IP_LEN];					// `IP_CLIENT_UDP`
        uint16_t port;							// `PORT_CLIENT_UDP`
        UDP_msg udp_msg;                        // Received msg from the UDP client with IP `ip` and PORT `port`
    } TCP_msg;
*/
TCP_msg *UDP_to_TCP(UDP_msg *udp_msg, struct sockaddr_in udp_addr)
{
    if (udp_msg->type < 0 || udp_msg->type > 3)
        return NULL;

    // Create a new TCP message
    TCP_msg *tcp_msg = (TCP_msg *) calloc(1, sizeof(TCP_msg));
    DIE(tcp_msg == NULL, "[ERROR]: Allocation error!\n");

    // Set the `size`, `ip`, `port` and `sever_msg` fields of the TCP msg
    sprintf(tcp_msg->size, "%lu", sizeof(TCP_msg));
    strcpy(tcp_msg->ip, inet_ntoa(udp_addr.sin_addr));
    tcp_msg->port = ntohs(udp_addr.sin_port);
    tcp_msg->from_server = false;

    // Complete the `topic`
    strncpy(tcp_msg->udp_msg.topic, udp_msg->topic, TOPIC_SIZE);
    tcp_msg->udp_msg.topic[TOPIC_SIZE] = '\0';

    // We can have one of the following types (0 - INT, 1 - SHORT_REAL, 2 - FLOAT, 3 - STRING)
    switch (udp_msg->type)
    {
        case 0:
            if (convert_to_int(udp_msg, tcp_msg) == 0)
                return NULL;
            break;

        case 1:
            convert_to_short_real(udp_msg, tcp_msg);
            break;

        case 2:
            if (convert_to_float(udp_msg, tcp_msg) == 0)
                return NULL;
            break;

        case 3:
            convert_to_string(udp_msg, tcp_msg);
            break;
    }

    return tcp_msg;
}


void send_tcp_msg_to_conn_client(Client *client, TCP_msg *tcp_msg)
{
    // First, send the size of the message
    send(client->socket, tcp_msg->size, MAX_DIGITS_TCP_MSG_LEN, 0);

    // Send the actual message
    send(client->socket, (char *) tcp_msg, atoi(tcp_msg->size), 0);
}


void store_tcp_msg_to_unconn_client(Client *client, int topic_idx, TCP_msg *tcp_msg)
{
    // Reallocate memory for client TCP messages if needed
    if (client->topics[topic_idx]->num_of_tcps == client->topics[topic_idx]->max_tcps)
    {
        // Double the capacity
        client->topics[topic_idx]->max_tcps *= 2;
        client->topics[topic_idx]->tcps      = (TCP_msg **) realloc(client->topics[topic_idx]->tcps,
                                                                    client->topics[topic_idx]->max_tcps * sizeof(TCP_msg *));
        DIE(client->topics[topic_idx]->tcps == NULL, "[ERROR]: Reallocation error!\n");
    }

    // Add the msg to the client's list of TCP messages (when it's disconnected)
    client->topics[topic_idx]->tcps[client->topics[topic_idx]->num_of_tcps++] = tcp_msg;
}


void send_tcp_msg(TCP_msg *tcp_msg)
{
    // Iterate through subscribers
    for (int i = 0; i < subs_curr_cap; ++i)
    {
        // Iterate through each subscriber's topic
        for (int j = 0; j < subscribers[i]->num_of_topics; ++j)
        {
            // Found the topic
            if (strcmp(subscribers[i]->topics[j]->name, tcp_msg->udp_msg.topic) == 0)
            {
                if (subscribers[i]->connected == true)
                {
                    send_tcp_msg_to_conn_client(subscribers[i], tcp_msg);
                    break;
                }
                else if (subscribers[i]->connected == false && subscribers[i]->topics[j]->sf == 1)
                {
                    store_tcp_msg_to_unconn_client(subscribers[i], j, tcp_msg);
                    break;
                }
            }
        }
    }
}


void dealloc_memory()
{
    // Iterate through each subscriber
    for (int i = 0; i < subs_curr_cap; ++i)
    {
        // Iterate through each topic
        for (int j = 0; j < subscribers[i]->num_of_topics; ++j)
        {
            // Free each stored TCP msg
            for (int k = 0; k < subscribers[i]->topics[j]->num_of_tcps; ++k)
                free(subscribers[i]->topics[j]->tcps[k]);
            free (subscribers[i]->topics[j]->tcps);
            free(subscribers[i]->topics[j]);
        }
        free(subscribers[i]->topics);
        free(subscribers[i]);
    }
    free(subscribers);
}


void close_sockets(int fdmax, fd_set read_fds)
{
    // Close all opened sockets
    for (int i = 2; i <= fdmax; ++i)
        if (FD_ISSET(i, &read_fds))
            close(i);
}
