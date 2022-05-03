#include "utils.c"

// Tell if the server will send repsonses
// back to the client if an error occurs
bool verbose = false;

// List of TCP subscribers
Client **subscribers;
size_t subs_curr_cap;
size_t subs_max_cap;


/* Print the correct usage of the program */
void usage(FILE *file, const char *exec_name)
{
    fprintf(file, "Usage: %s [SERVER_PORT] <VERBOSE>\n", exec_name);
    fprintf(file, "\t<VERBOSE> is an optional argument: true/false\n");
    exit(EXIT_FAILURE);
}


int main(int argc, char *argv[])
{
    /* Sanity check for arguments */
    if (argc < 2)
        usage(stderr, argv[0]);

    /* Check if the `verbose` argument was given */
    if (argc > 2)
    {
        if (strcmp(argv[2], VERBOSE_TRUE) == 0)
            verbose = true;
    }
    
    /* Convert the given port in `argv[1]` to integer */
    int port_number = atoi(argv[1]);
    DIE(port_number == 0, "[ERROR]: Couldn't convert the str `argv[1]` to int!\n");

    /* Disable buffering */
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    /* Declare sockets */
    struct sockaddr_in udp_addr;    // UDP socket
    struct sockaddr_in tcp_addr;    // TCP socket
    struct sockaddr_in sub_addr;    // Subscriber socket

    /* Declare sets */
    fd_set read_fds;    // Read set used in `select()`
    fd_set temp_fds;    // Temporary set

    /* Clear sets */
    FD_ZERO(&read_fds); // Clear the `read_fds` set
    FD_ZERO(&temp_fds); // Clear the `temp_fds` set

    /* Socket address lengths */
    socklen_t udp_len = sizeof(struct sockaddr_in);
    socklen_t tcp_len = sizeof(struct sockaddr_in);


    /* Create UDP socket */
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(udp_socket < 0, "[ERROR]: Couldn't create the UDP socket!\n");

    /* Create TCP socket (listener) */
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    DIE(tcp_socket < 0, "[ERROR]: Couldn't create the TCP socket!\n");


    /* Initialize UDP socket */
	memset((char *) &udp_addr, 0, sizeof(udp_addr));
	udp_addr.sin_family         = AF_INET;
	udp_addr.sin_port           = htons(port_number);
	udp_addr.sin_addr.s_addr    = INADDR_ANY;

    /* Initialize TCP socket */
	memset((char *) &tcp_addr, 0, sizeof(tcp_addr));
	tcp_addr.sin_family         = AF_INET;
	tcp_addr.sin_port           = htons(port_number);
	tcp_addr.sin_addr.s_addr    = INADDR_ANY;


    /* Bind UDP socket */
    int ret = bind(udp_socket, (struct sockaddr *) &udp_addr, sizeof(struct sockaddr));
    DIE(ret < 0, "[ERROR]: Couldn't bind the UDP socket!\n");

    /* Bind TCP socket */
    ret     = bind(tcp_socket, (struct sockaddr *) &tcp_addr, sizeof(struct sockaddr));
    DIE(ret < 0, "[ERROR]: Couldn't bind the TCP socket!\n");

    
    /* Listen on the TCP socket for clients */
    ret = listen(tcp_socket, BACKLOG);
    DIE(ret < 0, "[ERROR]: Couldn't listen on TCP socket!\n");


    /* Add UDP, TCP and STDIN sockets in the `read_fds` set */
    FD_SET(udp_socket,   &read_fds);
    FD_SET(tcp_socket,   &read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    int fdmax       = MAX(udp_socket, tcp_socket);
    int prev_fdmax  = fdmax;


    /* Declare UDP and TCP messages */
    UDP_msg *udp_msg;
    TCP_msg *tcp_msg;
    Action  *action;

    /* Initialize the `subscribers` list */
    subs_curr_cap   = 0;
    subs_max_cap    = INITIAL_CAP_SUBS_LIST;
    subscribers     = (Client **) calloc(subs_max_cap, sizeof(Client *));
    DIE(subscribers == NULL, "[ERROR]: Allocation error!\n");

    char buffer[BUFF_LEN];
    while (1)
    {
        temp_fds = read_fds;
        ret = select(fdmax + 1, &temp_fds, NULL, NULL, NULL);
        DIE(ret < 0, "[ERROR]: Couldn't select a socket!\n");

        /* Iterate through sockets */
        for (int i = 0; i <= fdmax; ++i)
        {
            if (!FD_ISSET(i, &temp_fds))
                continue;

            memset(buffer, 0, BUFF_LEN);
            if (i == STDIN_FILENO)
            {
                // STDIN fd (only `exit` command)
                fscanf(stdin, "%s", buffer);
                if (strcmp(buffer, EXIT_ACTION) == 0)
                {
                    dealloc_memory();
                    close_sockets(fdmax, read_fds);
                    return 0;
                }
            }
            else if (i == udp_socket)
            {
                memset(buffer, 0, BUFF_LEN);
                // UDP socket
                ret = recvfrom(udp_socket, buffer, BUFF_LEN, 0, (struct sockaddr *) &udp_addr, &udp_len);
                DIE(ret < 0, "[ERROR]: Couldn't receive data on UDP socket!\n");

                // Convert from UDP to TCP packet and send the message
                udp_msg = (UDP_msg *) buffer;
                tcp_msg = (TCP_msg *) UDP_to_TCP(udp_msg, udp_addr);
                DIE(tcp_msg == NULL, "[ERROR]: Couldn't convert the UDP message to TCP message!\n");
                send_tcp_msg(tcp_msg);
            }
            else if (i == tcp_socket)
            {
                // Connection request on the listener TCP socket
                int req_tcp_socket = accept(tcp_socket, (struct sockaddr *) &sub_addr, &tcp_len);
                DIE(req_tcp_socket < 0, "[ERROR]: Couldn't accept a TCP client!\n");

                // Disable Nagle's algorithm
                int opt = 1;
                ret = setsockopt(req_tcp_socket, IPPROTO_TCP, TCP_NODELAY, (char *) &opt, sizeof(int));
                DIE(ret < 0, "[ERROR]: Couldn't disable the Nagle's algorithm!\n");

                // Add the `req_tcp_socket` to the `read_fds` set
                FD_SET(req_tcp_socket, &read_fds);

                // Update `fdmax`
                prev_fdmax  = fdmax;
                fdmax       = MAX(fdmax, req_tcp_socket);

                // First, receive the client's ID (this is the first thing sent by the `client` to the `server`)
                ret = recv(req_tcp_socket, buffer, BUFF_LEN, 0);
                DIE(ret < 0, "[ERROR]: Couldn't receive the client's ID!\n");

                // Check for ID duplicates (another client already has this ID)
                Client *client = get_client_by_id(buffer);
                if (client == NULL)
                {
                    // Create a new `client` and add it to the `subscribers` list
                    add_new_client(buffer, req_tcp_socket);
                    printf("New client %s connected from %s:%d.\n", buffer, inet_ntoa(sub_addr.sin_addr), ntohs(sub_addr.sin_port));
                }
                else
                {
                    // A client is trying to join with an existing ID from the db
                    // Let's check if he is trying to reconnect with his ID or
                    // connect with an existing ID of another user
                    if (client->connected)
                    {
                        printf("Client %s already connected.\n", client->id);
                        
                        if (verbose)
                        {
                            memset(buffer, 0, BUFF_LEN);
                            sprintf(buffer, "Client %s already connected.\n", client->id);
                            respose_with_err_msg(buffer, req_tcp_socket);
                        }

                        // Another client is already connected, close the socket
                        FD_CLR(req_tcp_socket, &read_fds);

                        // Use the last `fdmax`, because the current socket will be closed
                        fdmax = prev_fdmax;

                        // Close the current socket
                        close(req_tcp_socket);
                    }
                    else
                    {
                        // The current client is an old subscriber reconnecting
                        reconnect_old_sub(client, req_tcp_socket);
                        printf("New client %s connected from %s:%d.\n", buffer, inet_ntoa(sub_addr.sin_addr), ntohs(sub_addr.sin_port));
                    }
                }
            }
            else
            {
                // Received a TCP message from a connected subscriber
                ret = recv(i, buffer, sizeof(Action), 0);
                DIE(ret < 0, "[ERROR]: Couldn't receive the message from a connected client!\n");
                if (ret == 0)
                {
                    // The client stopped the communication
                    disconnect_client(i);

                    // The client has disconnected, so we can close the socket
                    FD_CLR(i, &read_fds);

                    // Use the last `fdmax`, because the current socket was closed
                    fdmax = prev_fdmax;
                }
                else
                {
                    // `subscribe` or `unsubscribe` actions
                    action = (Action *) buffer;

                    if (strcmp(action->type, SUBSCRIBE_ACTION) == 0)
                        subscribe_to_topic(action, i);
                    else if (strcmp(action->type, UNSUBSCRIBE_ACTION) == 0)
                        unsubscribe_from_topic(action, i);
                }
            }
        }
    }

    dealloc_memory();
    close_sockets(fdmax, read_fds);
    return 0;
}
