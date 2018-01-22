#ifndef CLIENT_HANDLER__H
#define CLIENT_HANDLER__H

#include <pthread.h>
#include <string>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

class Server; // Forward declaration for compiler. 

class ClientHandler
{
	public:
		ClientHandler(Server* server); // Server is needed so that we can readd this handler back to the available queue once it's done with a request. 
		~ClientHandler();
		
		void startRequest(int cliSock);
		void wait(); // This is public so that the pthread can access it later. 
		
	private:
		Server* server;
		bool flag_reqMade;
		pthread_t thread;
		pthread_mutex_t reqMutex;
		pthread_cond_t reqCond;
		
		// Browser variables. 
		int browserSocket = 0; // This socket is already setup by connecting to the server, thus I can just read and write with it.
		std::string str_browserInput; // String holding browser input.
		char returnToBrowser[1024] = {0}; // This is the full response from the webServer, the buffer will be filled after each range request and sent to the browser upon completion. 
		
		// Webserver variables
		int webServerSocket = 0;
		struct sockaddr_in webServerAddr;
		
		void handleRequest();
		void connectWebserver();
		static void* threadStarter(void* cl);
		static int hostnameToIP(const char* hostname, char* rvIP);
};

#endif
