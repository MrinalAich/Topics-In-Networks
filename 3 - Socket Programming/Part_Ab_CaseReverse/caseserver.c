#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUFSIZE 1024
#define TRUE 1
#define FALSE 0
#define DEBUG 0

static const int MAXPENDING = 5; // Maximum outstanding connection requests

/* Socket related functions */
ssize_t HandleMessage(int clntSock);
int AcceptTCPConnection(int servSock);

/* Feature related function */
void OperateOnQuery(char* buffer, ssize_t recvLen);

int main(int argc, char ** argv) {

	if (argc != 2) {
		perror("<server port>");
		exit(-1);
	}

	in_port_t servPort = atoi(argv[1]); // Local port

	// create socket for incoming connections
	int servSock;
	if ((servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("socket() failed");
		exit(-1);
	}

	// Set local parameters
	struct sockaddr_in servAddr;
	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_port = htons(servPort);

	// Bind to the local address
	if (bind(servSock, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
		perror("bind() failed");
		exit(-1);
	}

	// Listen to the client
	if (listen(servSock, MAXPENDING) < 0) {
		perror("listen() failed");
		exit(-1);
	}

	// Prepare for using select()
	fd_set orgSockSet; // Set of socket descriptors for select
	FD_ZERO(&orgSockSet);
	FD_SET(STDIN_FILENO, &orgSockSet); // STDIN
	FD_SET(servSock, &orgSockSet);
	int maxDescriptor;
	if (STDIN_FILENO > servSock) {	
		maxDescriptor = STDIN_FILENO;
	} else {
		maxDescriptor = servSock;
	}
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
		memcpy(&currSockSet, &orgSockSet, sizeof(fd_set));

		select(maxDescriptor + 1, &currSockSet, NULL, NULL, &timeOut);

		int currSock;
		for (currSock = 0; currSock < maxDescriptor + 1; currSock++) {

			if (FD_ISSET(currSock, &currSockSet)) {
				// A new client
				// Establish TCP connection, register a new socket to fd_sed to watch with select()
				if (currSock  == servSock) {
					int newClntSock;
					newClntSock = AcceptTCPConnection(servSock);
					FD_SET(newClntSock, &orgSockSet);
					if (maxDescriptor < newClntSock) {
						maxDescriptor = newClntSock;
					}
				}

				// An input from Keybord
				else if (currSock == STDIN_FILENO) {
					printf("Shutting down server\n");
					loopRunning = 0;
				}

				// Echo the message
				else {
					ssize_t recvLen = HandleMessage(currSock);
					if (recvLen == 0) {
						FD_CLR(currSock, &orgSockSet);
					}
				}
			}
		}
	}


	int closingSock;
	for (closingSock = 0; closingSock < maxDescriptor + 1; closingSock++)
		close(closingSock);

	printf("End of Program\n");
}


int AcceptTCPConnection(int servSock) {
	struct sockaddr_in clntAddr;
	socklen_t clntAddrLen = sizeof(clntAddr);

	// Wait for a client to connect
	int clntSock = accept(servSock, (struct sockaddr *) &clntAddr, &clntAddrLen);
	if (clntSock < 0) {
		perror("accept() failed");
		exit(-1);
	}

	char clntIpAddr[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &clntAddr.sin_addr.s_addr, clntIpAddr, sizeof(clntIpAddr)) != NULL) {
		printf("\nHandling client %s %d\n", clntIpAddr, ntohs(clntAddr.sin_port));
	} else {
		puts("\nUnable to get client IP Address");
	}

	return(clntSock);
}

/* Receives query and Sends response */
ssize_t HandleMessage(int clntSock) {
	// Receive data
	char buffer[BUFSIZE];
	memset(buffer, 0, BUFSIZE);

    ssize_t recvLen = recv(clntSock, buffer, BUFSIZE, 0);
	if (recvLen < 0) {
		perror("recv() failed");
		exit(-1);
	}
	buffer[recvLen] = '\0';
    
    if( recvLen > 0 )
    {
        if(DEBUG) printf("String received : |%s|\n", buffer);

        OperateOnQuery(buffer, recvLen);

		// Send the mysql result back to client
		ssize_t sentLen = send(clntSock, buffer, recvLen, 0);
		if (sentLen < 0) {
			perror("send() failed");
			exit(-1);
		} else if (sentLen != recvLen) {
			perror("send() sent unexpected number of bytes");
			exit(-1);
		}
    }

	return(recvLen);
}

/* Performs functionality - LowerCase to UpperCase */
void OperateOnQuery(char* buffer, ssize_t recvLen)
{
    int i;
    for(i=0;i<recvLen;i++)
    {
        if( buffer[i] >= 65 && buffer[i] <=90 )
            buffer[i] += 32;
        else if( buffer[i] >= 97 && buffer[i] <=122 )
            buffer[i] -= 32;
    }
}
