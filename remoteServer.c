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
#include <signal.h>

#define MAX_MSG 512
#define MAX_CMD 100
// null termination (1) + ignore stderr (12)  + MAX_CMD (100) + a character to exceed MAX_CMD (1) + cmdNumber (10) + port (5) + three delimiters (3) + ip/address (up to 30)
#define MAX_CMD_PLUS_HEADER MAX_CMD+62

#define LS_CMD "ls"
#define CAT_CMD "cat"
#define CUT_CMD "cut"
#define GREP_CMD "grep"
#define TR_CMD "tr"
#define END_CMD "end"
#define STOP_CMD "timeToStop"


struct InputCommand {
   char *command;
   char *clientport;
   int isCompleted;
   char *cmdNumber;
   char *initialPtr;
};

int make_socket(uint16_t port);
int child_server(int *fd);
void perror_exit(char *message);
void create_children(int childrenTotal, pid_t *pid);
void parent_server(int childrenTotal, int sock, pid_t *pid, int fd[][2][2]);
struct InputCommand *read_from_client(int fileDes);
struct InputCommand *create_command_struct(char * command, size_t len);
void allocate_to_children(struct InputCommand *Command, int fd[][2][2], struct sockaddr_in clientname, int *readyChild, int childrenTotal);
void remove_leading_spaces(char** line);
void remove_spaces(char* s);
void remove_trailing_spaces(char** line);
void to_lowercase(char** line);
void keep_first_command(char *cmd);
void replace_unquoted_pipes_with_newline(char **cmd);
void remove_invalid_pipe_commands(char **cmd);
void send_result_with_UDP(char *port, char *ip, char *result, size_t packetNum, char *cmdNumber);
size_t count_digits(size_t n);
void keep_one_child_per_pipe(pid_t *pid, int childrenTotal, int fdall[][2][2], int fdChild[2]);
int find_child_index(pid_t *pid, int childrenTotal);
void close_unused_parent_ends(int childrenTotal,int fd[][2][2]);
int find_child_from_process(pid_t *pid, pid_t process, int childrenTotal);
void terminate_process();
void close_child_resources(pid_t cpid, int child);


static int *g_fdall;
static fd_set g_socket_fd_set, g_pipe_fd_set;
static pid_t *g_pid;
static int g_childrenTotal;
static int g_continue = 1;
static int childStop = 0;
static int g_sock;
// static int readyChild;

int main(int argc, char *argv[])
{
	int sock, childrenTotal;
    uint16_t port;
	pid_t ppid = getpid();

    if (argc != 3){
        printf("Please give port number and number of children processes\n");
        exit(1);
    }
    port = atoi(argv[1]);
    childrenTotal = atoi(argv[2]);
	g_childrenTotal = childrenTotal;

	/* Ignore SIGPIPEs */
    signal(SIGPIPE, SIG_IGN);


	/* Create the TCP socket and set it up to accept connections. */
	sock = make_socket(port);
	g_sock = sock;
	

	/* Initialize the set of reading file descriptors to be monitored by select */
	FD_ZERO(&g_socket_fd_set);
	FD_SET(sock, &g_socket_fd_set);
	FD_ZERO(&g_pipe_fd_set);

	// initialize pipes
	// int fd[2];
	int fdall[childrenTotal][2][2];
	for (int i=0;i<childrenTotal;i++){
		// write to child pipe
		if (pipe(fdall[i][0])==-1){ 
			perror_exit("pipe"); 
		}
		// read from child pipe
		if (pipe(fdall[i][1])==-1){ 
			perror_exit("pipe"); 
		}
		FD_SET(fdall[i][1][0], &g_pipe_fd_set); // let select monitor reading ends
		// printf("for parent -> r-write:%d w-write:%d r-read:%d w-read:%d\n", fdall[i][0][0],fdall[i][0][1], fdall[i][1][0],fdall[i][1][1]);
	}

	g_fdall = malloc(sizeof(int)*2*2*childrenTotal);
	for (int child = 0;child<childrenTotal;child++){
		memcpy(g_fdall + child*4 + 0, &fdall[child][0][0], sizeof(int));
		memcpy(g_fdall + child*4 + 1, &fdall[child][0][1], sizeof(int));
		memcpy(g_fdall + child*4 + 2, &fdall[child][1][0], sizeof(int));
		memcpy(g_fdall + child*4 + 3, &fdall[child][1][1], sizeof(int));
		// printf("write-parent r: %d\n",*(g_fdall + child*4 + 0));
		// printf("write-parent w: %d\n",*(g_fdall + child*4 + 1));
		// printf("read-parent r: %d\n",*(g_fdall + child*4 + 2));
		// printf("read-parent w: %d\n",*(g_fdall + child*4 + 3));
	}


	/* Listen for connections */
	if (listen(sock, 1) < 0) {
        perror_exit("listen");
	}
	// printf("Listening for connections to port %d with total children processes: %d\n", port, childrenTotal);
	pid_t pid[childrenTotal];
	memset(pid, -1, childrenTotal*sizeof(pid_t));
	create_children(childrenTotal, pid);

	g_pid = malloc(sizeof(pid_t)*childrenTotal);
	for (int child = 0;child<childrenTotal;child++){
		*(g_pid + child) = pid[child];
	}


	if (getpid() == ppid){
		close_unused_parent_ends(childrenTotal, fdall);
		parent_server(childrenTotal, sock, pid, fdall);
	}
	else{
		int fd[2];
		keep_one_child_per_pipe(pid, childrenTotal, fdall, fd);
		child_server(fd);
	}
}

void close_unused_parent_ends(int childrenTotal,int fd[][2][2]){
	for (int i=0;i<childrenTotal;i++){
		// printf("Parent closing %d and %d\n", fd[i][0][0], fd[i][1][1]);
		close(fd[i][0][0]); //read end for parent writing pipe
		close(fd[i][1][1]); //write end for parent reading pipe
	}
}

int find_child_index(pid_t *pid, int childrenTotal){
	for (int j=0;j<childrenTotal;j++){
		if (pid[j] == -1){
			return j-1;
		}
	}
	return childrenTotal-1;
}

void keep_one_child_per_pipe(pid_t *pid, int childrenTotal, int fdall[][2][2], int fdChild[2]){
	int child = find_child_index(pid, childrenTotal);
	for (int j=0;j<childrenTotal;j++){
		if (child != j){
			// printf("Child closing %d, %d, %d and %d\n", fdall[j][0][0], fdall[j][0][1], fdall[j][1][0], fdall[j][1][1]);
			close(fdall[j][0][0]);
			close(fdall[j][0][1]);
			close(fdall[j][1][0]);
			close(fdall[j][1][1]);
		}
	}
	// printf("Child closing its %d and %d\n", fdall[child][0][1], fdall[child][1][0]);
	close(fdall[child][0][1]); //write end for parent writing pipe
	close(fdall[child][1][0]); //read end for parent reading pipe
	fdChild[0]=fdall[child][0][0]; //read end for parent writing pipe
	fdChild[1]=fdall[child][1][1]; //write end for parent reading pipe
	// printf("child = '%d', r = '%d', w = '%d'\n", getpid(), fd[0], fd[1]);
}

int find_child_from_process(pid_t *pid, pid_t process, int childrenTotal){
	int child = -1;
	for (int i=0;i<childrenTotal;i++){
		// printf("%d==%d\n", pid[i], process);
		if (pid[i] == process){
			child = i;
			break;
		}
	}
	return child;
}

int fdset_is_empty(fd_set const *fdset){
    static fd_set empty;     // initialized to 0 -> empty
    return memcmp(fdset, &empty, sizeof(fd_set)) == 0;
}

void handle_stop(int sig){
	g_continue = 0;
	for (int child=0;child<g_childrenTotal;child++){
		kill(g_pid[child], SIGUSR2);
		close_child_resources(g_pid[child], child);
	}
	shutdown(g_sock, SHUT_RDWR);
	close(g_sock);
	_exit(EXIT_SUCCESS);
}

void handle_end(int sig, siginfo_t *siginfo, void *context){
	g_continue = 0;
	// if (sig == SIGUSR1){
	int child = find_child_from_process(g_pid, siginfo->si_pid, g_childrenTotal);
	close_child_resources(siginfo->si_pid, child);
	// int child = find_child_from_process(g_pid, siginfo->si_pid, g_childrenTotal);
	// // printf ("Closing PID: %ld, child = %d, fd = %d\n", siginfo->si_pid, child, *(g_fdall+child*4+2));
	// FD_CLR(*(g_fdall+child*4+2), &g_pipe_fd_set);
	// close(*(g_fdall+child*4+1));
	// close(*(g_fdall+child*4+1));
	// waitpid(siginfo->si_pid, NULL, 0);
	// printf("Wait is over...\n");
	if (fdset_is_empty(&g_pipe_fd_set)){
		shutdown(g_sock, SHUT_RDWR);
		close(g_sock);
		_exit(EXIT_SUCCESS);
	}
}

void close_child_resources(pid_t cpid, int child){
	// int child = find_child_from_process(g_pid, cpid, g_childrenTotal);
	// printf ("Closing PID: %ld, child = %d, fd = %d\n", cpid, child, *(g_fdall+child*4+2));
	FD_CLR(*(g_fdall+child*4+2), &g_pipe_fd_set);
	close(*(g_fdall+child*4+1));
	close(*(g_fdall+child*4+1));
	waitpid(cpid, NULL, 0);
}

void handleContinueSignal(int sig) {
	sleep(1);
	g_continue = 1;
}

void parent_server(int childrenTotal, int sock, pid_t *pid, int fd[][2][2]){
	fd_set read_fd_set;
	int i;
	struct sockaddr_in clientname;
	size_t size;

	static struct sigaction actEnd, actStop;
 
	memset (&actEnd, '\0', sizeof(actEnd));
	memset (&actStop, '\0', sizeof(actStop));
 
	/* Use sa_sigaction with SA_SIGINFO to check for invoking pid */
	actEnd.sa_sigaction = &handle_end;
	actEnd.sa_flags = SA_SIGINFO;
	sigfillset(&(actEnd.sa_mask)); // ignore everything apart from SIGCONT for syncing
	sigdelset(&(actEnd.sa_mask), SIGCONT);

	actStop.sa_handler = &handle_stop;
	sigfillset(&(actStop.sa_mask)); // ignore everything else to terminate

	// handle signal for "end" command
	if (sigaction(SIGUSR1, &actEnd, NULL) < 0) {
		perror_exit("sigaction");
	}
	// handle signal for "timeToStop" command
	if (sigaction(SIGUSR2, &actStop, NULL) < 0) {
		perror_exit("sigaction");
	}

	signal(SIGCONT, handleContinueSignal);

	// // handle signal for terminating commands
	// signal(SIGUSR1, handle_end); // "end"
	// signal(SIGUSR2, handle_end); // "timeToStop"

	const int maxReadPipeFd = find_max_read_pipe(fd, childrenTotal);
	char ready[2];
	int readyChild = childrenTotal;

	// wait(NULL);

	// for (int child = 0;child<childrenTotal;child++){
	// 	printf("write-parent r: %d\n",*(g_fdall + child*4 + 0));
	// 	printf("write-parent w: %d\n",*(g_fdall + child*4 + 1));
	// 	printf("read-parent r: %d\n",*(g_fdall + child*4 + 2));
	// 	printf("read-parent w: %d\n",*(g_fdall + child*4 + 3));
	// }

	while (1) {
		if (g_continue == 1){
			/* Block until input arrives */
			
			// printf("readyChild=%d\n", readyChild);


			if (readyChild == childrenTotal){
				read_fd_set = g_pipe_fd_set;
				// printf("Testing2\n");
				if (select(FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0) {
					perror_exit("select");
				}
				// printf("Testing3\n");


				/* check which children are ready to accept commands */
				for (i = 0; i < maxReadPipeFd+1; i++){
					int child = find_child_of_pipe(fd, childrenTotal, i);
					// printf("child=%d\n", child);
					if (child != -1 && FD_ISSET(i, &read_fd_set)) {
						read(i, ready, 1);
						readyChild = child;
						// printf("readyChild %d wrote data and parent will read from %d\n", readyChild, i);
						break;
					}
				}
			}

			if (readyChild < childrenTotal){
				// printf("readyChild=%d\n", readyChild);
				read_fd_set = g_socket_fd_set;

				if (select(FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0) {
					perror_exit("select");
				}

				// printf("After select...");

				/* Service all the sockets with input pending. */
				for (i = 0; i < FD_SETSIZE; i++){
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
							FD_SET(new, &g_socket_fd_set);
						} else {
							// printf("same fd: %d, sock: %d\n", i, sock);
							g_continue = 0;
							struct InputCommand *Command = read_from_client(i);
							// for (int i=0; i<Command->numCommand; i++){
								// printf("cmd: '%s', port '%s'\n", Command->command, Command->clientport);
							// }
							if (Command->isCompleted == 1){
								// printf("Closing TCP connection with socket %d...\n", i);
								// fflush(stdout);
								close(i);
								FD_CLR(i, &g_socket_fd_set);

								// ignore empty line in the end
								if (Command->clientport  == NULL){
									break;
								}
							}
							// printf("readyChild=%d\n", readyChild);
							allocate_to_children(Command, fd, clientname, &readyChild, childrenTotal);
							// pause();
							break;
						}
					}
				}
			}
		}
	}
}

int find_max_read_pipe(int fd[][2][2], int childrenTotal){
	int max = 0;
	for (int i=0;i<childrenTotal;i++){
		if (fd[i][1][0] > max){
			max = fd[i][1][0];
		}
	}
	return max;
}

int find_child_of_pipe(int fd[][2][2], int childrenTotal, int fdSelected){
	for (int child = 0;child<childrenTotal;child++){
		// printf("%d=%d\n", fd[child][1][0], fdSelected);
		if (fd[child][1][0] == fdSelected){
			return child;
		}
	}
	return -1;
}

void handle_stop_child(int sig){
	childStop = 1;
}

int child_server(int *fd){

	char *cmd = malloc(MAX_CMD_PLUS_HEADER*sizeof(char));
	char *cmdTmp = malloc(MAX_CMD_PLUS_HEADER*sizeof(char));
	size_t initSize = 2*(MAX_MSG+1); // +1 for null termination between messages
	char *result = realloc(NULL, sizeof(char)*(initSize));
	char * const initialCmd = cmd;
	char * const initialCmdTmp = cmdTmp;
	
	static struct sigaction actChildStop;
 
	memset (&actChildStop, '\0', sizeof(actChildStop));

	actChildStop.sa_handler = &handle_stop_child;

	sigfillset(&(actChildStop.sa_mask)); // ignore everything else to terminate

	// handle signal for "timeToStop" command
	if (sigaction(SIGUSR2, &actChildStop, NULL) < 0) {
		perror_exit("sigaction");
	}

	// printf("writing to fd = %d\n", fd[1]);
	write(fd[1], "\n", 1);

	while(!childStop){
		// resetting pointers
		cmd = initialCmd;
		cmdTmp = initialCmdTmp;
		size_t partNum = 0;
		char *resultPtr = result;
		

		read(fd[0], cmd, MAX_CMD_PLUS_HEADER-1);
		if (childStop){
			break;
		}
		// sleep(1);
		// printf("After reading from %d...\n", fd[0]);


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
			kill(getppid(), SIGCONT);
		}else{
			keep_first_command(cmd);
			remove_leading_spaces(&cmd);
			remove_trailing_spaces(&cmd);


			if (strcmp(cmd, END_CMD) == 0){
				kill(getppid(), SIGUSR1);
				partNum++;
				sprintf(result, "%s;%s;%s", cmdNumber, "1f", "");
				send_result_with_UDP(port, ip, result, partNum, cmdNumber);
				break;
			}else if(strcmp(cmd, STOP_CMD) == 0){
				kill(getppid(), SIGUSR2);
				partNum++;
				sprintf(result, "%s;%s;%s", cmdNumber, "1f", "");
				send_result_with_UDP(port, ip, result, partNum, cmdNumber);
				break;
			}

			kill(getppid(), SIGCONT);

			
			// Command empty (in case client side allows that)
			printf("%s\n",cmd);
			if (!*cmd){
				partNum++;
				sprintf(result, "%s;%s;%s", cmdNumber, "1f", "");
				// printf("%s\n",result);
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

					// fflush(stdout);


					// ignore stderr
					sprintf(cmd+strlen(cmd), " 2>/dev/null");
					
					// printf("cmd: '%s'\n", cmd);
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
							// printf("%s;%s;%s;%d;%s\n",port, ip, cmdNumber, partNum);
							if (count_digits(partNum) != count_digits(partNum-1)){
								iterSize -= 1;
							}
							fgetsRes = fread(nextBuffer, sizeof(char), iterSize, pipe_fp);
						}else if (partNum > 0){ // finish flag
							resultPtr = resultPtr - (MAX_MSG + 1);
							sprintf(resultPtr, "%s;%d%c;%s", cmdNumber, partNum, 'f', buffer);
							// printf("%s\n", result);
							break;
						}else{
							partNum++;
							sprintf(result, "%s;%s;%s", cmdNumber, "1f", buffer);
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

		// // let child die if parent dies unexpectedly, 1 -> init process
		// if (getppid() <= 1){
		// 	break;
		// }

		write(fd[1], "1", 1);
		
	}

	close(fd[0]);
	close(fd[1]);
	free(result);
	free(initialCmdTmp);
	free(initialCmd);

	terminate_process();
}

void terminate_process(){

	int pid = getpid();
	char termMsg[count_digits(pid)+20];
	sprintf(termMsg, "process %d terminated\n", pid);
	write(2, termMsg, strlen(termMsg));
	kill(getppid(), SIGCONT);
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
			// printf("%s with cmdNumber='%s'\n", result+i*(MAX_MSG+1), cmdNumber);
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
		// printf("tmpPtr: %s\n", *cmd);

		while(*tmpPtr == ' '){
			tmpPtr++;
		}
		// printf("tmpPtr: %s\n", *cmd);

		strncpy(name, tmpPtr, 4);

		for (int i=0;i<strlen(name);i++){
			if (name[i] == ' '){
				name[i] = '\0';
			}
		}
		// printf("%c", name[0]);
		// name[0] = tmpPtr[0];
		// name[1] = tmpPtr[1];
		// name[2] = tmpPtr[2];
		// name[3] = '\0';
		to_lowercase(&name);
		// printf("name: %s\n", *cmd);
		if (strcmp(name, LS_CMD) != 0 
			&& strcmp(name, CAT_CMD) != 0 
			&& strcmp(name, CUT_CMD) != 0 
			&& strcmp(name, GREP_CMD) != 0 
			&& strcmp(name, TR_CMD)!= 0){
				(*cmd) = initialPtr;
				free(name);
				return;
		}

		*(firstCmd-1)='|';
		firstCmd = strsep(cmd, "\n");
		// printf("firstCmd: %s\n", *cmd);
	}
	// if while finished, initial cmd was ok
	(*cmd) = initialPtr; 
	free(name);
	return;
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

void allocate_to_children(struct InputCommand *Command, int fd[][2][2], struct sockaddr_in clientname, int *readyChild, int childrenTotal){
	// int maxWrapperSize = MAX_CMD+50;
	char cmdbuf[MAX_CMD_PLUS_HEADER];
	snprintf(cmdbuf, MAX_CMD_PLUS_HEADER-1, "%s;%s;%s;%s", Command->clientport, inet_ntoa(clientname.sin_addr), Command->cmdNumber, Command->command);
	printf("writing to child %d fd = %d\n", *readyChild, fd[*readyChild][0][1]);
	if (write(fd[*readyChild][0][1], cmdbuf, MAX_CMD_PLUS_HEADER-1) == -1){
		perror_exit("write of allocate_to_children");
	}
	printf("'%s'\n", cmdbuf);
	*readyChild=childrenTotal; // child occupied
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

void create_children(int childrenTotal, pid_t *pid){
    for(int j=0;j<childrenTotal;j++) {
		pid[j] = fork();
        if(pid[j] == 0) {	
            // printf("[child] pid %d from [parent] pid %d\n",getpid(),getppid());
            break;
        }else if(pid[j] < 0){
            perror_exit("fork");
        }
    }
}

struct InputCommand *read_from_client(int fileDes){
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
	// printf("readResult=%d\n", readResult);
	// client finish, close connection
	if (readResult == 0){
		Command->isCompleted = 1;
	}else{
		Command->isCompleted = 0;
	}
	return Command;
}

void perror_exit(char *message){
	if (errno == EPIPE || errno == EINTR){ // ignore broken pipes and interrupted select
		return;
	}else{
		perror(message);
		shutdown(g_sock, SHUT_RDWR);
		close(g_sock);
		exit(EXIT_FAILURE);
	}
}