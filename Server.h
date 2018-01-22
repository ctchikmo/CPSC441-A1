#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include <queue>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include "ClientHandler.h"

/* The server class will listen for a user browser request from the port entered on startup.
 * When the request is detected the server class will pass the data to an available ClientHandler task, which sends this request to the actual webserver (of course only downloading small chuncks at a time if html).
*/ 
class Server
{
	public:
		int port = 0;
		int maxClients = 0;
		
		Server(int port, int maxClients);
		~Server();
		
		void makeAvailable(ClientHandler* cl);
		
	private:
		int servSocket;
		struct sockaddr_in serv_addr; // Socket Addressing information. 
		pthread_mutex_t queueMutex;
		pthread_cond_t queueCond;
		std::queue<ClientHandler*> CHqueue_available;  					//TODO::::: ADD LATER SOMETING TO HANDLE EXCESS REQUESTS
		
		void startServer();
		void directRequests();
};

#endif
