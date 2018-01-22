#include <pthread.h>
#include <string>
#include <iostream>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <stdlib.h>
#include <cstring>
#include <string>
#include <arpa/inet.h>
#include<netdb.h> //hostent

#include "ClientHandler.h"
#include "Server.h"

#define HTTP_PORT 80 // NOTE: WE ARE NOT DOING HTTPS!! (Bless cause that was going to be really tough otherwise.)

ClientHandler::ClientHandler(Server* server)
: server(server),
  flag_reqMade(false)
{
	pthread_mutex_init(&reqMutex, NULL);
	pthread_cond_init(&reqCond, NULL);
	
	int rv = pthread_create(&thread, NULL, threadStarter, this);
	if (rv != 0)
	{
		std::cout << "Error creating thread, exiting program" << std::endl;
		exit(-1);
	}
}

ClientHandler::~ClientHandler()
{
	pthread_mutex_destroy(&reqMutex);
	pthread_cond_destroy(&reqCond);
}

// This method is called by the main thread when it grabs a client, however the request is handled by the ClientHandler thread. 
void ClientHandler::startRequest(int cliSock)
{
	pthread_mutex_lock(&reqMutex);
	{
		browserSocket = cliSock;
		flag_reqMade = true;
		pthread_cond_signal(&reqCond); // This will cause the wait to stop waiting and begin handleRequest with the user data. 
	}
	pthread_mutex_unlock(&reqMutex);
}

void ClientHandler::wait()
{
	while(true)
	{
		pthread_mutex_lock(&reqMutex);
		{
			close(browserSocket);
			close(webServerSocket);
			server->makeAvailable(this);
			while (!flag_reqMade)
				pthread_cond_wait(&reqCond, &reqMutex);
		}
		pthread_mutex_unlock(&reqMutex);
		
		// At this point the Server can not interfere with this handler, the value is not in the queue, and is instead maintained by the pthread. So we can now handle the clients request. 
		// This is also why the mutex has been unlocked already. 
		handleRequest();
	}
}

// Big thanks to: https://www.geeksforgeeks.org/socket-programming-cc/ and http://www.bogotobogo.com/cplusplus/sockets_server_client.php
void ClientHandler::handleRequest() // During handle request there is no interference!! This means I do not need to worry about mutexes
{
	char browserInput[1024] = {0}; // This the request data sent from the browser. 
	recv(browserSocket, browserInput, sizeof(browserInput) - 1, 0); // sizeof() - 1 so that there is room for the /0
	str_browserInput.assign(browserInput);
	std::cout << str_browserInput;
	
	// Check for the HOST line, which tells us what ip and port to forward the users tcp request to. 
	connectWebserver();
	
	// Now we begin handling the data
	char test[1500] = {0};
	send(webServerSocket, browserInput, sizeof(browserInput) - 1, 0);
	recv(webServerSocket, test, sizeof(test) - 1, 0);
	std::cout << test;
	send(browserSocket, test, sizeof(test) - 1, 0);
	
	recv(browserSocket, browserInput, sizeof(browserInput) - 1, 0); // sizeof() - 1 so that there is room for the /0
	str_browserInput.assign(browserInput);
	std::cout << str_browserInput;
	
	exit(1);
	
	flag_reqMade = false;
	wait(); // We are done the user request, time to wait for another. 
}

// Big thanks to: https://www.geeksforgeeks.org/socket-programming-cc/ and http://www.bogotobogo.com/cplusplus/sockets_server_client.php
// Note that ports to connect on will either be 80 or 443 as determined by whether the request is http or https respecively.
void ClientHandler::connectWebserver()
{
	// Until I start building the socket, I'm extracting the host (web server) data from the browsers first http request.
	int hostPos = str_browserInput.find("Host:") + 6;
	std::string str_hostIP;
	
	for(; str_browserInput.at(hostPos) != ':' && str_browserInput.at(hostPos) != '\r' && str_browserInput.at(hostPos) != '\n'; hostPos++)
		str_hostIP += str_browserInput.at(hostPos);
	
	// Start building the socket
	webServerSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(webServerSocket < 0)
	{
		std::cout << "Error creating a client - webserver socket" << std::endl;
		exit(1);
	}
	
	memset(&webServerAddr, '0', sizeof(webServerAddr)); // Set all of the addr to be 0 chars.
	webServerAddr.sin_family = AF_INET;
	webServerAddr.sin_port = htons(HTTP_PORT);
	
	// Converting to binary form
	if(inet_pton(AF_INET, str_hostIP.c_str(), &webServerAddr.sin_addr) != 1)
	{
		bool error = false;
		
		// We might have an error and go in here because the address is not an ip, rather its text such as www.xxx.com, thus we handle that check next.
		char ipFromName[100];
		if(hostnameToIP(str_hostIP.c_str(), ipFromName) == 0)
		{
			if(inet_pton(AF_INET, ipFromName, &webServerAddr.sin_addr) != 1)
				error = true;
		}
		else
			error = true;
		
		if(error)
		{
			std::cout << "Invalid web server address / address not supported." << std::endl;
			exit(1);	
		}
	}
	
	// Connecting
	if(connect(webServerSocket, (struct sockaddr *)&webServerAddr, sizeof(webServerAddr)) < 0)
	{
		std::cout << "Connection to web server failed." << std::endl;
		exit(1);
	}
}

// Help start off the pthread. The function is run from the newly created pthread. 
void* ClientHandler::threadStarter(void* cl)
{
	ClientHandler* clientHandler = (ClientHandler*)cl;
	clientHandler->wait();
	
	delete clientHandler;
	return NULL;
}

// Thank you for helping to resolve hostname to an ip http://www.binarytides.com/hostname-to-ip-address-c-sockets-linux/
int ClientHandler::hostnameToIP(const char* hostname, char* rvIP)
{
	struct hostent *he;
    struct in_addr **addr_list;
    int i;
    
    if((he = gethostbyname(hostname)) == NULL) 
		return 1;
 
    addr_list = (struct in_addr **) he->h_addr_list;
    
    for(i = 0; addr_list[i] != NULL; i++) 
    {
        strcpy(rvIP , inet_ntoa(*addr_list[i])); // Return the first addr
        return 0;
    }
    
    return 1;
}













