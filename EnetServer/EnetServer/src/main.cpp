#include <cstdint>
#include "Server.h"

// Main function
int main(int argc, char* argv[])
{
	// Create the server
	GameServer server;

	// Run the server (this blocks until shutdown)
	server.run();

	return 0;
}
