/* i n e t _ s t r _ c l i e n t .c: Internet stream sockets client */
#include <stdio.h>
#include <sys/types.h>  /* sockets */
#include <sys/socket.h> /* sockets */
#include <netinet/in.h> /* internet sockets */
#include <unistd.h>       /* read , write , close */
#include <netdb.h>        /* ge th os tb ya dd r */
#include <stdlib.h>       /* exit */
#include <string.h>       /* strlen */

#define MAXMSG 512
#define MAXCMD 101

void perror_exit(char *message);
void send_commands(int sock, char *inputFile, int clientPort);
void write_line(int sock, char *line, size_t len);

void main(int argc, char *argv[])
{
    int serverPort, clientPort, sock, i;
    struct hostent *rem;
    struct sockaddr_in server;
    struct sockaddr *serverptr = (struct sockaddr *)&server;
    char *line;
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
        herror("gethostbyname");
        exit(1);
    }
    
    server.sin_family = AF_INET; /* Internet domain */
    memcpy(&server.sin_addr, rem->h_addr, rem->h_length);
    server.sin_port = htons(serverPort); /* Server port */

    /* Initiate connection */
    if (connect(sock, serverptr, sizeof(server)) < 0)
        perror_exit("connect");
    printf("Connecting to %s port %d\n", serverName, serverPort);
    
    send_commands(sock, inputFile, clientPort);
}

void send_commands(int sock, char *inputFile, int clientPort){
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t size;
    char buf[MAXMSG];

    int counter = 0;

    char portBuf[6];
    sprintf(portBuf, "%d\n", clientPort);

    fp = fopen(inputFile, "r");
    if (fp == NULL){
        close(sock);
        perror_exit("fopen");
    }
    
    while ((size = getline(&line, &len, fp)) != -1){
        write_line(sock, portBuf, strlen(portBuf));
        write_line(sock, line, len);
            
        // if (read(sock, buf, MAXCMD) < 0)
        //     perror_exit("read");
        // printf("%s", buf);
        counter++;
        if (counter % 10 == 0){
            printf("Waiting...");
            fflush(stdout);
            counter = 0;
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


void perror_exit(char *message){
    perror(message);
    exit(EXIT_FAILURE);
}