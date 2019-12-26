#include <stdio.h>
#include <sys/types.h>  /* sockets */
#include <sys/socket.h> /* sockets */
#include <netinet/in.h> /* internet sockets */
#include <netdb.h>        /* ge th os tb yn am e */
#include <unistd.h>       /* read , write , close */
#include <stdlib.h>
#define PORTNUM 15000
#define BUFFSIZE 256
/* Write () repeatedly until ’size ’ bytes are written */
void perror_exit(char *message);
int write_all(int fd, void *buff, size_t size)
{
    int sent, n;
    for (sent = 0; sent < size; sent += n)
    {
        if ((n = write(fd, buff + sent, size - sent)) == -1)
            return -1; /* error */
    }
    return sent;
}
int main(int argc, char *argv[])
{
    struct sockaddr_in servadd; /* The address of server */
    struct hostent *hp;         /* to resolve server ip */
    int sock, n_read;           /* socket and message length */
    char buffer[BUFFSIZE];      /* to receive message */
    if (argc != 3)
    {
        puts("Usage : rls < hostname > < directory >");
        exit(1);
    }
    /* Step 1: Get a socket */
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        perror_exit("socket");
    /* Step 2: lookup server ’s address and connect there */
    if ((hp = gethostbyname(argv[1])) == NULL)
    {
        herror("gethostbyname");
        exit(1);
    }
    memcpy(&servadd.sin_addr, hp->h_addr, hp->h_length);
    servadd.sin_port = htons(PORTNUM); /* set port number */
    servadd.sin_family = AF_INET;      /* set socket type */
    if (connect(sock, (struct sockaddr *)&servadd, sizeof(servadd)) != 0)
        perror_exit("connect");
    /* Step 3: send directory name + newline */
    if (write_all(sock, argv[2], strlen(argv[2])) == -1)
        perror_exit("write");
    if (write_all(sock,"\n", 1) == -1)
        perror_exit("write");
    /* Step 4: read back results and send them to stdout */
    while ((n_read = read(sock, buffer, BUFFSIZE)) > 0)
        if (write_all(STDOUT_FILENO, buffer, n_read) < n_read)
            perror_exit("fwrite");
    close(sock);
    return 0;
}

/* Print error message and exit */
void perror_exit(char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}