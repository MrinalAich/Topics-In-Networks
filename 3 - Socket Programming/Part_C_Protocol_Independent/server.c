#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#define BUFSIZE 1024
#define LOG_ERROR(x) \
    { \
        perror(x); \
        exit(-1); \
    }

static const int MAXPENDING = 5; // Maximum outstanding connection requests

int AcceptTCPConnection(int servSock);
ssize_t HandleMessage(int clntSock);
int max(int val1, int val2);

int main(int argc, char ** argv) {

	if (argc != 2)
        LOG_ERROR("<server port>");

	in_port_t servPort = atoi(argv[1]); // Local port

	// create socket for incoming connections
	int servSock;
	if ((servSock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)) < 0)
		LOG_ERROR("socket() failed");

	// Set local parameters
	struct sockaddr_in6 servAddr;
	memset(&servAddr, 0x00, sizeof(servAddr));
	servAddr.sin6_family = AF_INET6;
	servAddr.sin6_addr = in6addr_any;
	servAddr.sin6_port = htons(servPort);

	// Bind to the local address
	if (bind(servSock, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0)
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
		memcpy(&currSockSet, &orgSockSet, sizeof(fd_set));

		select(maxDescriptor + 1, &currSockSet, NULL, NULL, &timeOut);

		int currSock;
		for (currSock = 0; currSock < maxDescriptor + 1; currSock++) {
			if (FD_ISSET(currSock, &currSockSet)) {

				// A new client
				// Establish TCP connection,
				// register a new socket to fd_sed to watch with select(),
				// and echo first message
				if (currSock  == servSock) {
					int newClntSock = AcceptTCPConnection(servSock);
					FD_SET(newClntSock, &orgSockSet);
                    maxDescriptor = max(maxDescriptor, newClntSock);
				}

				// An input from Keybord
				else if (currSock == STDIN_FILENO) {
					printf("Shutting down server\n");
					loopRunning = 0;
				}

				// Input from an existing client, Echo back message
				else {
					ssize_t recvLen = HandleMessage(currSock);
					if (recvLen == 0)
						FD_CLR(currSock, &orgSockSet);
				}
			}
		}
	}

	int closingSock;
	for (closingSock = 0; closingSock < maxDescriptor + 1; closingSock++) {
		close(closingSock);
	}
	printf("End of Program\n");
}


int AcceptTCPConnection(int servSock)
{
	struct sockaddr_storage clntAddr;
    int clntSock, clntAddrLen;
    char buffer[BUFSIZE];
    memset(buffer, 0x00, sizeof(buffer));

    clntSock = accept(servSock, (struct sockaddr *)&clntAddr, &clntAddrLen);
     if (clntSock < 0 )
         LOG_ERROR("accept() failed");

    if(clntAddr.ss_family == AF_INET6)
    {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&clntAddr;
        if(inet_ntop(AF_INET6, &(sin6->sin6_addr), buffer, sizeof(buffer)))
            printf(" Handling Client: |%s|, port: |%u|\n", buffer, ntohs(sin6->sin6_port));
    }

	return(clntSock);
}

ssize_t HandleMessage(int clntSock)
{
    // Receive data
    char buffer[BUFSIZE];
    memset(buffer, 0, BUFSIZE);
    ssize_t recvLen = recv(clntSock, buffer, BUFSIZE, 0);
    if (recvLen < 0)
        LOG_ERROR("recv() failed");

    buffer[recvLen-1] = '\0';

    // Send the received data back to client
    ssize_t sentLen = send(clntSock, buffer, recvLen, 0);
    if (sentLen < 0)
    {
        LOG_ERROR("send() failed");
    }
    else if (sentLen != recvLen)
    {
        LOG_ERROR("send() sent unexpected number of bytes");
    }
    return(recvLen);
}

int max(int val1, int val2)
{
    return val1 > val2 ? val1 : val2; 
}
