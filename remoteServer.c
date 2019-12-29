/*
 * Tcp server with select() example:
 * http://www.gnu.org/software/libc/manual/html_node/Server-Example.html
 */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define MAXMSG 512
#define MAXCMD 100

int make_socket(uint16_t port);
int child_server(int filedes);
void perror_exit(char *message);

int main(int argc, char *argv[])
{
	extern int make_socket(uint16_t port);
	int sock, childrenTotal;
    uint16_t port;
	fd_set active_fd_set, read_fd_set;
	int i;
	struct sockaddr_in clientname;
	size_t size;

    if (argc != 3)
    {
        printf("Please give port number and number of children processes\n");
        exit(1);
    }
    port = atoi(argv[1]);
    childrenTotal = atoi(argv[2]);

	/* Create the TCP socket and set it up to accept connections. */
	sock = make_socket(port);
	/* Listen for connections */
	if (listen(sock, 1) < 0) {
        perror_exit("listen");
	}
	printf("Listening for connections to port %d with total children processes: %d\n", port, childrenTotal);

	/* Initialize the set of active sockets. */
	FD_ZERO(&active_fd_set);
	FD_SET(sock, &active_fd_set);

	while (1) {
		childrenTotal = childrenTotal + 3; //taking 3 standard file descriptors into account
		/* Block until input arrives on one or more active sockets. */
		read_fd_set = active_fd_set;
		if (select(childrenTotal, &read_fd_set, NULL, NULL, NULL) < 0) {
            perror_exit("select");
		}

		/* Service all the sockets with input pending. */
		for (i = 0; i < childrenTotal; ++i)
			if (FD_ISSET(i, &read_fd_set)) {
				printf("adding fd: %d\n", i);
				if (i == sock) {
					/* Connection request on original socket. */
					int new;
					size = sizeof(clientname);
					new = accept(sock,(struct sockaddr *)&clientname, &size);
					if (new < 0) {
                        perror_exit("accept");
					}
					printf("DEBUG: creating new from socket %d\n",sock);
					// printf
					//     ("Server: connect from host , port %d.\n",
					//      //inet_ntoa (clientname.sin_addr),
					//      ntohs(clientname.sin_port));
					FD_SET(new, &active_fd_set);
				} else {
					printf("DEBUG: checking socket %d\n",i);
					switch (fork()) {        /* Create child for serving client */
						case -1: /* Error */
							perror("fork");
							break;
						case 0: /* Child process */
							close(sock);
							/* Data arriving on an already-connected socket. */
							if (child_server(i) < 0) {
								printf("Closing connection with socket %d...\n", i);
								close(i);
								FD_CLR(i, &active_fd_set);
							}
							exit(0);
					}
					close(i); /* parent closes socket to client */
					FD_CLR(i, &active_fd_set);
				}
			}
	}
}

int make_socket(uint16_t port)
{
	int sock;
	struct sockaddr_in name;

	/* Create the socket. */
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
        perror_exit("socket");
	}

	/* Give the socket a name. */
	name.sin_family = AF_INET;
	name.sin_port = htons(port);
	name.sin_addr.s_addr = htonl(INADDR_ANY);

	// Allow reuse of socket before bind when server quits
    int option = 1;
    int optLen = sizeof(option);
    if(
        setsockopt(sock,SOL_SOCKET,SO_REUSEPORT,&option,optLen) < 0 ||
        setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&option,optLen) < 0
    )
    {
        close(sock);
        perror_exit("setsockopt");
    }

	if (bind(sock, (struct sockaddr *) &name, sizeof(name)) < 0) {
        perror_exit("bind");
	}

	return sock;
}

int child_server(int filedes)
{
	char buffer[MAXMSG];
	int nbytes;

	while(1){
		nbytes = read(filedes, buffer, MAXMSG);
		if (nbytes < 0) {
			/* Read error. */
			perror_exit("read");
		} else if (nbytes == 0)
			/* End-of-file. */
			return -1;
		else {
			printf("Socket %d sent message: %s", filedes, buffer);
			/* Data read. */
			/* Capitalize character */
			for (int j=0; j<MAXMSG ;j++){
				buffer[j] = toupper(buffer[j]);
			}
			if (write(filedes, buffer, MAXMSG) < 0)
				perror_exit("write");
		}
	}
}

void perror_exit(char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}