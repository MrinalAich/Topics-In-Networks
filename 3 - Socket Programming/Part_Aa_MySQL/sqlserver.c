#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <mysql/mysql.h>

#define BUFSIZE 1024
#define NAMEBUFSIZE 128
#define TRUE 1
#define FALSE 0
#define DEBUG 0

static const int MAXPENDING = 16; // Maximum outstanding connection requests
struct mysql_params {
    char user[NAMEBUFSIZE];
    char password[NAMEBUFSIZE];
    char host[NAMEBUFSIZE];
    char dbname[NAMEBUFSIZE];
    unsigned int port;
} mysql_init_params;


/* Socket related functions */
ssize_t HandleMessage(MYSQL* conn, int clntSock);
int AcceptTCPConnection(int servSock);

/* Mysql related functions */
bool InitializeMYSQL(MYSQL** conn, char ** argv);
void OperateOnMYSQL(MYSQL* conn, char *query, ssize_t query_len, char *result);

int main(int argc, char ** argv) {

	if (argc != 7) {
		perror("<server port> <mysqlserver-username> <mysqlserver user-password> <host> <database> <mysqlserver port>");
		exit(-1);
	}

	in_port_t servPort = atoi(argv[1]); // Local port

    MYSQL* conn;
    // Initialize MySql Server
    if( InitializeMYSQL(&conn, argv) == 0 ) {
        perror("MySql server initialization failed");
		exit(-1);
	}

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
					ssize_t recvLen = HandleMessage(conn, currSock);
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

    mysql_close(conn);
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
ssize_t HandleMessage(MYSQL *conn, int clntSock) {
	// Receive data
	char buffer[BUFSIZE], result[BUFSIZE];
	memset(buffer, 0, BUFSIZE);
  	memset(result, 0, BUFSIZE);

    ssize_t recvLen = recv(clntSock, buffer, BUFSIZE, 0);
	if (recvLen < 0) {
		perror("recv() failed");
		exit(-1);
	}
	buffer[recvLen] = '\0';
    
    if( recvLen > 0 )
    {
        if(DEBUG) printf("String received : |%s|\n", buffer);

        // Do operation on MySQL server
        OperateOnMYSQL(conn, buffer, recvLen, result);

		// Send the mysql result back to client
		ssize_t sentLen = send(clntSock, result, strlen(result), 0);
		if (sentLen < 0) {
			perror("send() failed");
			exit(-1);
		} else if (sentLen != strlen(result)) {
			perror("send() sent unexpected number of bytes");
			exit(-1);
		}
    }

	return(recvLen);
}

/* Connects to the mysql-server in the localhost */
bool InitializeMYSQL(MYSQL** conn, char ** argv)
{
    char *user = (char*)argv[2];
    char *password = (char*)argv[3];
    char *host = (char*)argv[4];
    char *dbname = (char*)argv[5];
    unsigned int port = atoi((char*)argv[6]);
    *conn = mysql_init(NULL); // Initialize the mysql structue

    if(!mysql_real_connect(*conn, host, user, password, dbname, port, NULL, 0))
    {
        printf("\nError: %s[%d]\n", mysql_error(*conn), mysql_errno(*conn));
        return FALSE;
    }

    return TRUE;
}

/* Performs query on mysql and returs query-result */
void OperateOnMYSQL(MYSQL* conn, char *query, ssize_t query_len, char *result)
{
    MYSQL_RES *res;
    MYSQL_ROW row;

    mysql_query(conn, query);

    res = mysql_store_result(conn);
    if( res == NULL)
    {
        if( mysql_errno(conn)!=0 )
            sprintf(result, "\nError: %s[%d]\n", mysql_error(conn), mysql_errno(conn));
        else
            sprintf(result, "\nResult Successful.\n");
    }
    else
    {
        if(mysql_num_rows(res) == 0 )
            strcat(result, "Empty set");
        else
        {
            unsigned int noColumns = mysql_num_fields(res);
            char buf[128];
            int i;
            while( row = mysql_fetch_row(res) )
            {
                for(i = 0; i < noColumns - 1; i++ )
                {
                    memset(buf, 0x00, sizeof(buf));
                    sprintf(buf, "%s\t|", row[i]);
                    strcat(result, buf);
                }
                memset(buf, 0x00, sizeof(buf));
                sprintf(buf, "%s\n", row[noColumns-1]);
                strcat(result, buf);
            }
        }
    }
    mysql_free_result(res);
}


