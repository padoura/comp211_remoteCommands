/* rlsd .c - a remote ls server - with paranoia */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#define PORTNUM 15000 /* rlsd listens on this port */
void perror_exit(char *msg);
void sanitize(char *str);
int main(int argc, char *argv[])
{
    struct sockaddr_in myaddr; /* build our address here */
    int c, lsock, csock;       /* listening and client sockets */
    FILE *sock_fp;             /* stream for socket IO */
    FILE *pipe_fp;             /* use popen to run ls */
    char dirname[BUFSIZ];      /* from client */
    char command[BUFSIZ];      /* for popen () */
    /** create a TCP a socket **/
    if ((lsock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        perror_exit("socket");
    /** bind address to socket . **/
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myaddr.sin_port = htons(PORTNUM); /* port to bind socket */
    myaddr.sin_family = AF_INET;      /* internet addr family */
    if (bind(lsock, (struct sockaddr *)&myaddr, sizeof(myaddr)))
        perror_exit("bind");
    /** listen for connections with Qsize =5 **/
    if (listen(lsock, 5) != 0)
        perror_exit("listen");
    while (1)
    { /* main loop : accept - read - write */
        /* accept connection , ignore client address */
        if ((csock = accept(lsock, NULL, NULL)) < 0)
            perror_exit("accept");
        /* open socket as buffered stream */
        if ((sock_fp = fdopen(csock,"r+")) == NULL)
            perror_exit("fdopen");
        /* read dirname and build ls command line */
        if (fgets(dirname, BUFSIZ, sock_fp) == NULL)
            perror_exit("reading dirname");
        sanitize(dirname); /* clear wild characters */
        snprintf(command, BUFSIZ,"ls %s", dirname);
        /* Invoke ls through popen */
        if ((pipe_fp = popen(command,"r")) == NULL)
            perror_exit("popen");
        /* transfer data from ls to socket */
        while ((c = getc(pipe_fp)) != EOF)
            putc(c, sock_fp);
        pclose(pipe_fp);
        fclose(sock_fp);
    }
    return 0;
}

/* it would be very bad if someone passed us an dirname like
*"; rm *"and we naively created a command"ls ; rm *".
* So .. we remove everything but slashes and a lp ha nu me ri cs .
*/
void sanitize(char *str)
{
    char *src, *dest;
    for (src = dest = str; *src; src++)
        if (*src == '/' || isalnum(*src))
            *dest++ = *src;
    *dest = '\0';
}
/* Print error message and exit */
void perror_exit(char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}