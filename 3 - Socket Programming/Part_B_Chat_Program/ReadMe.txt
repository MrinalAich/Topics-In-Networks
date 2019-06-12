Implemented : 1	server	and	N	clients.
Say	N clients may	connect	to	the	server.		Client	1	wants	to	talk with client m.
How	do	you	manage	multiple	clients	and	select	the	one	you	want to	talk to?


Features:
1. Client will be shown a list of Active Idle Clients in the application, after successful login.
2. Client may choose one of the other clients to establish a one-to-one chat session, without his/her permission.
3. After establishing connection, both the users may chat with one another on a one-to-one basis (via server).
4. While leaving, if there exists connection with another Client, it will be safely closed and the other client stays inside the application.
5. Server maintains a Database that record's new client's and deletes outgoing client's. (Mapping done w.r.t. sockfd of client's in Server).
6. A client has the following commands:
    Request for the List of Active Idle Clients. = "REQUEST"
    Connect to a Specific Client                 = "CONNECT:<remoteUsername>"
    Leaving the Application                      = "bye" or "Ctrl-C"

Assumptions:
1. Client on connect, should mention its 'unique' Username.
2. If client-1 chooses to connect to idle client-2, the client-2 will automatically connect. No permission will be requested.
3. User input on Server indicates 'Stop Application'.
4. Client may uses the following commands:
    Request for the List of Active Idle Clients. = "REQUEST"
    Connect to a Specific Client                 = "CONNECT:<remoteUsername>"
    Leaving the Application                      = "bye" or "Ctrl-C"


Technical:
1. Client maintains two threads, one to receive from Socket, and another to read from StdIn.
2. Server maintain sockfd's in FD_SET data structure, and uses select function.


Constraints:
1. At max, 30 clients can be supported at once.
2. Username cannot exceed 16 characters.
3. Only Unique Usernames allowed.




