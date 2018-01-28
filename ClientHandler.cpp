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
#define HEADER_RESPONSE_EST_SIZE 1500 // Im hoping I provide a large enough buffer. 

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
			server->makeAvailable(this);
			while (!flag_reqMade)
				pthread_cond_wait(&reqCond, &reqMutex);
		}
		pthread_mutex_unlock(&reqMutex);
		
		// At this point the Server can not interfere with this handler, the value is not in the queue, and is instead maintained by the pthread. So we can now handle the clients request. 
		// This is also why the mutex has been unlocked already. 
		handleRequest();
		
		flag_reqMade = false;
		close(browserSocket);
		close(webServerSocket);
	}
}

// Small thanks to: https://www.geeksforgeeks.org/socket-programming-cc/ and http://www.bogotobogo.com/cplusplus/sockets_server_client.php
// Once this method is done it will return to where it was called in the wait method. 
// Note: I am forcing a Connection: close, thus we need to reconnect to the webserver before every send
void ClientHandler::handleRequest() // During handle request there is no interference!! This means I do not need to worry about mutexes
{
	// Handle this request, as well as anything else this request needs from the webserver. Furture browser requests create a new ClientHandler. 
	char browserInput[HEADER_RESPONSE_EST_SIZE] = {0}; // This the request data sent from the browser. (header size is not known pre fetch, so must over est. it).
	bool endOfBrowserIn = false;
	int browserInBytes = 0;
	while(!endOfBrowserIn)
	{
		int rv = recv(browserSocket, browserInput + browserInBytes, HEADER_RESPONSE_EST_SIZE - browserInBytes, 0);
		browserInBytes += rv;
		if((browserInBytes == 2 && browserInput[browserInBytes - 2] == '\r' && browserInput[browserInBytes - 1] == '\n') || (browserInBytes > 4 && browserInput[browserInBytes - 4] == '\r' && browserInput[browserInBytes - 3] == '\n' && browserInput[browserInBytes - 2] == '\r' && browserInput[browserInBytes - 1] == '\n') || rv == 0)
			endOfBrowserIn = true;
	}
	std::string str_browserInput(browserInput, browserInBytes);
	unsigned int connLoc = str_browserInput.find("Connection: "); // This bit with the Connection: part forces everything to be as Connection: closed, as I'm not implementing a timing function to handle keep-alive. 
	if(connLoc != std::string::npos)
	{
		str_browserInput.replace(connLoc + 12, 10, "close"); // This is fine as keep-alive is longer than close, so it can replace it fully. 
		browserInBytes -= 5;
	}
	else
	{
		// No Connection argument, ensure we use Connection: close. Note browserInBytes - 3 is at the X in: value\r\nX\r\n
		char conMethod[20] = "Connection: close\r\n";
		str_browserInput.insert(str_browserInput.rfind("\r\n"), conMethod, 19); // I only insert 19 as I want to avoid the \0.
		browserInBytes += 19;
	}
	
	if(!connectWebserver(str_browserInput))
	{
		char noConnect[] = "HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\n\r\n";
		send(browserSocket, noConnect, 47, 0);
		return;
	}
	
	// This section is used to swap the GET or POST to be a HEAD so we can get header length and content-length for later. 
	std::string str_headReq(str_browserInput.c_str(), browserInBytes);
	if(str_browserInput.find("GET ") != std::string::npos) // We found GET
	{
		std::cout << "GET: " << getStringAt("GET ", str_browserInput) << std::endl;
		
		str_headReq.erase(0, 3); // Remove GET
		str_headReq.assign("HEAD" + str_headReq); // Recreate with HEAD at start
		send(webServerSocket, str_headReq.c_str(), browserInBytes + 1, 0); // browserInBytes + 1 as we went from GET to HEAD
	}
	else // POST
	{
		std::cout << "POST: " << getStringAt("POST ", str_browserInput) << std::endl;
		
		str_headReq.erase(0, 4); // Remove POST
		str_headReq.assign("HEAD" + str_headReq); // Recreate with HEAD at start
		send(webServerSocket, str_headReq.c_str(), browserInBytes, 0);
	}
	
	// Process needed data from the header (bytes in header and body, range requests allowed.)
	char headResp[HEADER_RESPONSE_EST_SIZE] = {0};
	bool endOfHeader = false;
	int headBytes = 0;// headBytes corresponds to the exact number of bytes in the header portion for the GET as well
	int bodyBytes = 0;
	while(!endOfHeader) // This works fine cause its just a HEAD request, meaning we get nothing but the header. 
	{
		int rv = recv(webServerSocket, headResp + headBytes, HEADER_RESPONSE_EST_SIZE - headBytes, 0);
		headBytes += rv;
		if((headBytes == 2 && headResp[headBytes - 2] == '\r' && headResp[headBytes - 1] == '\n') || (headBytes > 4 && headResp[headBytes - 4] == '\r' && headResp[headBytes - 3] == '\n' && headResp[headBytes - 2] == '\r' && headResp[headBytes - 1] == '\n') || rv == 0)
			endOfHeader = true;
	}
	std::string str_headResp(headResp, headBytes);
	int httpCode = stringToInt(getStringAt("HTTP/1.1 ", str_headResp).substr(0, 3));
	
	connectWebserver(str_browserInput);
	send(webServerSocket, str_browserInput.c_str(), browserInBytes, 0);
	
	if(str_headResp.find("Content-Length: ") == std::string::npos) // Note this is from the HEAD request, which may not have gotten content length if the page is dynamically created.
	{
		char getHeadResp[HEADER_RESPONSE_EST_SIZE] = {0};
		headBytes = 0;
		unsigned int headEnd = 0;
		while(true)
		{
			int rv = recv(webServerSocket, getHeadResp + headBytes, HEADER_RESPONSE_EST_SIZE - headBytes, 0);
			if(rv == 0)
				break;
			
			std::string str_resp(getHeadResp + headBytes, rv);
			headBytes += rv;
			
			if(headBytes == 2 && str_resp.find("\r\n") != std::string::npos) // Empty Header
				break;
			
			else if((headEnd = str_resp.find("\r\n\r\n")) != std::string::npos) // End of Header
				break;
		}
		
		std::string str_getHeadResp(getHeadResp, headBytes);
		send(browserSocket, getHeadResp, headBytes, 0); // Send the Header bytes here, then later we read in the body and send it. (Done like this to avoid another GET request).
		if(str_getHeadResp.find("Content-Length: ") == std::string::npos) // Still no content-length, so we will assume there is no body for whatever reason. 
			return;
		
		// The recv above is guranteed to fetch all of the header, however it might also fetch some of the body, this portion of the body fetched is already sent with the header. 
		bodyBytes = stringToInt(getStringAt("Content-Length: ", str_getHeadResp)) - (headBytes - (headEnd + 4));
		headBytes = 0;
	}
	else // Happens most often
		bodyBytes = stringToInt(getStringAt("Content-Length: ", str_headResp));
		
	if(httpCode >= 300 && bodyBytes == 0) // Sometimes no body message is sent, so we return for a 300 or > code. If there is a body message we continue on. 
	{
		send(browserSocket, str_headResp.c_str(), headBytes, 0);
		return;
	}
		
	int size = headBytes + bodyBytes;
	int bytesGet = size;
	char* finalResponse = new char[size]; // Don't do this on the stack, cause we might get an overflow exception. Firefox couldnt update, so i did some calcs on the content-length. It was ~19MB and cygwin stack is 1.8MB
	while(bytesGet > 0) // Some sites may stop sending because its EOF, even though the full file size has not been sent, I dunno why, but when this happens recv returns 0. 
	{
		int rv = recv(webServerSocket, finalResponse + size - bytesGet, bytesGet, 0); // I know recv blocks, own thread, and lets it be compatible with windows + unix.
		bytesGet -= rv;
		if(rv == 0)
			break;
	}
	
	// I could send right after recieving in the while, but then I would need to block until the send is done each time. 
	send(browserSocket, finalResponse, size, 0); // Browser connection does not need to be checked for close.
	delete[] finalResponse;
}

// Big thanks to: https://www.geeksforgeeks.org/socket-programming-cc/ and http://www.bogotobogo.com/cplusplus/sockets_server_client.php
// This method connects to the host websever (ueses the HOST method). Note we connect on port 80 as we are doing http.
// I only need to worry about the return value on the first call cause all of the calls use the same information for this clh's life. So if it can't connect the first time it never will. 
bool ClientHandler::connectWebserver(const std::string &str_initBrowserReq)
{
	// I'm extracting the host (web server) data from the browsers first http request.
	std::string str_hostIP(getStringAt("Host: ", str_initBrowserReq));
	
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
			return false;
		}
	}
	
	// Connecting
	if(connect(webServerSocket, (struct sockaddr *)&webServerAddr, sizeof(webServerAddr)) < 0)
	{
		std::cout << "Connection to web server failed on host: " << getStringAt("Host: ", str_initBrowserReq) << std::endl;
		return false;
	}
	
	return true;
}

std::string ClientHandler::getStringAt(const std::string &str_key, const std::string &str_src)
{
	int valuePos = str_src.find(str_key) + str_key.size(); // No need to worry about the \0
	std::string str_value;
	for(; str_src.at(valuePos) != '\r' && str_src.at(valuePos) != '\n'; valuePos++)
		str_value += str_src.at(valuePos);
	
	return str_value;
}

int ClientHandler::stringToInt(const std::string &s)
{
	int value;
	std::stringstream ss;
	ss<<s;
	ss>>value;
	
	if(ss.rdstate() != 0 && ss.rdstate() != std::stringstream::eofbit)
	{
		std::cout << "One of the values entered as a string value that should be an int was not. Occured when parsing: " + s << std::endl;
		return -1;
	}
	
	return value;
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













