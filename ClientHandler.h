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
		int browserSocket = -1; // This socket is already setup by connecting to the server, thus I can just read and write with it.
		
		// Webserver variables
		int webServerSocket = -1;
		struct sockaddr_in webServerAddr;
		
		void handleRequest();
		bool connectWebserver(const std::string &str_initBrowserReq);
		
		std::string getStringAt(const std::string &str_key, const std::string &str_src);
		int stringToInt(const std::string &s);
		static void* threadStarter(void* cl);
		static int hostnameToIP(const char* hostname, char* rvIP);
};

#endif
