#include "SessionMusicBot.h"				//Pre-written sanity checks for versions
#include <filesystem>						//Standard C++ Filesystem
#include <dpp/dpp.h>						//D++ header
#include "fmod.hpp"							//FMOD Core
#include "fmod_studio.hpp"					//FMOD Studio
#include "SessionMusicBot_Utils.h"			//Some utility functions that 

/* Be sure to place your token in the line below.
 * Follow steps here to get a token:
 * https://dpp.dev/creating-a-bot-application.html
 * When you invite the bot, be sure to invite it with the 
 * scopes 'bot' and 'applications.commands', e.g.
 * https://discord.com/oauth2/authorize?client_id=940762342495518720&scope=bot+applications.commands&permissions=139586816064
 */

bool isRunning = true;

//file paths
std::filesystem::path exe_path = getExecutableFolder();                 //Special function from my header
std::filesystem::path banks_path = exe_path.append("soundbanks");
std::filesystem::path workpath = banks_path;

//---FMOD Declearations---//
FMOD::Studio::System* pSystem = nullptr;           //overall system

FMOD::Studio::Bank* pMasterBank = nullptr;          //Master Bank
FMOD::Studio::Bank* pMasterStringsBank = nullptr;   //Master Strings
FMOD::Studio::Bank* pSharedUIBank = nullptr;        //Example non-master bank
FMOD::Studio::Bus* pBus = nullptr;                  //A bus, aka ChannelGroup in FMOD Core terms


//FMOD::Studio::EventDescription* pEventDescription = nullptr;        //Event Description, essentially the Event itself plus data
//FMOD::Studio::EventInstance* pEventInstance = nullptr;              //Event Instance
FMOD_3D_ATTRIBUTES listenerAttributes;
FMOD_3D_ATTRIBUTES eventAttributes;


//Known bank names, basically just Master and Strings
std::string master_bank = "Master.bank";
std::string masterstrings_bank = "Master.strings.bank";
std::string ui_bank = "Shared_UI.bank";


std::string getBotToken()
{
	//read from .config (text) file and grab the token
	std::ifstream myfile ("token.config");
	std::string token;
	if (myfile.is_open())
	{
		myfile >> token;
		std::cout << "Token from config file is: " << token << "\n";
	}
	else
	{
		std::cout << "Token config file not opened properly. Ensure it's at the correct location and isn't corrupted!";
	}
	myfile.close();
	return token;
}

void ping(const dpp::slashcommand_t& event) {
	std::string response = "Pong! I'm alive!";
	event.reply(dpp::message(response.c_str()).set_flags(dpp::m_ephemeral));
	std::cout << response.c_str() << std::endl;
}

void list(const dpp::slashcommand_t& event) {
	std::string response = "List of events (coming soon).";
	event.reply(dpp::message(response.c_str()).set_flags(dpp::m_ephemeral));
	std::cout << response.c_str() << std::endl;
}

void join(const dpp::slashcommand_t& event) {
	dpp::guild* g = dpp::find_guild(event.command.guild_id);							//Get the Guild aka Server
	auto current_vc = event.from->get_voice(event.command.guild_id);					//Get the bot's current voice channel
	bool join_vc = true;
	if (current_vc) {																	//If already in voice...
		auto users_vc = g->voice_members.find(event.command.get_issuing_user().id);		//get channel of user
		if ((users_vc != g->voice_members.end()) && (current_vc->channel_id == users_vc->second.channel_id)) {
			join_vc = false;			//skips joining a voice chat below
		}
		else {
			event.from->disconnect_voice(event.command.guild_id);						//We're in a different VC, so leave it and join the new one below
			join_vc = true;																//possibly redundant assignment?
		}
	}
	if (join_vc) {																		//If we need to join a call above...
		if (!g->connect_member_voice(event.command.get_issuing_user().id)) {			//try to connect, return false if we fail
			event.reply(dpp::message("You're not in a voice channel to be joined!").set_flags(dpp::m_ephemeral));
			std::cout << "Not in a voice channel to be joined." << std::endl;
			return;
		}
		//If not caught above, we're in voice! Not instant, will need to wait for on_voice_ready callback
		event.reply(dpp::message("Joined your channel!").set_flags(dpp::m_ephemeral));
		std::cout << "Joined channel of user." << std::endl;
	}
	else {
		event.reply(dpp::message("I am already living in your walls.").set_flags(dpp::m_ephemeral));
		std::cout << "Already living in your walls." << std::endl;
	}
}

void leave(const dpp::slashcommand_t& event) {
	auto current_vc = event.from->get_voice(event.command.guild_id);
	if (current_vc) {
		event.from->disconnect_voice(event.command.guild_id);
		event.reply(dpp::message("Bye bye! I hope I played good sounds!").set_flags(dpp::m_ephemeral));
		std::cout << "Leaving voice channel." << std::endl;
	}
}

//void indexBanks()
//void indexEvents()????
//void listEvents()
//void play()
//void updateParam()
//void stop()

void init()
{
	workpath.append(master_bank);       //Sets workpath to default file, to ensure there's always at least _a_ path

	//FMOD Init
	//Todo: error checking
	std::cout << "Initializing FMOD...";
	FMOD::Studio::System::create(&pSystem);
	pSystem->initialize(128, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, nullptr);
	std::cout << "Good!" << std::endl;

	//Load Master Bank and Master Strings
	//Todo: error checking
	std::cout << "Loading banks...";
	pSystem->loadBankFile(workpath.replace_filename(master_bank).string().c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &pMasterBank);
	pSystem->loadBankFile(workpath.replace_filename(masterstrings_bank).string().c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &pMasterStringsBank);
	pSystem->loadBankFile(workpath.replace_filename(ui_bank).string().c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &pSharedUIBank);
	std::cout << "Good!" << std::endl;

	//Also get the Master Bus
	//pSystem->getBus("bus:/", &pBus);
	//pBus->setVolume(dBToFloat(-4.0f));



	//Setting Listener positioning, normally would be done from game engine data
	//Todo: Error checking
	std::cout << "Setting up Listener...";
	listenerAttributes.position = { 0.0f, 0.0f, 0.0f };
	listenerAttributes.forward = { 0.0f, 1.0f, 0.0f };
	listenerAttributes.up = { 0.0f, 0.0f, 1.0f };
	listenerAttributes.velocity = { 0.0f, 1.0f, 1.0f };         //Used exclusively for Doppler, but built-in Doppler kinda sucks
	pSystem->setListenerAttributes(0, &listenerAttributes);
	std::cout << "Good!" << std::endl;

	//Create event instance
	//std::cout << "Creating Event Instances...\n";
	//pSystem->getEvent("event:/Master/Music/TitleTheme", &pEventDescription);
	//pEventDescription->createInstance(&pEventInstance);

	//pEventInstance->start();
	//pEventInstance->release();

	std::cout << "All systems go!" << std::endl;;
}

int main() {

	init();

	/* Create bot cluster */
	dpp::cluster bot(getBotToken());

	/* Output simple log messages to stdout */
	bot.on_log(dpp::utility::cout_logger());

	/* Register slash command here in on_ready */
	bot.on_ready([&bot](const dpp::ready_t& event) {
		/* Wrap command registration in run_once to make sure it doesnt run on every full reconnection */
		if (dpp::run_once<struct register_bot_commands>()) {
			std::vector<dpp::slashcommand> commands {
				{ "ping", "Ping the bot to ensure it's alive.", bot.me.id },
				{ "list", "List all found banks/events.", bot.me.id},
				{ "join", "Join your current voice channel.", bot.me.id},
				{ "leave", "Leave the current voice channel.", bot.me.id}
			};
			bot.global_bulk_command_create(commands);
		}
	});

	/* Handle slash commands */
	bot.on_slashcommand([&bot](const dpp::slashcommand_t& event) {
		if (event.command.get_command_name() == "ping") { ping(event); }
		else if (event.command.get_command_name() == "list") { list(event); }
		else if (event.command.get_command_name() == "join") { join(event); }
		else if (event.command.get_command_name() == "leave") { leave(event); }
	});

	/* Start the bot */
	bot.start();

	//FMOD update loop here?
	while (isRunning)
	{
		pSystem->update();
		Sleep(10);
	}
	std::cout << "Quitting program. Releasing resources..." << std::endl;
	pSystem->unloadAll();
	pSystem->release();

	return 0;
}
