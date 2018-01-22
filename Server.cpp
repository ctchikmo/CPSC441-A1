#include <pthread.h>
#include <string>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <queue>
#include <iostream>
#include <unistd.h>

#include "Server.h"
#include "ClientHandler.h"

Server::Server(int port, int maxClients) 
: port(port),
  maxClients(maxClients)
{
	pthread_mutex_init(&queueMutex, NULL);
	pthread_cond_init(&queueCond, NULL);
	
	for(int i = 0; i < maxClients; i++)
		new ClientHandler(this); // The ClientHandler starts its own pthread, note the ClientHandler deletes itself when done (see threadStart static function). Once the thread is ready it will add itself to the queue. 
	
	// This mutex waiting is done so that we don't start the server early without a clh ready. As soon as one becomes ready this will un-wait and start the server. 
	pthread_mutex_lock(&queueMutex);
	{
		while(CHqueue_available.size() == 0)
			pthread_cond_wait(&queueCond, &queueMutex);
	}
	pthread_mutex_unlock(&queueMutex);
	
	startServer();
}

Server::~Server()
{
	pthread_mutex_destroy(&queueMutex);
	pthread_cond_destroy(&queueCond);
}

void Server::makeAvailable(ClientHandler* cl)
{
	pthread_mutex_lock(&queueMutex);
	{
		CHqueue_available.push(cl);
		pthread_cond_signal(&queueCond); // Signal that a clh is ready. Note that when the program is starting up this will signal when 1 clh is ready, meaning the server can begin startup. 
	}
	pthread_mutex_unlock(&queueMutex);
}

// Big thanks to: https://www.geeksforgeeks.org/socket-programming-cc/ and http://www.bogotobogo.com/cplusplus/sockets_server_client.php
void Server::startServer()
{
	servSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(servSocket < 0)
	{
		std::cout << "There was an error opening the server socket." << std::endl;
		exit(1);
	}
	
	// Setting up bind call address information
	serv_addr.sin_family = AF_INET; // Byte order (IPV4)
	serv_addr.sin_addr.s_addr = INADDR_ANY; // Auto fill with current host ip
	serv_addr.sin_port = htons(port); // Convert port into network byte order. 
	if(bind(servSocket, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
	{
		std::cout << "There was an error binding the server socket." << std::endl;
		exit(1);
	}
	
	if(listen(servSocket, maxClients) < 0) // Start listening for connections, we have a backlog queue size of maxClients.
	{
		std::cout << "There was an error when starting to listen on the server socket." << std::endl;
		exit(1);
	}
	
	directRequests();
}

// Big thanks to: https://www.geeksforgeeks.org/socket-programming-cc/ and http://www.bogotobogo.com/cplusplus/sockets_server_client.php
void Server::directRequests()
{
	int addrLen = sizeof(serv_addr);

	std::cout << "The server is ready to accept requests." << std::endl << std::endl;
	while(true)
	{
		ClientHandler* clh;
		pthread_mutex_lock(&queueMutex);
		{
			while(CHqueue_available.size() == 0)
				pthread_cond_wait(&queueCond, &queueMutex);
			
			clh = CHqueue_available.front();
			CHqueue_available.pop();// Remove the now used ClientHandler, it will add itself back later once it is done its request. 
		}
		pthread_mutex_unlock(&queueMutex);
		
		// At this point a ClientHandler has been secured for use. 
		int clientSocket = accept(servSocket, (struct sockaddr*) &serv_addr, (socklen_t*) &addrLen);
		if(clientSocket < 0)
		{
			std::cout << "There was an error when accepting a connection on the server socket." << std::endl;
			exit(1);
		}
		
		clh->startRequest(clientSocket); // The clh will close the socket for us.
	}
	
	close(servSocket);
}
