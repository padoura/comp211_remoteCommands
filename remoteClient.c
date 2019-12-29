/* i n e t _ s t r _ c l i e n t .c: Internet stream sockets client */
#include <stdio.h>
#include <sys/types.h>  /* sockets */
#include <sys/socket.h> /* sockets */
#include <netinet/in.h> /* internet sockets */
#include <unistd.h>       /* read , write , close */
#include <netdb.h>        /* ge th os tb ya dd r */
#include <stdlib.h>       /* exit */
#include <string.h>       /* strlen */

#define MAXMSG  512

void perror_exit(char *message);

void main(int argc, char *argv[])
{
    int serverPort, clientPort, sock, i;
    char buf[MAXMSG];
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
    if ((rem = gethostbyname(serverName)) == NULL)
    {
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
    do
    {
        printf("Give input string: ");
        line = fgets(buf, sizeof(buf), stdin); /* Read from stdin */
        if (line == NULL)
            break;
        // for (i = 0; buf[i] != '\0'; i++)
        // { /* For every char */
            /* Send i-th character */
        if (write(sock, buf, MAXMSG) < 0)
            perror_exit("write");
        /* receive i- th character transformed */
        if (read(sock, buf, MAXMSG) < 0)
            perror_exit("read");
        // }
        printf("Received string: %s", buf);
    } while (strcmp(buf,"END\n") != 0); /* Finish on"end"*/
    close(sock);                           /* Close socket and exit */
}

void perror_exit(char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}