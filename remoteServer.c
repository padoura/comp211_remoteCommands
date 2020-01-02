/*
 * Tcp server with select() example:
 * http://www.gnu.org/software/libc/manual/html_node/Server-Example.html
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define MAXMSG 512
#define MAXCMD 100

struct InputCommands {
   char  **commands;
   size_t numCommands;
   uint16_t clientport;
};

int make_socket(uint16_t port);
int child_server(int *fd);
void perror_exit(char *message);
pid_t *create_children(int childrenTotal);
void parent_server(int childrenTotal, fd_set active_fd_set, int sock, pid_t *pid, int *fd);
struct InputCommands *read_from_client(int fileDes);
struct InputCommands *newline_splitter(char * commands, size_t len, size_t numCommands);
void allocate_to_children(struct InputCommands *Commands, int *fd, struct sockaddr_in clientname);

int main(int argc, char *argv[])
{
	extern int make_socket(uint16_t port);
	int sock, childrenTotal;
    uint16_t port;
	fd_set active_fd_set, read_fd_set;
	pid_t ppid = getpid();

    if (argc != 3){
        printf("Please give port number and number of children processes\n");
        exit(1);
    }
    port = atoi(argv[1]);
    childrenTotal = atoi(argv[2]);

	/* Ignore SIGPIPEs */
    signal(SIGPIPE, SIG_IGN);

	// initialize pipes
	int fd[2];
	// int fd[childrenTotal][2]; 
	// for (int i=0;i<childrenTotal;i++){
	if (pipe(fd)==-1){ 
		perror_exit("pipe"); 
	}
	// }

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

	pid_t *pid = create_children(childrenTotal);

	if (getpid() == ppid){
		parent_server(childrenTotal, active_fd_set, sock, pid, fd);
	}
	else{
		child_server(fd);
	}
}

void parent_server(int childrenTotal, fd_set active_fd_set, int sock, pid_t *pid, int *fd){
	fd_set read_fd_set;
	int i;
	struct sockaddr_in clientname;
	size_t size;
	close(fd[0]); // close reading end
	while (1) {
		/* Block until input arrives on one or more active sockets. */
		read_fd_set = active_fd_set;
		if (select(FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0) {
            perror_exit("select");
		}
		/* Service all the sockets with input pending. */
		for (i = 0; i < FD_SETSIZE; ++i)
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
					printf("Server: connect from host %s port %d.\n",
						inet_ntoa(clientname.sin_addr),
						ntohs(clientname.sin_port));
					FD_SET(new, &active_fd_set);
				} else {
					struct InputCommands *Commands = read_from_client(i);
					// for (int i=0; i<Commands->numCommands; i++){
					// 	printf("%s\n", *(Commands->commands+i));
					// }
					//allocate_to_children(commands);
					printf("Closing TCP connection with socket %d...\n", i);
					close(i);
					FD_CLR(i, &active_fd_set);

					allocate_to_children(Commands, fd, clientname);
				}
			}
	}
}

int child_server(int *fd)
{
	close(fd[1]);

	while(1){
		int maxWrapperSize = MAXCMD+5+21+2;
		char *cmd = malloc(maxWrapperSize*sizeof(char));
		// char cmdCopy[maxWrapperSize];
		read(fd[0], cmd, maxWrapperSize+1);
		char *port = strsep(&cmd, ";");
		char *ip = strsep(&cmd, ";");
		// command_to_run(cmd);
		if (strlen(cmd) > MAXCMD){
			
		}else{
			FILE *pipe_fp;
			char result[MAXMSG];
			if ((pipe_fp = popen(cmd, "r")) == NULL )
				perror_exit("popen");
			/* transfer data from ls to socket */
			printf("Child %d read '%s' with results '", getpid(), cmd);
			while(fgets(result, MAXMSG, pipe_fp) != NULL) {
				printf("%s", result);
			}
			printf("' and will be sent to address '%s' and port '%s' \n", ip, port);
			pclose(pipe_fp);
		}
		
		// send_result_with_UDP(cmd);
		// printf("Child %d read '%s' with result '%s'\n", getpid(), cmd, strlen(cmd));
	}
}

void allocate_to_children(struct InputCommands *Commands, int *fd, struct sockaddr_in clientname){
	int maxWrapperSize = MAXCMD+5+21+2;
	for(int i=0;i<Commands->numCommands;i++){
		char cmdbuf[maxWrapperSize];
		snprintf(cmdbuf, maxWrapperSize, "%d;%s;%s", Commands->clientport, inet_ntoa(clientname.sin_addr), *(Commands->commands+i));
		if (write(fd[1], cmdbuf, maxWrapperSize+1) == -1){
			perror_exit("write of allocate_to_children");
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

pid_t *create_children(int childrenTotal){
	pid_t pid[childrenTotal];
    for(int j=0;j<childrenTotal;j++) {
		pid[j] = fork();
        if(pid[j] == 0) { 
            printf("[child] pid %d from [parent] pid %d\n",getpid(),getppid());
            break;
        }
    }
	return pid;
}

struct InputCommands *read_from_client(int fileDes){
	char buf[1];
	char *commands;
	size_t len = 0;
	size_t initSize = 100;
	size_t numCommands = 0;

	commands = realloc(NULL, sizeof(char)*initSize);
	printf("Socket %d sent commands:\n", fileDes);
    while (read(fileDes, buf, 1) > 0){/* Receive 1 char */
		commands[len++]=buf[0];
        if(len==initSize){
            commands = realloc(commands, sizeof(char)*(initSize+=16));
        }
		if (buf[0] == '\n'){
			numCommands++;
		}
    }
	commands[len]='\0';
	if (commands[len-1] != '\n'){
		numCommands++;
	}
	struct InputCommands *Commands = newline_splitter(commands, len, numCommands);
	// Commands->clientport = atoi(strsep());
	// Commands->commands = splitted;
	// Commands->numCommands = numCommands;
	// free(commands);
	return Commands;
}

struct InputCommands *newline_splitter(char * commands, size_t len, size_t numCommands){
	struct InputCommands *Commands = malloc(sizeof(struct InputCommands));
	Commands->commands = malloc(sizeof(char*)*numCommands);
	Commands->clientport = atoi(strsep(&commands, "\n"));
	char *p = strsep(&commands, "\n");
	for (int i=0; i<numCommands; i++){
		*(Commands->commands+i)=p;
		// printf("Splitted: %s\n", *(splitted+i));
		// printf("Rest: %s\n", commands);
		p = strsep(&commands, "\n");
	}
	Commands->numCommands = numCommands;
	return Commands;
}

void perror_exit(char *message){
    perror(message);
    exit(EXIT_FAILURE);
}