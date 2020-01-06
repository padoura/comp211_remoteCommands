#include <stdio.h>
#include <sys/types.h>  /* sockets */
#include <sys/socket.h> /* sockets */
#include <netinet/in.h> /* internet sockets */
#include <unistd.h>       /* read , write , close */
#include <netdb.h>        /* ge th os tb ya dd r */
#include <stdlib.h>       /* exit */
#include <string.h>       /* strlen */

#define MAX_MSG 512

void perror_exit(char *message);
void send_commands(int sock, char *inputFile, int clientPort);
void write_line(int sock, char *line, size_t len);
pid_t *create_children(int childrenTotal);
void receive_results(uint16_t clientPort);
int make_socket(uint16_t port);
char *name_from_address(struct in_addr addr);

void main(int argc, char *argv[])
{
    int sock, i;
    uint16_t serverPort, clientPort;
    struct hostent *rem;
    struct sockaddr_in server;
    struct sockaddr *serverptr = (struct sockaddr *)&server;
    char *line;
    pid_t ppid = getpid();
    int childStatus = 0;

    if (argc != 5)
    {
        printf("Please give server name and port number, client port number and input file.\n");
        exit(1);
    }
    char *serverName = strdup(argv[1]);
    serverPort = atoi(argv[2]);        /* Convert port number to integer */
    clientPort = atoi(argv[3]);
    char *inputFile  = strdup(argv[4]);
    
    /* Create socket */
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        perror_exit("socket");
    /* Find server address */
    if ((rem = gethostbyname(serverName)) == NULL){
        perror_exit("gethostbyname");
    }
    
    server.sin_family = AF_INET; /* Internet domain */
    memcpy(&server.sin_addr, rem->h_addr, rem->h_length);
    server.sin_port = htons(serverPort); /* Server port */

    /* Initiate connection */
    if (connect(sock, serverptr, sizeof(server)) < 0)
        perror_exit("connect");
    // printf("Connecting to %s port %d\n", serverName, serverPort);
    
    pid_t *pid = create_children(1);

	if (getpid() == ppid){
		send_commands(sock, inputFile, clientPort);
        while (wait(&childStatus) > 0); // waiting for all children to exit first
	}
	else{
		receive_results(clientPort);
	}
}

void receive_results(uint16_t clientPort){
    int n, sock;
    char buf[MAX_MSG+1];
    char *clientname;
    struct sockaddr_in client;
    struct sockaddr_in *clientPtr = (struct sockaddr*) &client;
    unsigned int clientlen;


    sock = make_socket(clientPort);

    while(1) { 
        clientlen = sizeof(client);
        /* Receive message */
        if ((n = recvfrom(sock, buf, sizeof(buf), 0, clientPtr, &clientlen)) < 0)
            perror_exit("recvfrom");

        buf[sizeof(buf)-1]='\0'; /* force str termination */

        /* Try to discover client’s name */
        clientname = name_from_address(client.sin_addr);
        printf("Received from %s: %s\n", clientname , buf); /* Send message */

        if (sendto(sock, buf, n, 0, clientPtr, clientlen)<0)
            perror_exit("sendto");
    }
}

char *name_from_address(struct in_addr addr){
    struct hostent *rem;
    int asize = sizeof(addr.s_addr);
    if((rem = gethostbyaddr(&addr.s_addr, asize, AF_INET)))
        return rem->h_name; /* reverse lookup success */
    return inet_ntoa(addr); /* fallback to a.b.c.d form */
}


int make_socket(uint16_t port){
    int sock;
    struct sockaddr_in server;
    struct sockaddr_in *serverPtr = (struct sockaddr*) &server;
    unsigned int serverlen;
    if ((sock = socket(AF_INET , SOCK_DGRAM , 0)) < 0) 
        perror_exit("socket");
    
    /* Bind socket to address */
    server.sin_family = AF_INET; /* Internet domain */
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);
    serverlen = sizeof(server);
    if (bind(sock, serverPtr, serverlen) < 0)
        perror_exit("bind");
    
    /* Discover selected port */
    if (getsockname(sock, serverPtr, &serverlen) < 0)
        perror_exit("getsockname");
    // printf("Socket port: %d\n", ntohs(server.sin_port));

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

	if (bind(sock, serverPtr, sizeof(server)) < 0) {
        perror_exit("bind");
	}


    return sock;
}


void send_commands(int sock, char *inputFile, int clientPort){
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t size;

    int counter = 0;

    fp = fopen(inputFile, "r");
    if (fp == NULL){
        close(sock);
        perror_exit("fopen");
    }
    
    while ((size = getline(&line, &len, fp)) != -1){
        counter++;
        char lineBuf[strlen(line)+18]; // length of line + int counter (10) + two ";" delimiters (2) + port (5) + null termination (1)
        snprintf(lineBuf, strlen(line)+17, "%d;%d;%s", counter, clientPort, line);
        write_line(sock, lineBuf, strlen(lineBuf));
        if (counter % 10 == 0){
            sleep(5);
        }
    }
    close(sock);
    fclose(fp);
}

void write_line(int sock, char *line, size_t len){
    // printf("Sending command: %s\n",line);
    for (int i = 0; i < len && line[i] != '\0'; i++){ 
        /* For every char */
        /* Send i-th character */
        if (write(sock, line + i, 1) < 0)
            perror_exit("write");
    }
}

pid_t *create_children(int childrenTotal){
	pid_t pid[childrenTotal];
    for(int j=0;j<childrenTotal;j++) {
		pid[j] = fork();
        if(pid[j] == 0) { 
            // printf("[child] pid %d from [parent] pid %d\n",getpid(),getppid());
            break;
        }else if(pid[j] < 0){
            perror_exit("fork");
        }
    }
	return pid;
}


void perror_exit(char *message){
    perror(message);
    exit(EXIT_FAILURE);
}