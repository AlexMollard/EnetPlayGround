#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#include <algorithm>
#include <chrono>
#include <random>
#include <unordered_map>
#include <vector>

#include "IconsLucide.h"
#include "PluginInterface.h"

class CatPlugin : public IPlugin
{
public:
	CatPlugin()
	      : lastBroadcastTime(0),
	        broadcastIntervalMs(300000), // Random cat behavior every 5 minutes
	        lastPurrTime(0),
	        purrIntervalMs(120000), // Purring every 2 minutes
	        lastNapTime(0),
	        napDurationMs(0),
	        isNapping(false)
	{
		// Initialize keyword responses
		catKeywords = {
			{   "meow",              { "*purrs contentedly*", "*meows back at you*", "Meeeooowww!", "Meow?" } },
			{    "pet", { "*arches back and purrs*", "*rubs against your leg*", "*accepts pets gracefully*" } },
			{   "fish", { "*ears perk up*", "*stares intensely*", "*paws excitedly at the mention of fish*" } },
			{   "food",          { "*runs to food bowl*", "*meows hungrily*", "*stares at you expectantly*" } },
			{  "treat",                 { "*zooms across room*", "*spins in circles*", "*chirps excitedly*" } },
			{  "mouse",                { "*tail twitches*", "*gets into hunting position*", "*eyes dilate*" } },
			{ "string",    { "*pounces playfully*", "*bats at imaginary string*", "*zooms around the room*" } },
			{ "pspsps",       { "*appears suddenly*", "*zooms to your location*", "*rubs against your leg*" } }
		};

		// Initialize cat sounds
		catSounds = { "*quiet meow*", "*loud MEOW*", "*purrrrrrrr*", "*chirps*", "*trills*", "*hisses at nothing*", "*yawns loudly*", "*chatters at birds*", "*knocks something over in the distance*" };

		// Initialize ASCII art
		catAsciiArts = {
			R"(
 /\_/\
( o.o )
 > ^ <
)",
			R"(
  /\_/\  
 ( o.o ) 
  > ^ <  
)",
			R"(
   /\     /\
  {  `---'  }
  {  O   O  }
  ~~>  V  <~~
   \  \|/  /
    `-----'
)",
			R"(
  /\_/\
 ( o o )
 =( I )=
)",
			R"(
 /\_/\
( *.* )
 >\Y/<
)"
		};

		// Initialize random engine
		randomEngine.seed(std::chrono::system_clock::now().time_since_epoch().count());
	}

	// Updated onLoad to accept function pointers
	bool onLoad(ServerFunctions* functions) override
	{
		if (functions != nullptr)
		{
			this->serverFuncs = *functions;
		}
		// Use a direct check to avoid crashes
		serverFuncs.getLogger()->info("CatPlugin: The cat has arrived!");
		serverFuncs.broadcastSystemMessage("A mysterious cat has appeared on the server! " + std::string(ICON_LC_CAT));
		loaded = true;
		return true;
	}

	bool getLoaded() const override
	{
		return loaded;
	}

	void onUnload() override
	{
		serverFuncs.getLogger()->info("CatPlugin: The cat has left the building!");
		serverFuncs.broadcastSystemMessage("The server cat has disappeared to take a nap somewhere else...");
	}

	// Information methods
	std::string getName() const override
	{
		return "CatPlugin";
	}

	std::string getVersion() const override
	{
		return "2.0";
	}

	std::string getAuthor() const override
	{
		return "AlexM";
	}

	// Event hooks
	void onPlayerConnect(Player& player) override
	{
		// This is typically a guest account as the player hasn't logged in yet
	}

	void onPlayerLogin(Player& player) override
	{
		// Greet new players with a cat welcome
		std::string welcomeMessage = getRandomCatGreeting(player.name);
		serverFuncs.sendSystemMessage(player, welcomeMessage);

		// Track player for interaction history
		if (playerInteractions.find(player.name) == playerInteractions.end())
		{
			playerInteractions[player.name] = 0;
		}
	}

	void onChatMessage(const std::string& sender, const std::string& message) override
	{
		// Check if the server cat is napping
		if (isNapping)
		{
			uint32_t currentTime = getCurrentTimeMs();
			if (currentTime - lastNapTime <= napDurationMs)
			{
				// There's a small chance the cat will wake up if addressed directly
				if (message.find("cat") != std::string::npos || message.find("kitty") != std::string::npos)
				{
					if (getRandomNumber(0, 10) < 2)
					{ // 20% chance to wake up
						isNapping = false;
						serverFuncs.broadcastSystemMessage("*The server cat wakes up from its nap, stretches, and yawns*");
					}
				}
				return; // Cat is still napping, ignore message
			}
			else
			{
				// Nap time is over
				isNapping = false;
				serverFuncs.broadcastSystemMessage("*The server cat wakes up from its nap, stretches, and yawns*");
			}
		}

		// Process message for keywords
		std::string lowercaseMessage = message;
		std::transform(lowercaseMessage.begin(), lowercaseMessage.end(), lowercaseMessage.begin(), ::tolower);

		// Check for cat keywords
		for (const auto& keyword: catKeywords)
		{
			if (lowercaseMessage.find(keyword.first) != std::string::npos)
			{
				// Respond to keyword
				std::string response = getRandomElement(keyword.second);

				// Add ASCII art occasionally
				if (getRandomNumber(0, 10) < 3)
				{ // 30% chance
					response += "\n" + getRandomElement(catAsciiArts);
				}

				serverFuncs.broadcastSystemMessage(response);

				// Update player interaction counter
				if (playerInteractions.find(sender) != playerInteractions.end())
				{
					playerInteractions[sender]++;
				}
				else
				{
					playerInteractions[sender] = 1;
				}

				// Only respond to one keyword per message to avoid spam
				return;
			}
		}

		// Count mentions of the cat
		if (lowercaseMessage.find("cat") != std::string::npos || lowercaseMessage.find("kitty") != std::string::npos || lowercaseMessage.find("feline") != std::string::npos)
		{
			if (getRandomNumber(0, 10) < 7)
			{ // 70% chance to respond
				std::string response = getRandomElement(catSounds);
				serverFuncs.broadcastSystemMessage(response);
			}
		}
	}

    bool onPlayerCommand(Player& player, const std::string& command, const std::vector<std::string>& args) override
	{
		// Cat-related commands
		if (command == "cat")
		{
			// Check if there are any arguments (args[0] is "cat" itself, so we need at least 2 elements)
			if (args.size() <= 1)
			{
				serverFuncs.sendSystemMessage(player, "Available cat commands: pet, feed, play, status, meow, art");
				return true;
			}

			// Subcommand is at index 1 since args[0] contains the command "cat"
			const std::string& subCommand = args[1];

			if (subCommand == "pet")
			{
				handlePetCommand(player);
				return true;
			}
			else if (subCommand == "feed")
			{
				handleFeedCommand(player, args);
				return true;
			}
			else if (subCommand == "play")
			{
				handlePlayCommand(player);
				return true;
			}
			else if (subCommand == "status")
			{
				displayCatStatus(player);
				return true;
			}
			else if (subCommand == "meow")
			{
				serverFuncs.broadcastSystemMessage("*The server cat looks at " + player.name + " and " + getRandomElement(catSounds) + "*");
				return true;
			}
			else if (subCommand == "art")
			{
				serverFuncs.sendSystemMessage(player, getRandomElement(catAsciiArts));
				return true;
			}
		}
		else if (command == "help")
		{
			serverFuncs.sendSystemMessage(player, "/cat - Interact with the server cat\n/cat pet - Pet the cat\n/cat feed [food] - Feed the cat\n/cat play - Play with the cat\n/cat status - Check the cat's status\n/cat meow - Make the cat meow\n/cat art - Display cat ASCII art");
			return true;
		}
		else if (command == "pspsps")
		{
			// Summon the cat
			serverFuncs.broadcastSystemMessage("*The server cat zooms to " + player.name + "'s location*\n" + std::string(ICON_LC_CAT));
			return true;
		}

		return false;
	}

	// onServerTick for random cat behaviors
	void onServerTick() override
	{
		uint32_t currentTime = getCurrentTimeMs();

		// Random cat behavior
		if (currentTime - lastBroadcastTime > broadcastIntervalMs)
		{
			// Only perform random behavior if not napping
			if (!isNapping)
			{
				performRandomCatBehavior();
			}
			lastBroadcastTime = currentTime;

			// Randomize next behavior interval (3-7 minutes)
			broadcastIntervalMs = getRandomNumber(180000, 420000);
		}

		// Cat purring
		if (currentTime - lastPurrTime > purrIntervalMs)
		{
			// Only purr if not napping
			if (!isNapping)
			{
				if (getRandomNumber(0, 10) < 3)
				{ // 30% chance to purr
					serverFuncs.broadcastSystemMessage("*The server cat purrs contentedly*");
				}
			}
			lastPurrTime = currentTime;
		}

		// Check if nap is over
		if (isNapping && currentTime - lastNapTime > napDurationMs)
		{
			isNapping = false;
			serverFuncs.broadcastSystemMessage("*The server cat wakes up from its nap, stretches, and yawns*");
		}
	}

private:
	// Helper methods for cat behavior
	void performRandomCatBehavior()
	{
		int behavior = getRandomNumber(0, 10);

		switch (behavior)
		{
			case 0:
				// Zoomies
				serverFuncs.broadcastSystemMessage("*The server cat suddenly gets the zoomies and runs around wildly!*");
				break;
			case 1:
				// Knock something over
				serverFuncs.broadcastSystemMessage("*You hear something fall over in the distance... the server cat looks innocent*");
				break;
			case 2:
				// Loaf mode
				serverFuncs.broadcastSystemMessage("*The server cat has entered loaf mode*\n" + getCatLoafArt());
				break;
			case 3:
				// Time for nap
				startNap();
				break;
			case 4:
				// Hunt mode
				serverFuncs.broadcastSystemMessage("*The server cat's eyes dilate as it spots an invisible prey*");
				break;
			case 5:
				// Meow for attention
				serverFuncs.broadcastSystemMessage(getRandomElement(catSounds));
				break;
			case 6:
				// Groom
				serverFuncs.broadcastSystemMessage("*The server cat begins meticulously grooming itself*");
				break;
			case 7:
				// Ask for food
				serverFuncs.broadcastSystemMessage("*The server cat stares at its food bowl then stares at you accusingly*");
				break;
			case 8:
				// Show random cat fact
				serverFuncs.broadcastSystemMessage("CAT FACT: " + getRandomCatFact());
				break;
			case 9:
				// Find sun spot
				serverFuncs.broadcastSystemMessage("*The server cat has found a patch of sunlight and is lounging contentedly*");
				break;
			case 10:
				// Cat art
				serverFuncs.broadcastSystemMessage(getRandomElement(catAsciiArts));
				break;
		}
	}

	void startNap()
	{
		isNapping = true;
		lastNapTime = getCurrentTimeMs();
		// Nap for 3-10 minutes
		napDurationMs = getRandomNumber(180000, 600000);
		serverFuncs.broadcastSystemMessage("*The server cat has found a comfortable spot and fallen asleep. Shhh...*");
		serverFuncs.getLogger()->info("CatPlugin: Cat is napping for " + std::to_string(napDurationMs / 60000) + " minutes");
	}

	void handlePetCommand(Player& player)
	{
		// Increase relationship with player
		if (playerInteractions.find(player.name) != playerInteractions.end())
		{
			playerInteractions[player.name] += 2;
		}
		else
		{
			playerInteractions[player.name] = 2;
		}

		// Determine response based on relationship
		int relationship = playerInteractions[player.name];

		if (relationship > 20)
		{
			serverFuncs.broadcastSystemMessage("*The server cat recognizes " + player.name + " and purrs loudly, rubbing against their legs*");
		}
		else if (relationship > 10)
		{
			serverFuncs.broadcastSystemMessage("*The server cat enjoys being petted by " + player.name + " and purrs contentedly*");
		}
		else
		{
			serverFuncs.broadcastSystemMessage("*The server cat cautiously accepts pets from " + player.name + "*");
		}
	}

	void handleFeedCommand(Player& player, const std::vector<std::string>& args)
	{
		std::string food = "treats";
		if (args.size() > 1)
		{
			food = args[1];
		}

		std::string response;

		// Different responses based on food type
		if (food == "fish" || food == "salmon" || food == "tuna")
		{
			response = "*The server cat's eyes light up and it devours the " + food + " enthusiastically!*";
			playerInteractions[player.name] += 3;
		}
		else if (food == "treats" || food == "chicken")
		{
			response = "*The server cat happily munches on the " + food + " offered by " + player.name + "*";
			playerInteractions[player.name] += 2;
		}
		else if (food == "vegetables" || food == "broccoli")
		{
			response = "*The server cat sniffs the " + food + " and gives " + player.name + " a judgmental look*";
		}
		else
		{
			response = "*The server cat cautiously nibbles the " + food + "*";
			playerInteractions[player.name] += 1;
		}

		serverFuncs.broadcastSystemMessage(response);
	}

	void handlePlayCommand(Player& player)
	{
		std::vector<std::string> playResponses = {
			"*The server cat chases an invisible mouse around " + player.name + "*", "*The server cat pounces on a phantom toy, then looks at " + player.name + " proudly*", "*The server cat engages in play mode, zooming around " + player.name + "*", "*The server cat playfully bats at " + player.name + "'s shoelaces*"
		};

		serverFuncs.broadcastSystemMessage(getRandomElement(playResponses));
		playerInteractions[player.name] += 2;
	}

	void displayCatStatus(Player& player)
	{
		int relationship = 0;
		if (playerInteractions.find(player.name) != playerInteractions.end())
		{
			relationship = playerInteractions[player.name];
		}

		std::string status = "The server cat is ";
		if (isNapping)
		{
			status += "currently napping peacefully.";
		}
		else
		{
			std::vector<std::string> moods = { "feeling playful and energetic.", "in a calm and observant mood.", "looking for mischief.", "grooming itself meticulously.", "staring into the void contemplatively." };
			status += getRandomElement(moods);
		}

		std::string relationshipStatus;
		if (relationship > 20)
		{
			relationshipStatus = "The cat absolutely adores you!";
		}
		else if (relationship > 10)
		{
			relationshipStatus = "The cat recognizes and likes you.";
		}
		else if (relationship > 5)
		{
			relationshipStatus = "The cat is becoming familiar with you.";
		}
		else
		{
			relationshipStatus = "The cat is still getting to know you.";
		}

		serverFuncs.sendSystemMessage(player, status + "\n" + relationshipStatus);
	}

	// Utility methods
	std::string getRandomCatGreeting(const std::string& playerName)
	{
		std::vector<std::string> greetings = { "*The server cat notices " + playerName + " and meows a greeting*", "*The server cat looks up briefly as " + playerName + " connects*", "*The server cat purrs softly to welcome " + playerName + "*", "*The server cat stretches and acknowledges " + playerName + "'s arrival*" };

		return getRandomElement(greetings);
	}

	std::string getCatLoafArt()
	{
		return R"(
  /\__/\
 /      \
|  o  o  |
 \______)";
	}

	std::string getRandomCatFact()
	{
		std::vector<std::string> catFacts = { "Cats have five toes on their front paws but only four on their back paws.",
			"A group of cats is called a 'clowder'.",
			"Cats spend about 70% of their lives sleeping.",
			"Cats can make over 100 different vocal sounds, while dogs can only make about 10.",
			"A cat's purr vibrates at a frequency that promotes tissue regeneration.",
			"Cats can jump up to six times their length.",
			"A cat's sense of smell is 14 times stronger than a human's.",
			"Cats have a third eyelid called a 'haw' that helps protect their eyes.",
			"A cat's heart beats twice as fast as a human heart.",
			"Cats have 32 muscles in each ear, allowing them to move their ears independently.",
			"Most cats don't have eyelashes.",
			"Cats cannot taste sweet things because of a genetic mutation.",
			"A cat's jaw can't move sideways." };

		return getRandomElement(catFacts);
	}

	template<typename T>
	T getRandomElement(const std::vector<T>& elements)
	{
		if (elements.empty())
		{
			return T();
		}
		std::uniform_int_distribution<int> distribution(0, elements.size() - 1);
		return elements[distribution(randomEngine)];
	}

	int getRandomNumber(int min, int max)
	{
		std::uniform_int_distribution<int> distribution(min, max);
		return distribution(randomEngine);
	}

	uint32_t getCurrentTimeMs()
	{
		return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	// Member variables
	uint32_t lastBroadcastTime;
	uint32_t broadcastIntervalMs;
	uint32_t lastPurrTime;
	uint32_t purrIntervalMs;
	uint32_t lastNapTime;
	uint32_t napDurationMs;
	bool isNapping;
	bool loaded = false;

	std::unordered_map<std::string, std::vector<std::string>> catKeywords;
	std::vector<std::string> catSounds;
	std::vector<std::string> catAsciiArts;
	std::unordered_map<std::string, int> playerInteractions;
	std::mt19937 randomEngine;
};

// Export the required functions
extern "C"
{
	PLUGIN_API IPlugin* CreatePlugin()
	{
		return new CatPlugin();
	}

	PLUGIN_API void DestroyPlugin(IPlugin* plugin)
	{
		delete plugin;
	}
}
