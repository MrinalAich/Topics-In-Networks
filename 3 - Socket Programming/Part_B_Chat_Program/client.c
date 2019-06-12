#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define TRUE 1
#define FALSE 0

#define BUFSIZE 1024
#define MAX_USERNAME_LEN 16
#define LOG_ERROR(x) \
        {  \
            perror(x); \
            exit(-1); \
        }
#define LOG_ERROR_NO_EXIT(x) perror(x);

typedef struct thread_comm {
    pthread_mutex_t mutex;
    bool finish;
    pthread_cond_t done;
} pthread_comm_t;

pthread_comm_t pthread_comm;

// Stdin read function
void* HandleStdinBuffer(void* ownSock);

// Client Database maintainence related functions
bool SendUsernname(char* username, int ownSock);
bool RequestClientToConnect(int ownSock, char* otherUsername);
bool GetActiveListofOtherClients(int ownSock);

// Thread maintainance Functions
bool checkOtherThreadIsActive();
void threadInformAboutClose();
void threadInitializeVar();


int main(int argc, char **argv) 
{

    if (argc != 3)
        LOG_ERROR("<Server Address> <Server Port>");

    char *servIP = argv[1];

    // Set port number as given by user or as default 12345
    // in_port_t servPort = (argc == 3) ? atoi(argv[2]) : 12345;

    // Set port number as user specifies
    in_port_t servPort = atoi(argv[2]);

    //Creat a socket
    int ownSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ownSock < 0)
        LOG_ERROR("socket() failed");

    // Set the server address
    struct sockaddr_in servAddr;
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(servPort);
    int err = inet_pton(AF_INET, servIP, &servAddr.sin_addr.s_addr);
    if (err <= 0)
        LOG_ERROR("inet_pton() failed");

    // Connect to server
    if (connect(ownSock, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0)
        LOG_ERROR("connect() failed");

    // Send Client's UserName to Server
    char myUsername[MAX_USERNAME_LEN] = {0};
    do {

        err = SendUsernname(myUsername, ownSock);
        if( !err )
            printf("Error Occured. Try Again.\n");
    } while(!err);
   
    // Thread Initialization
    pthread_t thread;
    int iret = pthread_create( &thread, NULL, HandleStdinBuffer, (void*) &ownSock);
    if( iret != 0 )
        LOG_ERROR("Thread Creation Failed.");

    pthread_mutex_init (&pthread_comm.mutex , NULL);
    threadInitializeVar();

	// Prepare for using select()
	fd_set orgSockSet; // Set of socket descriptors for select
	FD_ZERO(&orgSockSet);
	FD_SET(ownSock, &orgSockSet);

	// Setting select timeout as Zero makes select non-blocking
	struct timeval timeOut;
	timeOut.tv_sec = 0; // 0 sec
	timeOut.tv_usec = 0; // 0 microsec

    // Loopaction to Handle Message from Server
    while(1) 
    {
        fd_set currSockSet;
  		memcpy(&currSockSet, &orgSockSet, sizeof(fd_set));

      	timeOut.tv_sec = 2; // 0 sec
    	timeOut.tv_usec = 0; // 0 microsec

        if( !checkOtherThreadIsActive() )
            break;

		select(ownSock + 1, &currSockSet, NULL, NULL, &timeOut);

        if(FD_ISSET(ownSock, &currSockSet))
        {
            char buffer[BUFSIZE];
            memset(buffer, 0, BUFSIZE);
            ssize_t recvLen = recv(ownSock, buffer, BUFSIZE - 1, 0);

            if (recvLen < 0)
            {
                LOG_ERROR_NO_EXIT("recv() failed");
            }
            else if (recvLen == 0)
            {
                LOG_ERROR("recv() connection closed prematurely");
            }
            else
            {
                if ( strncmp(buffer, "bye", 3) == 0)
                    printf("Server: Exiting Connection Closed.\n");
                else
                {
                    buffer[recvLen] = '\0';
                    printf("%s\n",buffer);
                }
            }
        }
    }
    close(ownSock);
    threadInformAboutClose();
    printf("Client Program Ends...\n");
    return 0;
}

/* Requests client to Connect. */
bool RequestClientToConnect(int ownSock, char* otherUsername)
{
    char buffer[BUFSIZE];
    memset(buffer, 0, BUFSIZE);
    strcpy(buffer, otherUsername);
    size_t bufferLen = strlen(buffer);
    buffer[bufferLen-1] = '\0'; // Removing \n from STDIN
    bool ret = TRUE;

    ssize_t sentLen = send(ownSock, buffer, bufferLen, 0);
    if (sentLen < 0)
    {
        LOG_ERROR("send() failed");
        ret = FALSE;
    }
    else if (sentLen != bufferLen) 
    {
        LOG_ERROR("send(): sent unexpected number of bytes");
        ret = FALSE;
    }
    return ret;
}

/* Thread function to handle STDIN */
void* HandleStdinBuffer(void* ownSock)
{
    /* After signalling `main`, the thread could actually
    go on to do more work in parallel.  */
    while(1)
    {
        char buffer[BUFSIZE];
        memset(buffer, 0, BUFSIZE);

        fgets(buffer, BUFSIZE, stdin);
        size_t bufferLen = strlen(buffer);

        // Check whether other thread is still active
        if( !checkOtherThreadIsActive() )
            break;

        if(bufferLen)
        {
            // Request to get Active List of Idle Clients.
            if( strncmp(buffer, "REQUEST", 7) == 0 )
                GetActiveListofOtherClients(*((int*)ownSock));

            // Request to Connect with Client
            else if(strncmp(buffer, "CONNECT:", 8) == 0)
                RequestClientToConnect(*((int*)ownSock), buffer);

            // User exits
            else if(strncmp(buffer, "bye", 3) == 0)
            {
                printf("You Closed Connection.\n");
                ssize_t sentLen = send(*((int*)ownSock), buffer, bufferLen, 0);
                if (sentLen < 0)
                {
                    LOG_ERROR("send() failed");
                }
                else if (sentLen != bufferLen)
                {
                    LOG_ERROR("send(): sent unexpected number of bytes");
                }
                break;
            }
            // Send string to server
            else 
            {
                printf("\nYou: ");
                fputs(buffer, stdout);
                buffer[bufferLen-1] = '\0';
                ssize_t sentLen = send(*((int*)ownSock), buffer, bufferLen, 0);
                if (sentLen < 0)
                {
                    LOG_ERROR("send() failed");
                }
                else if (sentLen != bufferLen)
                {
                    LOG_ERROR("send(): sent unexpected number of bytes");
                }
            }
        }
    }
    threadInformAboutClose();
    close(*((int*)ownSock));
}

/* Send Username to server just after Connect */
bool SendUsernname(char* username, int ownSock)
{
    bool ret = TRUE, err = FALSE;
    ssize_t bufferLen = 0;
    char buffer[BUFSIZE];
    memset(buffer, 0, BUFSIZE);

    // Send Username to Server
    printf("Username: ");
    do {
        fgets(buffer, BUFSIZE, stdin);
        bufferLen = strlen(buffer);
        if( bufferLen > MAX_USERNAME_LEN )
            printf("Username can be maximum of 16 characters. Try Again.\n");

    } while (bufferLen > MAX_USERNAME_LEN);
    buffer[bufferLen-1] = '\0'; // Remove \n character from stdin

    strncpy(username, buffer, bufferLen);
    
    ssize_t sentLen = send(ownSock, buffer, bufferLen, 0);
    if (sentLen < 0)
    {
        LOG_ERROR_NO_EXIT("send() failed");
        ret = FALSE;
    }
    else if (sentLen != bufferLen)
    {
        LOG_ERROR_NO_EXIT("send(): sent unexpected number of bytes");
        ret = FALSE;
    }

    // Wait for Response from Server
    // List of all Available Clients to Chat.
    printf("Wait from Server.\n");
    ssize_t recvLen = recv(ownSock, buffer, BUFSIZE - 1, 0);
    if (recvLen < 0)
    {
        LOG_ERROR("recv() failed");
        ret = FALSE;
    }
    else if (recvLen == 0)
    {
        LOG_ERROR("recv() connection closed prematurely");
        ret = FALSE;
    }
    else
    {
        printf("\nServer: ");
        buffer[recvLen] = '\n';

        fputs(buffer, stdout);
    }
    return ret;
}

/* Request for Active List of Other Clients */
bool GetActiveListofOtherClients(int ownSock)
{
    ssize_t bufferLen = 0;
    char buffer[BUFSIZE];
    bool ret = TRUE;
    memset(buffer, 0, BUFSIZE);
 
    strcpy(buffer, "RequestForActiveClientList");
    bufferLen = strlen(buffer);
    
    ssize_t sentLen = send(ownSock, buffer, bufferLen, 0);
    if (sentLen < 0)
    {
        LOG_ERROR_NO_EXIT("send() failed");
        ret = FALSE;
    }
    else if (sentLen != bufferLen)
    {
        LOG_ERROR_NO_EXIT("send(): sent unexpected number of bytes");
        ret = FALSE;
    }
    return ret;
}

/* Thread related functions */
/* Checks whether other thread is Active or not */
bool checkOtherThreadIsActive()
{
    bool ret = TRUE;
    pthread_mutex_lock(&pthread_comm.mutex);
    
    if( pthread_comm.finish == TRUE )
        ret = FALSE;

    /* Unlock and signal completion.  */
    pthread_mutex_unlock(&pthread_comm.mutex);
    pthread_cond_signal(&pthread_comm.done);
    return ret;
}

/* Inform the other thread about close. Updates shared variable */
void threadInformAboutClose()
{
    pthread_mutex_lock(&pthread_comm.mutex);
    
    pthread_comm.finish = TRUE;

    /* Unlock and signal completion.  */
    pthread_mutex_unlock(&pthread_comm.mutex);
    pthread_cond_signal (&pthread_comm.done);
}

/* Initialize the thread's shared variable */
void threadInitializeVar()
{
    pthread_mutex_lock(&pthread_comm.mutex);
    
    pthread_comm.finish = FALSE;

    /* Unlock and signal completion.  */
    pthread_mutex_unlock(&pthread_comm.mutex);
    pthread_cond_signal (&pthread_comm.done);

}
