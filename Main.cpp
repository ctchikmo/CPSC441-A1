#include <iostream>
#include <string>
#include <sstream>

#include "Server.h"

// There are 2 arguments, the port number and the number of client handlers
int main(int argc, char const* argv[])
{
	if(argc != 4)
	{
		std::cout << "Please use the start sequence: Sloxy.exe <port> <client threads> <Range Rate>." << std::endl;
		exit(1);
	}
	
	std::string str_port(argv[1]);
	std::stringstream ss; // The string stream can be used to convert a string to an int
	
	int port;
	ss<<str_port;
	ss>>port;
	
	if(ss.rdstate() != 0 && ss.rdstate() != std::stringstream::eofbit)
	{
		std::cout << "Ensure the port value entered is an integer." << std::endl;
		exit(1);
	}
	// At this point port has the user entered integer value.
	
	if(port < 0 || port == 80 || port > 65535)
	{
		std::cout << "Port value invalid, must be in the range 0-65535, excluding port 80." << std::endl;
		exit(1);
	}
	
	std::string str_clientsMax(argv[2]);
	std::stringstream ss2; // The string stream can be used to convert a string to an int
	
	int maxClients;
	ss2<<str_clientsMax;
	ss2>>maxClients;
	
	if(ss2.rdstate() != 0 && ss2.rdstate() != std::stringstream::eofbit)
	{
		std::cout << "Ensure the client threads count entered is an integer." << std::endl;
		exit(1);
	}
	// At this point maxClients has the user entered integer value.
	
	if(maxClients < 1 || maxClients > 100)
	{
		std::cout << "client threads count invalid, must be in the range 1-100" << std::endl;
		exit(1);
	}
	
	std::string str_rangeRate(argv[3]);
	std::stringstream ss3; // The string stream can be used to convert a string to an int
	
	int rangeRate;
	ss3<<str_rangeRate;
	ss3>>rangeRate;
	
	if(ss3.rdstate() != 0 && ss3.rdstate() != std::stringstream::eofbit)
	{
		std::cout << "Ensure the range rate entered is an integer." << std::endl;
		exit(1);
	}
	// At this point rangeRate has the user entered integer value.
	
	if(rangeRate < 5)
	{
		std::cout << "Please enter a larger range rate" << std::endl;
		exit(1);
	}
	
	std::cout << "A Sloxy server able to handle " << maxClients << " clients is being started on port " << port << " downloading .html files with range requests of the size " << rangeRate << ". Please wait a moment." << std::endl;
	
	// User input and error checking is done, the rest is now client + server. 
	Server server(port, maxClients, rangeRate);
	
	return 0;
}
