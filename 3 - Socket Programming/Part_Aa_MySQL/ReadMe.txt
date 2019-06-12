Packages required:
-------------------
mysql-server
libmysqlclient-dev


Execute the mysql-server:
------------------------
Start : /etc/init.d/mysql start
Stop  : /etc/init.d/mysql stop


Special Compilation:
---------------------
gcc $(mysql_config --cflags) sqlserver.c $(mysql_config --libs) -o mysqlserver
gcc sqlclient.c -o sqlclient


Sample Input:
--------------
./server 3333 root 123 127.0.0.1 client_server_application 3306


Constraint:
--------------
1. mysqlserver should contain a database 'client_server_application' which the server program would try to connect.
2. Check the port mysql-server is running, use it as the command line argument. You may find it in this file : /etc/mysql/my.cnf
3. The database specified on command line should exists inside the mysql-server.


Sample mysql commands:
-----------------------
DROP TABLE authors;
CREATE TABLE authors (id INT, name VARCHAR(20), email VARCHAR(20));

INSERT INTO authors (id,name,email) VALUES(1,"Vivek","xuz@abc.com");
INSERT INTO authors (id,name,email) VALUES(2,"Priya","p@gmail.com");
INSERT INTO authors (id,name,email) VALUES(3,"Tom","tom@yahoo.com");
