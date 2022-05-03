#include "utils.h"

/* Return the appropriate string, given the type as integer */
char *enum_to_str(uint8_t type)
{
    switch (type)
    {
        case 0:
            return "INT";
        case 1:
            return "SHORT_REAL";
        case 2:
            return "FLOAT";
        case 3:
            return "STRING";
        default:
            return "";
    }

    return "";
}


/* Print the correct usage of the program */
void usage(FILE *file, const char *exec_name)
{
    //                         argv[1]     argv[2]      argv[3]
    fprintf(file, "Usage: %s [CLIENT_ID] [SERVER_IP] [SERVER_PORT]\n", exec_name);
    exit(EXIT_FAILURE);
}


int main(int argc, char *argv[])
{
    /* Sanity check for arguments */
    if (argc < 3)
        usage(stderr, argv[0]);

    /* Rename the arguments */
    char *client_id   = argv[1];
    char *server_ip   = argv[2];
    char *server_port = argv[3];

    /* Convert the given port in `argv[3]` to integer */
    int port_number = atoi(server_port);
    DIE(port_number == 0, "[ERROR]: Couldn't convert the str `argv[3]` to int!\n");

    /* Disable buffering */
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    
    /* Declare server socket */
    struct sockaddr_in serv_addr;

    /* Declare sets */
    fd_set read_fds;    // Read set used in `select()`
    fd_set temp_fds;    // Temporary set

    /* Clear sets */
    FD_ZERO(&read_fds); // Clear the `read_fds` set
    FD_ZERO(&temp_fds); // Clear the `temp_fds` set

    /* Create server socket */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "[ERROR]: Couldn't create the server socket!\n");

    /* Initialize server socket */
	memset((char *) &serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family         = AF_INET;
	serv_addr.sin_port           = htons(port_number);
	int ret                      = inet_aton(server_ip, &serv_addr.sin_addr);
    DIE(ret == 0, "[ERROR]: Couldn't convert the `server ip` from dotted format into binary form!\n");

    /* Add server and STDIN sockets in the `read_fds` set */
    FD_SET(sockfd,  &read_fds);
    FD_SET(STDIN_FILENO, &read_fds);

    /* Connect to the server */
    ret = connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    DIE(ret < 0, "[ERROR]: Couldn't connect to the server!\n");

    /* First, send the cilent's ID */
    ret = send(sockfd, client_id, ID_CLIENT_LEN, 0);
    DIE(ret < 0, "[ERROR]: Couldn't send the client's ID to the server!\n");

    /* Disable Nagle's algorithm */
    int opt = 1;
    ret = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &opt, sizeof(int));
    DIE(ret < 0, "[ERROR]: Couldn't disable the Nagle's algorithm!\n");

    /* Declare an TCP message */
    TCP_msg *tcp_msg;

    /* Declare an action */
    Action action;
    
    char action_buffer[BUFF_LEN];
    char tcp_msg_buffer[TCP_MSG_SIZE];
    while (1)
    {
        temp_fds = read_fds;
        ret = select(sockfd + 1, &temp_fds, NULL, NULL, NULL);
        DIE(ret < 0, "[ERROR]: Couldn't select a socket!\n");

        if (FD_ISSET(STDIN_FILENO, &temp_fds))
        {
            /* Client sends something to the server (STDIN) */
            memset(action_buffer, 0, BUFF_LEN);
            fgets(action_buffer, BUFF_LEN - 1, stdin);
            if (strncmp(action_buffer, EXIT_ACTION, strlen(EXIT_ACTION)) == 0)
                break;

            if (strncmp(action_buffer, SUBSCRIBE_ACTION, strlen(SUBSCRIBE_ACTION)) == 0)
            {
                // Action format: subscribe <TOPIC> <SF>
                char *aux = strtok(action_buffer, " "); // subscribe
                if (aux == NULL)
                    continue;
                strcpy(action.type, aux);
                
                aux = strtok(NULL, " ");                // <TOPIC>
                if (aux == NULL)
                    continue;
                strcpy(action.topic, aux);

                aux = strtok(NULL, " ");                // <SF>
                if (aux == NULL)
                    continue;
                if (aux[strlen(aux) - 1] == '\n')
                    aux[strlen(aux) - 1] =  '\0';
                action.sf = atoi(aux);

                // Send the `action` to the `server`
                ret = send(sockfd, (char *) &action, sizeof(Action), 0);
                DIE(ret < 0, "[ERROR]: Couldn't send the `action` to the `server`!\n");

                printf("Subscribed to topic.\n");
            }
            else if (strncmp(action_buffer, UNSUBSCRIBE_ACTION, strlen(UNSUBSCRIBE_ACTION)) == 0)
            {
                // Action format: unsubscribe <TOPIC>
                char *aux = strtok(action_buffer, " "); // unsubscribe
                if (aux == NULL)
                    continue;
                strcpy(action.type, aux);
                
                aux = strtok(NULL, " ");                // <TOPIC>
                if (aux == NULL)
                    continue;
                if (aux[strlen(aux) - 1] == '\n')
                    aux[strlen(aux) - 1] =  '\0';
                strcpy(action.topic, aux);
            
                // Send the `action` to the `server`
                ret = send(sockfd, (char *) &action, sizeof(Action), 0);
                DIE(ret < 0, "[ERROR]: Couldn't send the `action` to the `server`!\n");

                printf("Unsubscribed from topic.\n");
            }
        }
        else if (FD_ISSET(sockfd, &temp_fds))
        {
            /* Client received something (size of the packet) from the server */
            memset(tcp_msg_buffer, 0, TCP_MSG_SIZE + 1);
            ret = recv(sockfd, tcp_msg_buffer, MAX_DIGITS_TCP_MSG_LEN, 0);
            DIE(ret < 0, "[ERROR]: Couldn't receive the size of the TCP message from the server!\n");

            // Server closed the `main` connection
            if (ret == 0)
                break;
            
            int size = atoi(tcp_msg_buffer);
            DIE(size == 0, "[ERROR]: Couldn't convert the TCP message size from `str` to `int`!\n");

            // Receive the actual message
            memset(tcp_msg_buffer, 0, TCP_MSG_SIZE + 1);
            int got = 0;
            while (got < size)
            {
                ret = recv(sockfd, tcp_msg_buffer, size, 0);
                DIE(ret < 0, "[ERROR]: Couldn't receive the TCP message from the server!\n");

                // Server closed the `main` connection
                if (ret == 0)
                    break;

                got += ret;
            }

            // Convert the bitstream to TCP_msg struct
            tcp_msg = (TCP_msg *) tcp_msg_buffer;

            if (!tcp_msg->from_server)
            {
                // Display the received message
                printf("%s:%d - %s - %s - %s\n", tcp_msg->ip, tcp_msg->port, tcp_msg->udp_msg.topic,
                                                 enum_to_str(tcp_msg->udp_msg.type), tcp_msg->udp_msg.payload);
            }
            else
                printf("%s", tcp_msg->udp_msg.payload);

        }
    }

    // Close the socket
    close(sockfd);
    return 0;
}
