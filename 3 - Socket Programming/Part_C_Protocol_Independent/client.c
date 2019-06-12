#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>

#define BUFSIZE 512
#define DEBUG 0
#define LOG_ERROR(x) \
    { \
        perror(x); \
        exit(-1); \
    }

void print(struct addrinfo* sockaddrPtr);

int main(int argc, char *argv[]) // IPAddr, service, transport_protocol, port
{
    struct addrinfo hints;
    struct addrinfo *result, *resPtr;
    int sockfd, s, j;
    size_t len;

    if (argc != 3)
   		LOG_ERROR("input error");

    /* Obtain address(es) matching host/port */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
	hints.ai_flags  = AI_CANONNAME; /* Cannonical Name */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(argv[1], NULL, &hints, &result) != 0)
   		LOG_ERROR("getaddrinfo() failed");

    /* returns one or more addrinfo structures, each
       of which contains an Internet address that can be specified in a call
       to bind(2) or connect(2) */
	in_port_t servPort = atoi(argv[2]);
    for (resPtr = result; resPtr != NULL; resPtr = resPtr->ai_next) 
    {
        if(DEBUG) print(resPtr);

        sockfd = socket(resPtr->ai_family, resPtr->ai_socktype, resPtr->ai_protocol);
        if (sockfd == -1)
            continue;

        /* Port */
        if(resPtr->ai_family == AF_INET) // IPv4
            ((struct sockaddr_in *)resPtr->ai_addr)->sin_port = htons(servPort); 
        else if(resPtr->ai_family == AF_INET6) // IPv6
            ((struct sockaddr_in6 *)resPtr->ai_addr)->sin6_port = htons(servPort);

        if (connect(sockfd, resPtr->ai_addr, resPtr->ai_addrlen) != -1) /* Success */
            break;

        close(sockfd);
    }

    freeaddrinfo(result);

    if (resPtr == NULL)               /* No address succeeded */
        LOG_ERROR("Could not connect\n");

    // Loopaction
    while(1) {
        printf("Type string: ");
        char buffer[BUFSIZE];
        memset(buffer, 0, BUFSIZE);
        fgets(buffer, BUFSIZE, stdin);

        if (strncmp(buffer, "BYE", 3) == 0)
            break;

        size_t bufferLen = strlen(buffer);

        // Send string to server
        ssize_t sentLen = send(sockfd, buffer, bufferLen, 0);
        if (sentLen < 0)
        {
            LOG_ERROR("send() failed");
        }
        else if (sentLen != bufferLen)
        {
            LOG_ERROR("send(): sent unexpected number of bytes");
        }

        // Receive string from server
        ssize_t recvLen = recv(sockfd, buffer, BUFSIZE - 1, 0);
        if (recvLen < 0)
        {
            LOG_ERROR("recv() failed");
        }
        else if (recvLen == 0)
        {
            LOG_ERROR("recv() connection closed prematurely");
        }
        printf("Received: %s\n", buffer);
    }

    close(sockfd);
    exit(0);
}

void print(struct addrinfo* sockaddrPtr)
{
    printf("result->ai_flags = %d\n", sockaddrPtr->ai_flags);
    printf("result->ai_family = %d\n", sockaddrPtr->ai_family);
    printf("result->ai_socktype = %d\n", sockaddrPtr->ai_socktype);
    printf("result->ai_protocol = %d\n", sockaddrPtr->ai_protocol);
    printf("result->ai_addrlen = %zd\n", (ssize_t)sockaddrPtr->ai_addrlen);
    printf("result->ai_canonname = %s\n", sockaddrPtr->ai_canonname);

    return;
}

