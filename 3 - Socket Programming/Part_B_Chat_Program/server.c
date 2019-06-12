#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>

#define TRUE 1
#define FALSE 0

#define MAXPENDING 32 // Maximum outstanding connection requests
#define BUFSIZE 1024
#define MAX_USERNAME_LEN 16
#define DEBUG 0
#define LOG_ERROR(x) \
    { \
        perror(x); \
        exit(-1); \
    }

typedef struct ClientNode {
    bool valid;
    bool busy;
    char username[MAX_USERNAME_LEN];
    int sockfd;
} ClientInfo;

/* Message Passing function */
ssize_t HandleMessage(ClientInfo* clientInfo, int clntSock); 

/* Client Database maintainence related functions */
int SaveUserNameOfClient(ClientInfo* clientInfo, int clntSock);
void GetClientListFromDB(ClientInfo* clientInfo, char* list, int clntSock);
bool ValidateUsername(ClientInfo* clientInfo, char *buffer);
int GetUserFDByNameFromDB(ClientInfo* clientInfo, char* username);
void PrintDB(ClientInfo *clientInfo);

/* Socket related functions */
int AcceptTCPConnection(ClientInfo* clientInfo, int servSock);
void CloseConnectionIfExists(ClientInfo* clientInfo, int clntSock);

/* Misc Functions */
void SendConnResponse(ClientInfo* clientInfo, int clntSock, int otherClntSock, bool connEstablished, char* response);
int max( int val1, int val2 ); 

/* Main */
int main(int argc, char ** argv) {

    ClientInfo clientInfo[MAXPENDING] = {0,0,"", 0};

	if (argc != 2)
        LOG_ERROR("<server port>")

	in_port_t servPort = atoi(argv[1]); // Local port

	// create socket for incoming connections
	int servSock;
	if ((servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        LOG_ERROR("socket() failed");

	// Set local parameters
	struct sockaddr_in servAddr;
	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_port = htons(servPort);

	// Bind to the local address
	if (bind(servSock, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0)
        LOG_ERROR("bind() failed");

	// Listen to the client
	if (listen(servSock, MAXPENDING) < 0) 
		LOG_ERROR("listen() failed");

	// Prepare for using select()
	fd_set orgSockSet; // Set of socket descriptors for select
	FD_ZERO(&orgSockSet);
	FD_SET(STDIN_FILENO, &orgSockSet); // STDIN
	FD_SET(servSock, &orgSockSet);
	int maxDescriptor = max(STDIN_FILENO, servSock);

	// Setting select timeout as Zero makes select non-blocking
	struct timeval timeOut;
	timeOut.tv_sec = 0; // 0 sec
	timeOut.tv_usec = 0; // 0 microsec

	// Server Loop
	int loopRunning = 1;
	while (loopRunning) {
		// The following process has to be done every time
		// because select() overwrite fd_set.
		fd_set currSockSet;
        int currSock;
		memcpy(&currSockSet, &orgSockSet, sizeof(fd_set));

      	timeOut.tv_sec = 2; // 0 sec
    	timeOut.tv_usec = 0; // 0 microsec

		select(maxDescriptor + 1, &currSockSet, NULL, NULL, &timeOut);

		for (currSock = 0; currSock < maxDescriptor + 1; currSock++)
        {
            // Check which SocketFD is SET
            if (FD_ISSET(currSock, &currSockSet))
            {
                if (currSock  == servSock) {
                    int newClntSock;
                    newClntSock = AcceptTCPConnection(clientInfo, servSock);
                    FD_SET(newClntSock, &orgSockSet);
                    maxDescriptor = max( maxDescriptor, newClntSock);
                }
                // An input from Keybord
                else if (currSock == STDIN_FILENO)
                {
                    printf("Server Application closes.\n");
                    loopRunning = 0;
                }
                // Input from the client
                else
                {
                    ssize_t recvLen = HandleMessage(clientInfo, currSock);
                    if (recvLen == 0)
                    {
                        if(DEBUG) printf("Connection Closed of %s.\n", clientInfo[currSock].username);
                        CloseConnectionIfExists(clientInfo, currSock);
                        FD_CLR(currSock, &orgSockSet);
                        close(currSock);
                    }
                }
            }
        }
	}

    close(servSock);
	printf("End of Program\n");

}

/* Checks whether Client was connected to a remote node. If yes, Disconnects it.*/
void CloseConnectionIfExists(ClientInfo* clientInfo, int clntSock)
{
    int otherClntInfo = clientInfo[clntSock].sockfd;
    if(otherClntInfo && clientInfo[otherClntInfo].valid )
    {
        char buffer[32] = "bye";
        ssize_t buffLen = 3;

        if(DEBUG) printf("Had another connection: Properly closing |%s| : received: |%s|\n", clientInfo[otherClntInfo].username, buffer);
        clientInfo[otherClntInfo].busy = FALSE;
        clientInfo[otherClntInfo].sockfd = 0;

        ssize_t sentLen = send(otherClntInfo, buffer, buffLen, 0);
        if (sentLen < 0)
        {
            LOG_ERROR("send() failed");
        }
        else if (sentLen != buffLen)
        {
            LOG_ERROR("send() sent unexpected number of bytes");
        }
    }
}

/* Accept TCP Connection over the Server Socket */
int AcceptTCPConnection(ClientInfo* clientInfo, int servSock)
{
	struct sockaddr_in clntAddr;
	socklen_t clntAddrLen = sizeof(clntAddr);

	// Wait for a client to connect
	int clntSock = accept(servSock, (struct sockaddr *) &clntAddr, &clntAddrLen);
	if (clntSock < 0) {
		perror("accept() failed");
		exit(-1);
	}

	char clntIpAddr[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &clntAddr.sin_addr.s_addr, clntIpAddr, sizeof(clntIpAddr)) != NULL)
		printf("\nHandling client %s %d\n", clntIpAddr, ntohs(clntAddr.sin_port));
    else 
		puts("----\nUnable to get client IP Address");

    // Add UserName of the Client to Database'
    int err;
    do {
        err = SaveUserNameOfClient(clientInfo, clntSock);
        if( !err )
            printf("Failed to Save UserName of Client. Try Again.");
    } while(!err);


	return(clntSock);
}

/* Validates a username given by Client from the DB */
bool ValidateUsername(ClientInfo* clientInfo, char *buffer)
{
    short i;
    for(i=0;i<MAXPENDING;i++)
    {
        if( clientInfo[i].valid && 
            strncmp(clientInfo[i].username, buffer, strlen(clientInfo[i].username)) )
            return TRUE;
    }
    return FALSE;
}

/* Debugging : Prints the List of all clients in DB */
void PrintDB(ClientInfo *clientInfo)
{
    int i;
    for(i=0;i<16;i++)
    {
        if( clientInfo[i].valid && DEBUG)
            printf("Connected to: %u |%s|\n", clientInfo[i].sockfd, clientInfo[i].username);
    }
}

/* Retreives Sockfd of Client using Client's Username */
int GetUserFDByNameFromDB(ClientInfo* clientInfo, char* username)
{
    short i;
    for(i=0;i<MAXPENDING;i++)
    {
        if(clientInfo[i].valid && !strcmp(username, clientInfo[i].username))
            return i;
    }
}

/* Response to Client's Connection Request */
void SendConnResponse(ClientInfo* clientInfo, int clntSock, int otherClntSock, bool connEstablished, char* response)
{
    char buffer[BUFSIZE] = {0};
    if( connEstablished )
    {
        printf("Connection established between %s and %s...\n", clientInfo[otherClntSock].username, clientInfo[clntSock].username);

        strcpy(buffer, "Server: You are connected to ");
        strcat(buffer, clientInfo[clntSock].username);
        send(otherClntSock, buffer, strlen(buffer), 0); // TODO Improve Code

        memset(buffer, 0x00, sizeof(buffer));
        strcpy(buffer, "Server: You are connected to ");
        strcat(buffer, clientInfo[otherClntSock].username);
        send(clntSock, buffer, strlen(buffer), 0);
    }
    else
    {
        strcpy(buffer, "Server:");
        strcat(buffer, response);
        send(clntSock, buffer, strlen(buffer), 0);
    }
}

/* Handles messages received from Client */
ssize_t HandleMessage(ClientInfo* clientInfo, int clntSock) 
{
	// Receive data from the Client
	char buffer[BUFSIZE];
	memset(buffer, 0, BUFSIZE);
	ssize_t recvLen = recv(clntSock, buffer, BUFSIZE, 0);
	if (recvLen < 0) 
    {
		LOG_ERROR("recv() failed");
        return 0;
	}
	buffer[recvLen] = '\0';

    if( strncmp(buffer, "RequestForActiveClientList", 26) == 0 )
    {
        char list[BUFSIZE];
        int sentLen;
        memset(list, 0, BUFSIZE);

        GetClientListFromDB(clientInfo, list, clntSock);

        sentLen = send(clntSock, list, strlen(list), 0);
        if (sentLen < 0)
        {
            LOG_ERROR("send() failed");
        }
        else if (sentLen != strlen(list))
        {
            LOG_ERROR("send() sent unexpected number of bytes");
        }
    }
    else if(strncmp(buffer, "CONNECT:", 8) == 0 )
    {
        bool connEstab = TRUE;
        int otherClntSock = 0;
        if( (clientInfo[clntSock].valid) && (clientInfo[clntSock].sockfd == 0))  // Client is not in a P2P connection.
        {
            char rspStr[64] ;
            memset(rspStr, 0x00, sizeof(rspStr));
            if(ValidateUsername(clientInfo, &buffer[8]))
            {
                otherClntSock = GetUserFDByNameFromDB(clientInfo, &buffer[8]);
                if(MAXPENDING == otherClntSock)
                {
                    strcpy(rspStr, "Invalid User.");
                    connEstab = FALSE;
                }
                else if(otherClntSock == clntSock)
                {
                    strcpy(rspStr, "Invalid Operation.");
                    connEstab = FALSE;
                }
                else
                {
                    // Other Client is busy with someone
                    if( clientInfo[otherClntSock].sockfd )
                    {
                        strcpy(rspStr, "User is busy.");
                        connEstab = FALSE;
                    }
                    // Client is Idle, establish connection
                    else
                    {
                        clientInfo[clntSock].sockfd = otherClntSock;
                        clientInfo[otherClntSock].sockfd = clntSock;

                        clientInfo[clntSock].busy = 1;
                        clientInfo[otherClntSock].busy = 1;

                        connEstab = TRUE;
                    }
                }
            }
            else
            {
                strcpy(rspStr, "Invalid User.");
                connEstab = FALSE;
            }
            SendConnResponse(clientInfo, clntSock, otherClntSock, connEstab, rspStr);
            return(recvLen);
        }
    }
    else if(strncmp(buffer, "bye", 3) == 0 || recvLen == 0) // Connection close by remote end
    {
        int otherClntInfo = clientInfo[clntSock].sockfd;

        CloseConnectionIfExists(clientInfo, clntSock);
        
        printf("Connection Closed by |%s|\n", clientInfo[clntSock].username);
        memset(&clientInfo[clntSock], 0x00, sizeof(clientInfo[clntSock]));
    }
    else
    {
        int otherClntInfo = clientInfo[clntSock].sockfd;

        if(clientInfo[otherClntInfo].valid)
        {
        	char ModBuffer[BUFSIZE];
        	memset(ModBuffer, 0x00, BUFSIZE);

            strcpy(ModBuffer, clientInfo[clntSock].username);
            strcat(ModBuffer, ":");
            strcat(ModBuffer, buffer);
            if(DEBUG) printf("|%s| -> |%s| : |%s| --- |%s|\n", clientInfo[clntSock].username, clientInfo[otherClntInfo].username, buffer, ModBuffer);
            recvLen = recvLen + strlen(clientInfo[clntSock].username) + 1;
            ssize_t sentLen = send(otherClntInfo, ModBuffer, recvLen, 0);
            if (sentLen < 0)
            {
                LOG_ERROR("send() failed");
            }
            else if (sentLen != recvLen)
            {
                LOG_ERROR("send() sent unexpected number of bytes");
            }
        }
    }

	return(recvLen);
}

/* Retrieves the List of Idle Clients */
void GetClientListFromDB(ClientInfo* clientInfo, char* list, int clntSock)
{
    int i, ret = FALSE;
    strcpy(list, "\nServer: Active Users are: \n");
    for(i=0;i<MAXPENDING;i++)
    {
        if( i != clntSock && clientInfo[i].valid == TRUE && clientInfo[i].busy == FALSE)
        {
            strcat(list, clientInfo[i].username);
            strcat(list, "\n");
            ret = TRUE;
        }
    }
    if( !ret )
        strcpy(list, "\nServer:No other users are active now.");
}

/* Adds UserName of Client into the DB */
int SaveUserNameOfClient(ClientInfo* clientInfo, int clntSock)
{
	// Receive data from the Client
    int ret = TRUE;
	char buffer[BUFSIZE], clientList[BUFSIZE];
	memset(buffer, 0, BUFSIZE);
    memset(clientList, 0, BUFSIZE);
	ssize_t recvLen = recv(clntSock, buffer, BUFSIZE, 0);
	if (recvLen < 0) 
    {
		LOG_ERROR("recv() failed");
        return FALSE;
	}
	buffer[recvLen] = '\n';
    // Username
    strcpy(clientInfo[clntSock].username, buffer);
    GetClientListFromDB(clientInfo, clientList, clntSock);

    clientInfo[clntSock].valid = 1;
    clientInfo[clntSock].busy = 0;
    clientInfo[clntSock].sockfd = 0;

    strcpy(buffer, "Welcome to Mrinal's Chat Program: ");
    strcat(buffer, clientInfo[clntSock].username);
    strcat(buffer, clientList);
    recvLen = strlen(buffer);
    ssize_t sentLen = send(clntSock, buffer, recvLen, 0);
    if (sentLen < 0)
    {
        LOG_ERROR("send() failed");
        ret = FALSE;
    }
    else if (sentLen != recvLen)
    {
        LOG_ERROR("send() sent unexpected number of bytes");
        ret = FALSE;        
    }
    return ret;
}

int max( int val1, int val2 )
{
    return val1 > val2 ? val1 : val2;
}
