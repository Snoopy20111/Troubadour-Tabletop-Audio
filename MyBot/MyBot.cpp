#include "MyBot.h"
#include <dpp/dpp.h>

/* Be sure to place your token in the line below.
 * Follow steps here to get a token:
 * https://dpp.dev/creating-a-bot-application.html
 * When you invite the bot, be sure to invite it with the 
 * scopes 'bot' and 'applications.commands', e.g.
 * https://discord.com/oauth2/authorize?client_id=940762342495518720&scope=bot+applications.commands&permissions=139586816064
 */

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

//void indexBanks()

//void indexEvents()????

//void listEvents()

//void play()

//void updateParam()

//void stop()

//void join()

//void leave()

int main()
{
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
		if (event.command.get_command_name() == "ping") {
			std::string response = "Pong! I'm alive!";
			event.reply(dpp::message(response.c_str()).set_flags(dpp::m_ephemeral));
			std::cout << response.c_str() << std::endl;
		}
		else if (event.command.get_command_name() == "list") {
			std::string response = "List of events (coming soon).";
			event.reply(dpp::message(response.c_str()).set_flags(dpp::m_ephemeral));
			std::cout << response.c_str() << std::endl;
		}
		else if (event.command.get_command_name() == "join") {
			dpp::guild* g = dpp::find_guild(event.command.guild_id);				//Get the Guild aka Server
			auto current_vc = event.from->get_voice(event.command.guild_id);		//Get the bot's current voice channel
			bool join_vc = true;
			if (current_vc) {																	//If already in voice...
				auto users_vc = g->voice_members.find(event.command.get_issuing_user().id);		//get channel of user
				if ((users_vc != g->voice_members.end()) && (current_vc->channel_id == users_vc->second.channel_id)) {
					join_vc = false;			//skips joining a voice chat below
				}
				else {
					event.from->disconnect_voice(event.command.guild_id);				//We're in a different VC, so leave it and join the new one below
					join_vc = true;														//possibly redundant assignment?
				}
			}
			if (join_vc) {																//If we need to join a call above...
				if (!g->connect_member_voice(event.command.get_issuing_user().id)) {	//try to connect, return false if we fail
					event.reply(dpp::message("You're not in a voice channel to be joined!").set_flags(dpp::m_ephemeral));
					std::cout << "Not in a voice channel to be joined." << std::endl;
					//return 1;
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
		else if (event.command.get_command_name() == "leave") {
			dpp::guild* g = dpp::find_guild(event.command.guild_id);
			auto current_vc = event.from->get_voice(event.command.guild_id);
			if (current_vc) {
				event.from->disconnect_voice(event.command.guild_id);
				event.reply(dpp::message("Bye bye! I hope I played good sounds!").set_flags(dpp::m_ephemeral));
				std::cout << "Leaving voice channel." << std::endl;
			}
		}
	});

	/* Start the bot */
	bot.start(dpp::st_wait);

	//FMOD update loop here?

	return 0;
}
