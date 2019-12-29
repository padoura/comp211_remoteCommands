// C program to demonstrate working of wait() 
#include<stdio.h> 
#include<sys/wait.h> 
#include<unistd.h> 
  

pid_t result, waitResult;

int main() 
{ 
    if (result = fork()== 0) {
        printf("hello to child %ld\n",(long) result);
    }
    else
    { 
        printf("hello to parent %ld\nwaiting for child...\n",(long) result);
        waitResult = wait(NULL);
        printf("child %ld has terminated\n", (long) result);

    } 
  
    printf("Bye\n"); 
    return 0;

}