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
void send_commands(int sock, FILE *fp, int clientPort);
void write_line(int sock, char *line, size_t len);
pid_t *create_children(int childrenTotal);
void receive_results(uint16_t clientPort, size_t cmdTotal);
int make_socket(uint16_t port);
size_t count_lines(FILE *fp);
size_t count_digits(size_t n);

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
    

    FILE *fp;
    fp = fopen(inputFile, "r");
    if (fp == NULL){
        close(sock);
        perror_exit("fopen");
    }
    size_t cmdTotal = count_lines(fp);
    rewind(fp);
    
    pid_t *pid = create_children(1);

	if (getpid() == ppid){
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

		send_commands(sock, fp, clientPort);
        while (
            wait(&childStatus) > 0
        ); // waiting for all children to exit first
	}
	else{
		receive_results(clientPort, cmdTotal);
	}
}

size_t count_lines(FILE *fp){
    char *line = NULL;
    size_t len = 0;
    ssize_t size;

    size_t counter = 0;
    
    while ((size = getline(&line, &len, fp)) != -1){
        counter++;
    }
    free(line);
    return counter;
}

void receive_results(uint16_t clientPort, size_t cmdTotal){
    int sock;
    char *buf = malloc((MAX_MSG+1)*sizeof(char));
    char * const initBuf = buf;
    char *servername;
    struct sockaddr_in server;
    struct sockaddr_in *serverPtr = (struct sockaddr*) &server;
    unsigned int serverlen;


    // strings for checking progress
    char resultReceived[cmdTotal+1];
    char expectedResultReceived[cmdTotal+1];
    for (int i = 0; i < cmdTotal; i++){
        resultReceived[i] = '0';
    }
    for (int i = 0; i < cmdTotal; i++){
        expectedResultReceived[i] = '1';
    }
    resultReceived[cmdTotal] = '\0';
    expectedResultReceived[cmdTotal] = '\0';

    sock = make_socket(clientPort);

    while(strcmp(resultReceived, expectedResultReceived) != 0) {
        // printf("%d\n", strcmp(resultReceived, expectedResultReceived) != 0);
        serverlen = sizeof(server);
        /* Receive message */
        if ((recvfrom(sock, buf, MAX_MSG+1, 0, serverPtr, &serverlen)) < 0)
            perror_exit("recvfrom");
        
        // parsing result with expected format
        int cmdNumLen = atoi(strsep(&buf, ";"));
        int partNumLen = atoi(strsep(&buf, ";"));
        int cmdResultLen = atoi(strsep(&buf, ";"));
        char *cmdNumber = strsep(&buf, ";");
        char *partNum = strsep(&buf, ";");
        char *cmdResult = buf;
        buf = initBuf;

        // printf("%d;%d;%d;%s;%s;%s\n", cmdNumLen, partNumLen, cmdResultLen, cmdNumber, partNum, cmdResult); /* Send message */
        // printf("%d;%d;%d;%d;%d\n", cmdNumLen == strlen(cmdNumber), partNumLen == strlen(partNum), cmdResultLen == strlen(cmdResult), atoi(cmdNumber) > 0, atoi(cmdNumber) <= cmdTotal); /* Send message */

        if (cmdNumLen == strlen(cmdNumber) &&
            partNumLen == strlen(partNum) &&
            cmdResultLen == strlen(cmdResult) &&
            atoi(cmdNumber) > 0 &&
            atoi(cmdNumber) <= cmdTotal
        ){
            //prepare ACK
            char msg[count_digits(clientPort) + strlen(cmdNumber) + strlen(partNum) + 7]; // port;cmdNumber;partNum;ACK
            sprintf(msg, "%u;%s;%s;ACK", clientPort, cmdNumber, partNum);

            if (sendto(sock, msg, count_digits(clientPort) + strlen(cmdNumber) + strlen(partNum) + 7, 0, serverPtr, serverlen)<0)
                perror_exit("sendto");
            
            //write result to appropriate file
            FILE *fp;
            char filename[15 + count_digits(clientPort) + strlen(cmdNumber)]; //output.receive{clientPort}.{cmdNumber}
            sprintf(filename, "output.receive%u.%s", clientPort, cmdNumber);
            fp = fopen(filename, "a");
            fprintf(fp, cmdResult);
            fclose(fp);

            if ((strstr(partNum, "f") != NULL)){
                *(resultReceived+atoi(cmdNumber)-1) = '1';
            }
        }
    }
    close(sock);
    free(buf);
}

size_t count_digits(size_t n){
	size_t counter = 0;
	while (n != 0) {
		n /= 10;
		counter++;
    }
	return counter;
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
    return sock;
}


void send_commands(int sock, FILE *fp, int clientPort){
    char *line = NULL;
    size_t len = 0;
    ssize_t size;

    int counter = 0;
    while ((size = getline(&line, &len, fp)) != -1){
        counter++;
        char lineBuf[len+18]; // length of line + int counter (10) + two ";" delimiters (2) + port (5) + null termination (1)
        snprintf(lineBuf, len+17, "%d;%d;%s", counter, clientPort, line);
        write_line(sock, lineBuf, strlen(lineBuf));
        if (counter % 10 == 0){
            sleep(5);
        }
    }
    free(line);
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