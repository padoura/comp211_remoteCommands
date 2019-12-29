/* i n e t _ s t r _ s e r v e r .c: Internet stream sockets server */
#include <stdio.h>
#include <sys/wait.h>   /* sockets */
#include <sys/types.h>  /* sockets */
#include <sys/socket.h> /* sockets */
#include <netinet/in.h> /* internet sockets */
#include <netdb.h>      /* ge th os tb ya dd r */
#include <unistd.h>     /* fork */
#include <stdlib.h>     /* exit */
#include <ctype.h>      /* toupper */
#include <signal.h>     /* signal */

void child_server(int newsock);
void perror_exit(char *message);
void sigchld_handler(int sig);

void main(int argc, char *argv[])
{
    int port, sock, newsock;
    struct sockaddr_in server, client;
    socklen_t clientlen;
    struct sockaddr *serverptr = (struct sockaddr *)&server;
    struct sockaddr *clientptr = (struct sockaddr *)&client;
    struct hostent *rem;
    if (argc != 2)
    {
        printf("Please give port number \n");
        exit(1);
    }
    port = atoi(argv[1]);
    /* Reap dead children a s y n c h r o n o u s l y */
    signal(SIGCHLD, sigchld_handler);
    /* Create socket */
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        perror_exit("socket");
    server.sin_family = AF_INET; /* Internet domain */
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port); /* The given port */
    /* Bind socket to address */
    if (bind(sock, serverptr, sizeof(server)) < 0)
        perror_exit("bind");
    /* Listen for connections */
    if (listen(sock, 5) < 0)
        perror_exit("listen");
    printf("Listening for connections to port %d\n", port);
    while (1)
    {
        /* accept connection */
        if ((newsock = accept(sock, clientptr, &clientlen)) < 0)
            perror_exit("accept");
        /* Find client ’s address */
        // if ((rem = gethostbyaddr((char *)&client.sin_addr.s_addr, sizeof(client.sin_addr.s_addr), client.sin_family)) == NULL)
        // {
            // herror("gethostbyaddr");
            // exit(1);
        // }
        // printf("Accepted connection from %s\n", rem->h_addr);
        // printf ("Accepted connection \n");
        switch (fork())
        {        /* Create child for serving client */
        case -1: /* Error */
            perror("fork");
            break;
        case 0: /* Child process */
            close(sock);
            child_server(newsock);
            exit(0);
        }
        close(newsock); /* parent closes socket to client */
    }
}

void child_server(int newsock)
{
    char buf[1];
    while (read(newsock, buf, 1) > 0)
    {                    /* Receive 1 char */
        putchar(buf[0]); /* Print received char */
        /* Capitalize character */
        buf[0] = toupper(buf[0]);
        /* Reply */
        if (write(newsock, buf, 1) < 0)
            perror_exit("write");
    }
    printf("Closing connection.\n");
    close(newsock); /* Close socket */
}

/* Wait for all dead child processes */
void sigchld_handler(int sig)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

void perror_exit(char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}