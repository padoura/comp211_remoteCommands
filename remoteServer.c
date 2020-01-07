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
#include <ctype.h>
#include <regex.h>

#define MAX_MSG 512
#define MAX_CMD 100
// null termination (1) + ignore stderr (12)  + MAX_CMD (100) + a character to exceed MAX_CMD (1) + cmdNumber (10) + port (5) + three delimiters (3) + ip/address (up to 30)
#define MAX_CMD_PLUS_HEADER MAX_CMD+62

#define LS_CMD "ls"
#define CAT_CMD "cat"
#define CUT_CMD "cut"
#define GREP_CMD "grep"
#define TR_CMD "tr"


struct InputCommand {
   char  *command;
   char *clientport;
   int isCompleted;
   char *cmdNumber;
   char *initialPtr;
};

int make_socket(uint16_t port);
int child_server(int *fd);
void perror_exit(char *message);
pid_t *create_children(int childrenTotal);
void parent_server(int childrenTotal, fd_set active_fd_set, int sock, pid_t *pid, int *fd);
struct InputCommand *read_from_client(int fileDes, fd_set active_fd_set);
struct InputCommand *create_command_struct(char * command, size_t len);
void allocate_to_children(struct InputCommand *Command, int *fd, struct sockaddr_in clientname);
void remove_leading_spaces(char** line);
void remove_spaces(char* s);
void remove_trailing_spaces(char** line);
void to_lowercase(char** line);
void keep_first_command(char *cmd);
void replace_unquoted_pipes_with_newline(char **cmd);
void remove_invalid_pipe_commands(char **cmd);
void send_result_with_UDP(char *port, char *ip, char *result, size_t packetNum, char *cmdNumber);
size_t count_digits(size_t n);


int main(int argc, char *argv[])
{
	int sock, childrenTotal;
    uint16_t port;
	fd_set active_fd_set;
	pid_t ppid = getpid();

    if (argc != 3){
        printf("Please give port number and number of children processes\n");
        exit(1);
    }
    port = atoi(argv[1]);
    childrenTotal = atoi(argv[2]);

	/* Ignore SIGPIPEs */
    // signal(SIGPIPE, SIG_IGN);


	/* Create the TCP socket and set it up to accept connections. */
	sock = make_socket(port);
	

	// initialize pipes
	int fd[2];
	// int fd[childrenTotal][2]; 
	// for (int i=0;i<childrenTotal;i++){
	if (pipe(fd)==-1){ 
		perror_exit("pipe"); 
	}
	// printf("r:%dw:%d\n",fd[0],fd[1]);
	// }


	/* Listen for connections */
	if (listen(sock, 1) < 0) {
        perror_exit("listen");
	}
	// printf("Listening for connections to port %d with total children processes: %d\n", port, childrenTotal);

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
				// printf("adding fd: %d\n", i);
				if (i == sock) {
					/* Connection request on original socket. */
					int new;
					size = sizeof(clientname);
					new = accept(sock,(struct sockaddr *)&clientname, &size);
					if (new < 0) {
                        perror_exit("accept");
					}
					// printf("Server: connect from host %s port %d.\n",
					// 	inet_ntoa(clientname.sin_addr),
					// 	ntohs(clientname.sin_port));
					FD_SET(new, &active_fd_set);
				} else {
					// printf("same fd: %d, sock: %d\n", i, sock);
					struct InputCommand *Command = read_from_client(i, active_fd_set);
					// for (int i=0; i<Command->numCommand; i++){
						// printf("cmd: '%s', port '%d'\n", *(Command->command), Command->clientport);
					// }
					if (Command->isCompleted == 1){
						// printf("Closing TCP connection with socket %d...\n", i);
						// fflush(stdout);
						close(i);
						FD_CLR(i, &active_fd_set);
					}

					allocate_to_children(Command, fd, clientname);
				}
			}
	}
}

int child_server(int *fd){

	close(fd[1]);
	char *cmd = malloc(MAX_CMD_PLUS_HEADER*sizeof(char));
	char *cmdTmp = malloc(MAX_CMD_PLUS_HEADER*sizeof(char));
	size_t initSize = 2*(MAX_MSG+1); // +1 for null termination between messages
	char *result = realloc(NULL, sizeof(char)*(initSize));
	char * const initialCmd = cmd;
	char * const initialCmdTmp = cmdTmp;

	while(1){
		// resetting pointers
		cmd = initialCmd;
		cmdTmp = initialCmdTmp;
		size_t partNum = 0;
		char *resultPtr = result;

		read(fd[0], cmd, MAX_CMD_PLUS_HEADER-1);


		char *port = strsep(&cmd, ";");
		char *ip = strsep(&cmd, ";");
		char *cmdNumber = strsep(&cmd, ";");

		// if (i+1 == 1){
		// }

		/* adding headers
		The transferred message has the form:
		cmdNumber;partNum;cmdResult
		*/

		// Command too large
		if (strlen(cmd) > MAX_CMD){
			partNum++;
			sprintf(result, "%s;%s;%s", cmdNumber, "1f", "");
		}else{
			keep_first_command(cmd);
			remove_leading_spaces(&cmd);
			remove_trailing_spaces(&cmd);

			
			// Command empty (in case client side allows that)
			if (!*cmd){
				partNum++;
				sprintf(result, "%s;%s;%s", cmdNumber, "1f", "");
			}else{
				
				memcpy(cmdTmp, cmd, MAX_CMD_PLUS_HEADER*sizeof(char));
				char *firstCmd = strsep(&cmdTmp, " ");
				to_lowercase(&firstCmd);
				

				// First command invalid
				if (strcmp(firstCmd, LS_CMD) != 0 
					&& strcmp(firstCmd, CAT_CMD) != 0 
					&& strcmp(firstCmd, CUT_CMD) != 0 
					&& strcmp(firstCmd, GREP_CMD) != 0 
					&& strcmp(firstCmd, TR_CMD)!= 0)
				{
					partNum++;
					sprintf(result, "%s;%s;%s", cmdNumber, "1f", "");
				}else{ // command acceptable, proceed with popen

					// printf("%d -> %s\n", getpid(), cmdNumber);
					remove_invalid_pipe_commands(&cmd);

					// printf("cmd: '%s'\n", cmd);
					// fflush(stdout);


					// ignore stderr
					sprintf(cmd+strlen(cmd), " 2>/dev/null");
					
					FILE *pipe_fp;
					if ((pipe_fp = popen(cmd, "r")) == NULL ){
						perror_exit("popen");
					}
					/* build result in null-separated MAX_MSG-sized packets */
					size_t iterSize = MAX_MSG - 2 - 2 - 3 - strlen(cmdNumber); // delimiters (2), partNum (2), strlen(buffer) (3)
					char nextBuffer[iterSize+1];
					char buffer[iterSize+1];
					size_t fgetsRes = fread(nextBuffer, sizeof(char), iterSize, pipe_fp);
					while(1) {
						nextBuffer[fgetsRes] = '\0'; // secure null termination
						if (fgetsRes > 0){
							sprintf(buffer,"%s",nextBuffer);
							partNum++;
							sprintf(resultPtr, "%s;%d;%s", cmdNumber, partNum, buffer);
							if (partNum*(MAX_MSG + 1) == initSize){
								result = realloc(result, sizeof(char)*(initSize += MAX_MSG+1));
							}
							resultPtr = result + partNum*(MAX_MSG + 1); // each part of message has MAX_MSG characters + null termination
							// digits increased for storing partNum
							// printf("%s;%s;%s;%d\n",port, ip, cmdNumber, partNum);
							if (count_digits(partNum) != count_digits(partNum-1)){
								iterSize -= 1;
							}
							fgetsRes = fread(nextBuffer, sizeof(char), iterSize, pipe_fp);
						}else if (partNum > 0){ // finish flag
							resultPtr = resultPtr - (MAX_MSG + 1);
							sprintf(resultPtr, "%s;%d%c;%s", cmdNumber, partNum, 'f', nextBuffer);
							break;
						}else{
							partNum++;
							sprintf(result, "%s;%s;%s", cmdNumber, "1f", nextBuffer);
							break;
						}
					}
					pclose(pipe_fp);
				}
			}
		}
		
		// printf("result: '%s'\n", result);
		// fflush(stdout);
		// printf("%s;%s;%s;%d\n",port, ip, cmdNumber, partNum);
		send_result_with_UDP(port, ip, result, partNum, cmdNumber);

		// let child die if parent dies unexpectedly, 1 -> init process
		if (getppid() <= 1){
			break;
		}
		
	}

	close(fd[0]);
	free(result);
	free(initialCmdTmp);
	free(initialCmd);
	exit(EXIT_SUCCESS);
}

size_t count_digits(size_t n){
	size_t counter = 0;
	while (n != 0) {
		n /= 10;
		counter++;
    }
	return counter;
}

void send_result_with_UDP(char *port, char *ip, char *result, size_t packetNum, char *cmdNumber){
	// server-client names are reversed in this method (since roles are reversed for UDP connection)
	int sock;
	struct hostent *rem;
	struct sockaddr_in server, client;
	unsigned int serverlen = sizeof(server);
	struct sockaddr *serverPtr = (struct sockaddr *) &server;
	struct sockaddr *clientPtr = (struct sockaddr *) &client;

	/* Create socket */
	if ((sock = socket(AF_INET , SOCK_DGRAM , 0)) < 0){
		perror_exit("socket");
	}
	/* Find server ’s IP address */
	if ((rem = gethostbyname(ip)) == NULL) {
		perror_exit("gethostbyname");
	}

	/* Setup server’s IP address and port */
	server.sin_family = AF_INET; /* Internet domain */
	memcpy(&server.sin_addr, rem->h_addr, rem->h_length);
	server.sin_port = htons(atoi(port));
	/* Setup my address */
	client.sin_family = AF_INET; /* Internet domain */
	client.sin_addr.s_addr = htonl(INADDR_ANY); /*Any address*/
	client.sin_port = htons(0); /* Autoselect port */

	/* Bind my socket to my address*/
	if (bind(sock, clientPtr, sizeof(client)) < 0) {
		perror_exit("bind");
	}

	for (int i=0;i<packetNum;i++){
		// if (i+1 == 1){
		// 	printf("%s with cmdNumber='%s'\n", result+i*(MAX_MSG+1), cmdNumber);
		// }
		if (sendto(sock, result+i*(MAX_MSG+1), MAX_MSG, 0, serverPtr, serverlen) < 0) {
			perror_exit("sendto");
		}
	}
	close(sock);
}

void remove_invalid_pipe_commands(char **cmd){
	replace_unquoted_pipes_with_newline(cmd);
	if ((strstr(*cmd, "\n") == NULL)){
		return; // no pipes to check
	}
	char * const initialPtr = strsep(cmd, "\n"); //first command already checked
	char *firstCmd = strsep(cmd, "\n");
	char *name = malloc(5*sizeof(char));
	name[4] = '\0';

	while(firstCmd != NULL){
		char *tmpPtr = firstCmd;
		// printf("tmpPtr: %s\n", tmpPtr);

		while(*tmpPtr == ' '){
			tmpPtr++;
		}
		// printf("tmpPtr: %s\n", tmpPtr);

		strncpy(name, tmpPtr, 4);
		// printf("%c", name[0]);
		// name[0] = tmpPtr[0];
		// name[1] = tmpPtr[1];
		// name[2] = tmpPtr[2];
		// name[3] = '\0';
		to_lowercase(&name);
		if (strcmp(name, LS_CMD) != 0 
			&& strcmp(name, CAT_CMD) != 0 
			&& strcmp(name, CUT_CMD) != 0 
			&& strcmp(name, GREP_CMD) != 0 
			&& strcmp(name, TR_CMD)!= 0){
				(*cmd) = initialPtr;
				break;
		}

		*(firstCmd-1)='|';
		firstCmd = strsep(cmd, "\n");
	}
	free(name);
}

void replace_unquoted_pipes_with_newline(char **cmd){
	int dQuoteOpen = 0;
	int sQuoteOpen = 0;
	for(int i = 0; (*cmd)[i]; i++){
		if ((*cmd)[i] == '|' && dQuoteOpen == 0 && sQuoteOpen == 0){
			(*cmd)[i] = '\n';
		}

		if ((*cmd)[i] == '\"' && (*cmd)[i-1] != '\\' && sQuoteOpen == 0)
			dQuoteOpen = (dQuoteOpen+1)%2;

		if ((*cmd)[i] == '\'' && (*cmd)[i-1] != '\\' && dQuoteOpen == 0)
			sQuoteOpen = (sQuoteOpen+1)%2;
	}
}

void keep_first_command(char *cmd){
	int dQuoteOpen = 0;
	int sQuoteOpen = 0;
	for(int i = 0; cmd[i]; i++){
		if (cmd[i] == ';' && dQuoteOpen == 0 && sQuoteOpen == 0){
			cmd[i] = '\0';
			return;
		}
			

		if (cmd[i] == '\"' && cmd[i-1] != '\\' && sQuoteOpen == 0){
			dQuoteOpen = (dQuoteOpen+1)%2;

		}

		if (cmd[i] == '\'' && cmd[i-1] != '\\' && dQuoteOpen == 0)
			sQuoteOpen = (sQuoteOpen+1)%2;
	}
}

void remove_leading_spaces(char** line){
	int i; 
	for(i = 0; (*line)[i] == ' '; i++){ 
		//intentionally left blank
	}
	*line += i;
}

void remove_trailing_spaces(char** line){
	for(int i = strlen(*line)-1; (*line)[i] == ' '; i--){
		(*line)[i] = '\0';
	}
}

void to_lowercase(char** line){
	for(int i = 0; (*line)[i]; i++){
		(*line)[i] = tolower((*line)[i]);
	}
}

void allocate_to_children(struct InputCommand *Command, int *fd, struct sockaddr_in clientname){
	// int maxWrapperSize = MAX_CMD+50; 
	char cmdbuf[MAX_CMD_PLUS_HEADER];
	snprintf(cmdbuf, MAX_CMD_PLUS_HEADER-1, "%s;%s;%s;%s", Command->clientport, inet_ntoa(clientname.sin_addr), Command->cmdNumber, Command->command);
	if (write(fd[1], cmdbuf, MAX_CMD_PLUS_HEADER-1) == -1){
		perror_exit("write of allocate_to_children");
	}
	// printf("'%s'\n", cmdbuf);
	free(Command->initialPtr);
	free(Command);
}

int make_socket(uint16_t port){
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
            // printf("[child] pid %d from [parent] pid %d\n",getpid(),getppid());
            break;
        }else if(pid[j] < 0){
            perror_exit("fork");
        }
    }
	return pid;
}

struct InputCommand *read_from_client(int fileDes, fd_set active_fd_set){
	char buf[1];
	size_t len = 0;
	size_t initSize = 119; // based on client's msg size
	ssize_t readResult;
	struct InputCommand *Command = malloc(sizeof(struct InputCommand));
	Command->command = realloc(NULL, sizeof(char)*initSize);
	// printf("Socket %d sent command:\n", fileDes);
    while ((readResult = read(fileDes, buf, 1) > 0) && buf[0] != '\n'){/* Receive 1 char */
		Command->command[len++]=buf[0];
        if(len==initSize){
            Command->command = realloc(Command->command, sizeof(char)*(initSize+=16));
        }
    }
	Command->initialPtr = Command->command;

	// null termination
	Command->command[len]='\0';

	Command->cmdNumber = strsep(&Command->command, ";");
	Command->clientport = strsep(&Command->command, ";");
	
	// printf("'%s' as: '%d'\n", Command->command, Command->cmdNumber);
	// client finish, close connection
	if (readResult == 0){
		Command->isCompleted = 1;
	}else{
		Command->isCompleted = 0;
	}
	return Command;
}

void perror_exit(char *message){
    perror(message);
    exit(EXIT_FAILURE);
}