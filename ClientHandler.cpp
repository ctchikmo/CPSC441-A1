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

#define HEADER_RESPONSE_EST_SIZE 600 // Im hoping I provide a large enough buffer. 
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
			server->makeAvailable(this);
			while (!flag_reqMade)
				pthread_cond_wait(&reqCond, &reqMutex);
		}
		pthread_mutex_unlock(&reqMutex);
		
		// At this point the Server can not interfere with this handler, the value is not in the queue, and is instead maintained by the pthread. So we can now handle the clients request. 
		// This is also why the mutex has been unlocked already. 
		handleRequest();
		
		flag_reqMade = false;
		close(webServerSocket);
		close(browserSocket);
		webServerSocket = -1;
		browserSocket = -1;
	}
}

// Small thanks to: https://www.geeksforgeeks.org/socket-programming-cc/ and http://www.bogotobogo.com/cplusplus/sockets_server_client.php
// Once this method is done it will return to where it was called in the wait method. 
// Note: I am forcing a Connection: close, thus we need to reconnect to the webserver before every send
// During handle request there is no interference!! This means I do not need to worry about mutexes
// SIGPIPE caused quite the headache, but from what I found on https://stackoverflow.com/questions/108183/how-to-prevent-sigpipes-or-handle-them-properly looks like I can ignore for multithreaded.
// Note that I'm not listing the linux man page resources as those are pretty self-explanatory. 
void ClientHandler::handleRequest() 
{
	bool slowDown = false;

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
	
	if(!connectWebserver(str_browserInput))
	{
		char noConnect[] = "HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\n\r\n";
		send(browserSocket, noConnect, 47, MSG_NOSIGNAL);
		return;
	}
	
	if(str_browserInput.find("Connection: ") != std::string::npos)
	{
		// Replace keep-alive. 
		str_browserInput.replace(str_browserInput.find("Connection: ") + 12, 10, "close"); // This is fine as keep-alive is longer than close, so it can replace it fully. 
	}
	else
	{
		// Add in close to header.
		char conMethod[20] = "Connection: close\r\n";
		str_browserInput.insert(str_browserInput.rfind("\r\n"), conMethod, 19); // I only insert 19 as I want to avoid the \0. Note that this is reverse find.
	}
	
	// This section is used to swap the GET or POST to be a HEAD so we can get header length and content-length for later. This also lets us get file type and if range is allowed. 
	std::string str_headReq(str_browserInput.c_str(), str_browserInput.size());
	if(str_browserInput.find("GET ") != std::string::npos) // We found GET
	{
		std::cout << "GET: " << getStringAt("GET ", str_browserInput) << std::endl;
		
		str_headReq.erase(0, 3); // Remove GET
		str_headReq.assign("HEAD" + str_headReq); // Recreate with HEAD at start
		send(webServerSocket, str_headReq.c_str(), str_headReq.size(), 0); 
	}
	else // POST
	{
		std::cout << "POST: " << getStringAt("POST ", str_browserInput) << std::endl;
		
		str_headReq.erase(0, 4); // Remove POST
		str_headReq.assign("HEAD" + str_headReq); // Recreate with HEAD at start
		send(webServerSocket, str_headReq.c_str(), str_headReq.size(), 0);
	}
	
	// Store needed data from the header (bytes in header and body, range requests allowed.)
	char headResp[HEADER_RESPONSE_EST_SIZE] = {0};
	bool endOfHeader = false;
	int headBytes = 0;// headBytes corresponds to the exact number of bytes in the header portion for the GET as well
	while(!endOfHeader) // This works fine cause its just a HEAD request, meaning we get nothing but the header. 
	{
		int rv = recv(webServerSocket, headResp + headBytes, HEADER_RESPONSE_EST_SIZE - headBytes, 0);
		headBytes += rv;
		if((headBytes == 2 && headResp[headBytes - 2] == '\r' && headResp[headBytes - 1] == '\n') || (headBytes > 4 && headResp[headBytes - 4] == '\r' && headResp[headBytes - 3] == '\n' && headResp[headBytes - 2] == '\r' && headResp[headBytes - 1] == '\n') || rv == 0)
			endOfHeader = true;
	}
	std::string str_headResp(headResp, headBytes);
	
	// If we have the desired file type check to see if we can do range requets in bytes
	if((str_headResp.find("Content-Type: ") != std::string::npos) && (getStringAt("Content-Type: ", str_headResp).find("html") != std::string::npos) 
		&& (str_headResp.find("Accept-Ranges: ") != std::string::npos) && (getStringAt("Accept-Ranges: ", str_headResp).find("bytes") != std::string::npos))
	{
		std::string str_range = "Range: bytes=0-" + std::to_string(server->sloxyRangeRate - 1) + "\r\n";
		str_browserInput.insert(str_browserInput.rfind("\r\n"), str_range.c_str(), str_range.size());
		slowDown = true;
	}

	connectWebserver(str_browserInput);
	send(webServerSocket, str_browserInput.c_str(), str_browserInput.size(), 0);
	
	// Read in the GET response. 
	char getResp[HEADER_RESPONSE_EST_SIZE] = {0};
	int getHeaderbytes = 0;
	unsigned int headEnd = 0;
	while(true) // I do not need to make slowDown requests in here. The request for this response is to make sure dynamic documents are loaded so that content-length can be grabbed. 
	{
		int rv = recv(webServerSocket, getResp + getHeaderbytes, HEADER_RESPONSE_EST_SIZE - getHeaderbytes, 0);
		if(rv == 0)
			break;
		
		std::string str_resp(getResp + getHeaderbytes, rv); // Do not use -1 after getHeaderbytes because we do the += after this, thus it is already on the proper position (just think).
		getHeaderbytes += rv;
		headEnd = str_resp.find("\r\n\r\n");
		
		if(getHeaderbytes == 2 && str_resp.find("\r\n") != std::string::npos) // Empty Header
		{
			headEnd = 2;
			break;
		}
		
		else if(headEnd != std::string::npos) // End of Header
			break;
	}
	std::string str_getResp(getResp, getHeaderbytes);
	
	// This is handling excess body bytes that have not yet been picked up by the recieve, and in the case of slowDown (which makes more response headers) these bytes will be missed at the final loop. 
	// The calculation is: if body bytes recieved (header - endEnd) is not equal to the range amount obtained then pick them up here and add em in. 
	unsigned int getHeaderBodyExtraBytes = 0;
	if(slowDown)
	{
		getHeaderBodyExtraBytes = stringToInt(getStringAt("Content-Length: ", str_getResp)); // Must do this and not just use the server->sloxyRangeRate value, in case there are less than server->sloxyRangeRate sent. 
		if((getHeaderbytes - (headEnd + 4)) != getHeaderBodyExtraBytes)
		{
			int getHeaderBodyBytesLeft = getHeaderBodyExtraBytes - (getHeaderbytes - (headEnd + 4));
			int totalBytes = getHeaderBodyBytesLeft;
			char buff[getHeaderBodyBytesLeft] = {0};
			while(getHeaderBodyBytesLeft > 0) // Some sites may stop sending because its EOF, even though the full file size has not been sent, I dunno why, but when this happens recv returns 0. 
			{	
				int bytesGrabbed = recv(webServerSocket, buff + totalBytes - getHeaderBodyBytesLeft, getHeaderBodyBytesLeft, 0); // I know recv blocks, own thread, and lets it be compatible with windows + unix.
				getHeaderBodyBytesLeft -= bytesGrabbed;
				
				if(bytesGrabbed == 0)
					break;
			}
			str_getResp.append(buff, totalBytes);
		}
	}
	
	if(str_getResp.find("Content-Length: ") == std::string::npos) // No content-length, so we will assume there is no body for whatever reason. 
	{
		send(browserSocket, str_getResp.c_str(), str_getResp.size(), MSG_NOSIGNAL); // Don't care if this errors or not, future sends are not affected. 
		return;
	}
	
	// The recv above is guranteed to fetch all of the header, however it might also fetch some of the body, this portion of the body fetched is already sent with the header. 
	int bodyBytes = 0;
	if(slowDown) // Range uses the end of Content-Range for the full amount. 
	{
		bodyBytes = stringToInt(getStringAt("Content-Range: bytes 0-" + std::to_string(std::stoi(getStringAt("Content-Length: ", str_getResp)) - 1) + "/", str_getResp)); // The first getStringAt is always that. 
		
		// I need to change the content-length to be the full amount before sending back to the browser so it accepts the full file. 
		str_getResp.erase(str_getResp.find("Content-Length: ") + 16, getStringAt("Content-Length: ", str_getResp).length());
		str_getResp.insert(str_getResp.find("Content-Length: ") + 16, std::to_string(bodyBytes));
		
		// Remove both the bodybytes obtained in the header recv and the following straggler pickup
		bodyBytes -= getHeaderBodyExtraBytes;
		 
		// remove partial content response values.
		str_getResp.erase(9, 19); // Starts with http/1.1 206 Partial Content
		str_getResp.insert(9, "200 OK");
		
		int contentRangeStart = str_getResp.find("Content-Range: ");
		int end = contentRangeStart;
		for(; str_getResp[end] != '\r'; end++);
		str_getResp.erase(contentRangeStart, end + 2 - contentRangeStart); // Remove partial content header
	}
	else
		bodyBytes = stringToInt(getStringAt("Content-Length: ", str_getResp)) - (getHeaderbytes - (headEnd + 4));
		
	int sRv = send(browserSocket, str_getResp.c_str(), str_getResp.size(), MSG_NOSIGNAL); // Send the Header bytes here, then later we read in the body and send it. (Done like this to avoid another GET request).
	if(sRv == -1) // Error, most likely socket closed cause user stopped the page. 
		return;
		
	if(bodyBytes <= 0) // Sometimes no body message is sent, so we return.
		return;
	
	int rangeCount = 2;
	int bytesRemaining = bodyBytes;
	char* finalResponse = new char[bodyBytes]; // Don't do this on the stack, cause we might get an overflow exception. Firefox couldnt update, so i did some calcs on the content-length. It was ~19MB and cygwin stack is 1.8MB
	while(bytesRemaining > 0) // Some sites may stop sending because its EOF, even though the full file size has not been sent, I dunno why, but when this happens recv returns 0. 
	{
		int bytesGrabbed;
		if(slowDown)
		{
			unsigned int endRangeValue = (server->sloxyRangeRate * rangeCount) - 1;
			if(endRangeValue > bodyBytes + getHeaderBodyExtraBytes) // Handle out of range case.
				endRangeValue -= server->sloxyRangeRate - bytesRemaining;
			
			str_browserInput.erase(str_browserInput.rfind("=") + 1, std::string::npos);
			str_browserInput += std::to_string(server->sloxyRangeRate * (rangeCount - 1)) + "-" + std::to_string(endRangeValue) + "\r\n\r\n"; // This is always the last line.
			rangeCount++;
			
			connectWebserver(str_browserInput);
			send(webServerSocket, str_browserInput.c_str(), str_browserInput.size(), 0);	
			
			char resp[server->sloxyRangeRate + HEADER_RESPONSE_EST_SIZE] = {0};
			bytesGrabbed = recv(webServerSocket, resp, server->sloxyRangeRate + HEADER_RESPONSE_EST_SIZE, 0); 
		
			int bodyStart = 4;
			for(; bytesGrabbed >= 4 && bodyStart < HEADER_RESPONSE_EST_SIZE; bodyStart++)
				if((resp[bodyStart - 4] == '\r' && resp[bodyStart - 3] == '\n' && resp[bodyStart - 2] == '\r' && resp[bodyStart - 1] == '\n')) // End of header
					break;
					
			int sendRv = send(browserSocket, resp + bodyStart, bytesGrabbed - bodyStart, MSG_NOSIGNAL);
			if(sendRv == -1) // Error, most likely socket closed cause user stopped the page. 
				break;
			bytesRemaining -= (bytesGrabbed - bodyStart);
		}
		else
		{
			bytesGrabbed = recv(webServerSocket, finalResponse + bodyBytes - bytesRemaining, bytesRemaining, 0); // I know recv blocks, own thread, and lets it be compatible with windows + unix.
			int sendRv = send(browserSocket, finalResponse + bodyBytes - bytesRemaining, bytesGrabbed, MSG_NOSIGNAL);
			if(sendRv == -1) // Error, most likely socket closed cause user stopped the page. 
				break;
			bytesRemaining -= bytesGrabbed;
		}
		
		if(bytesGrabbed == 0)
			break;
	}
	
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
	if(webServerSocket != -1)
		close(webServerSocket);
	
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













