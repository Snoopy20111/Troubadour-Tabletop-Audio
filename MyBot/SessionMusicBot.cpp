#include "SessionMusicBot.h"				//Pre-written sanity checks for versions
#include <dpp/dpp.h>						//D++ header
#include "fmod.hpp"							//FMOD Core
#include "fmod_studio.hpp"					//FMOD Studio
#include "fmod_common.h"
#include "fmod_studio_common.h"
#include "fmod_errors.h"					//Allows FMOD Results to be output as understandable text
#include <filesystem>						//Standard C++ Filesystem
#include <chrono>							//Standard C++ Timekeeping (mostly for debugging?)
#include <mutex>							//Standard C++ Mutex, to prevent race conditions between FMOD, Main, and Bot threads
#include "SessionMusicBot_Utils.h"			//Some utility functions specific to this bot

/* Be sure to place your token in the line below.
 * Follow steps here to get a token:
 * https://dpp.dev/creating-a-bot-application.html
 * When you invite the bot, be sure to invite it with the 
 * scopes 'bot' and 'applications.commands', e.g.
 * https://discord.com/oauth2/authorize?client_id=940762342495518720&scope=bot+applications.commands&permissions=139586816064
 */

//---File Paths---//
std::filesystem::path exe_path;
std::filesystem::path banks_path;
std::filesystem::path workpath;							//reusable path that gets filenames appended and then used

//---FMOD Declarations---//
FMOD::Studio::System* pSystem = nullptr;				//overall system
FMOD::System* pCoreSystem = nullptr;					//overall core system
FMOD::Studio::Bus* pMasterBus = nullptr;				//Master bus
FMOD::ChannelGroup* pMasterBusGroup = nullptr;			//Channel Group of the master bus
FMOD::DSP* mCaptureDSP = nullptr;						//DSP to attach to Master Channel Group for stealing output

//---Banks and stuff---//
FMOD::Studio::Bank* pMasterBank = nullptr;							//Master Bank, always loads first and contains shared content
std::string master_bank = "Master.bank";
FMOD::Studio::Bank* pMasterStringsBank = nullptr;					//Master Strings, allows us to refer to events by name instead of GUID
std::string masterstrings_bank = "Master.strings.bank";
std::vector<FMOD::Studio::Bank*> pBanks;							// List of all other banks
std::vector<std::filesystem::path> bankPaths;
std::vector<FMOD::Studio::EventDescription*> pEventDescriptions;	// List of all Events (plus associated data)
std::vector<std::string> eventPaths;
const std::string callableEventPath = "event:/Master/";				// The path where our callable events exist
std::vector<FMOD::Studio::EventInstance*> pEventInstances;			// List of all Event Instances

FMOD_3D_ATTRIBUTES listenerAttributes;
FMOD_3D_ATTRIBUTES eventAttributes;
FMOD::Studio::EventDescription* pEventDescription = nullptr;        //Event Description, essentially the Event itself plus data
FMOD::Studio::EventInstance* pEventInstance = nullptr;              //Event Instance






//---Misc Bot Declarations---//
dpp::discord_voice_client* currentClient = nullptr;		//current Voice Client of the bot. Only designed to run on one server.
std::vector<int16_t> myPCMData;							//Main buffer of PCM audio data, which FMOD adds to and D++ cuts "frames" from
bool isRunning = true;
bool isConnected = false;
bool isPlaying = false;

#ifndef NDEBUG
//---Extra Variables only present in Debug mode, for extra data---//
int samplesAddedCounter = 0;
#endif

//---FMOD and Audio Functions---//
FMOD_RESULT F_CALLBACK captureDSPReadCallback(FMOD_DSP_STATE* dsp_state, float* inbuffer, float* outbuffer, unsigned int length, int inchannels, int* outchannels) {

	FMOD::DSP* thisdsp = (FMOD::DSP*)dsp_state->instance;

	if (isConnected) {
		switch (inchannels) {
			case 1:																				//Mono Input
				if (*outchannels == 1) {
					for (unsigned int samp = 0; samp < length; samp++) {
						outbuffer[(samp * *outchannels)] = 0.0f;										//Brute force mutes system output
						myPCMData.push_back(floatToPCM(inbuffer[samp]));					//Adds sample to PCM buffer...
						myPCMData.push_back(floatToPCM(inbuffer[samp]));					//...twice, because D++ expects stereo input
#ifndef NDEBUG
						samplesAddedCounter += 2;
#endif
					}
				}
				else {
					std::cout << "Mono in, not mono out!" << std::endl;
				}
				break;

			case 2:																				//Stereo Input
				for (unsigned int samp = 0; samp < length; samp++) {
					for (int chan = 0; chan < *outchannels; chan++) {
						myPCMData.push_back(floatToPCM(inbuffer[(samp * inchannels) + chan]));			//Adds sample to PCM buffer in approprate channel
#ifndef NDEBUG
						samplesAddedCounter++;
#endif
					}
				}
				break;

			//Add cases here for other input channel configurations. Ultimately, all must mix down to stereo.

			default:
				std::cout << "DSP needs mono or stereo in and out!" << std::endl;
				return FMOD_ERR_DSP_DONTPROCESS;
				break;
		}
	}
	return FMOD_ERR_DSP_SILENCE;		//ensures System output is silent without manually telling every sample to be 0.0f
}

//---Bot Functions---//
// Simple ping, responds in chat and output log
void ping(const dpp::slashcommand_t& event) {
	event.reply(dpp::message("Pong! I'm alive!").set_flags(dpp::m_ephemeral));
	std::cout << "Responding to Ping command." << std::endl;
}

// Looks through the Soundbanks folder and makes an index of all existing & valid .bank files, events, and parameters
void list_banks(const dpp::slashcommand_t& event) {
	if (isPlaying) {			// If currently playing audio in a voice chat, exit early
		event.reply(dpp::message("Cannot index banks while the bot is active! Bad juju.").set_flags(dpp::m_ephemeral));
		return;
	}
	// Clear current Bank vector
	bankPaths.clear();

	//Show "Thinking..." while putting the list together
	event.thinking(true, [event](const dpp::confirmation_callback_t& callback) {

		std::cout << "Checking Banks path: " << banks_path << std::endl;
		std::string output = "";

		for (const auto& entry : std::filesystem::directory_iterator(banks_path)) {			// For every entry found in the banks folder
			if (entry.is_directory()) {														// Skip if directory...
				std::cout << entry.path() << " is a directory. Skipping..." << std::endl;
				continue;
			}
			else if (entry.path().extension() != ".bank") {									// Skip if not a bank...
				std::cout << "Skipped: " << entry.path() << "|| Extension is " << entry.path().extension()
					<< " which isn't an FMOD bank." << std::endl;
				continue;
			}

			std::cout << entry.path() << std::endl;											// Accepted!
			bankPaths.push_back(entry.path());
		}



		for (int i = 0; i < bankPaths.size(); i++) {										// For every accepted bank


			FMOD::Studio::Bank* newBank = nullptr;											// Load and add filename to the output string
			ERRCHECK(pSystem->loadBankFile(bankPaths[i].string().c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &newBank));
			pBanks.push_back(newBank);
			output.append("- " + bankPaths[i].filename().string() + "\n");
		}
		event.edit_original_response(dpp::message("## Found FMOD Banks: ##\n" + output));	// Send back output, list of all (now loaded) banks
	});
}

// Display list of known audio events
// in the future, these should only be ones the user can play, not _all_ events.
void list_events(const dpp::slashcommand_t& event) {

	if (!pMasterStringsBank->isValid() || pMasterStringsBank == nullptr) {
		std::cout << "Bad juju! Master Strings bank is invalid or nullptr." << std::endl;
		return;
	}

	eventPaths.clear();

	event.thinking(true, [event](const dpp::confirmation_callback_t& callback) {

		int count = 0;
		ERRCHECK(pMasterStringsBank->getStringCount(&count));
		if (count == 0) {
			return;
		}

		std::cout << "Count: " << count << std::endl;

		for (int i = 0; i < count; i++) {
			FMOD_GUID pathGUID;
			char pathStringChars[256];
			char* pathStringCharsptr = pathStringChars;
			int retreived;

			ERRCHECK(pMasterStringsBank->getStringInfo(i, &pathGUID, pathStringCharsptr, 256, &retreived));
			std::string pathString(pathStringCharsptr);

			//Discard all strings that aren't events in the Master folder
			//In the future we may want/need to use this for vectors of busses, VCAs, etc.
			if ((pathString.find("event:/Master/", 0) != 0)) {
				std::cout << "Skipped: " << pathString << std::endl;
				continue;
			}

			//What's left should be good for our eventPaths vector
			std::cout << "Added: " << pathString << std::endl;
			eventPaths.push_back(pathString);
		}
		//And now print 'em to Discord!
		std::string output = "";

		for (int i = 0; i < eventPaths.size(); i++) {
			output.append("- " + eventPaths[i] + "\n");
		}
		event.edit_original_response(dpp::message("## Found Events: ##\n" + output));
	});
}

void list_params(const dpp::slashcommand_t& event) {
	//Todo: For a given event (possibly another argument for function?)
}

void list_all(const dpp::slashcommand_t& event) {
	//Todo: index banks, list events, and tally up the parameters for each event
}

void play(const dpp::slashcommand_t& event) {
	//Todo: play from the indexed event list, and add to events playing list
}

void pause(const dpp::slashcommand_t& event) {
	//Todo: pause event (or all events) with matching name, if that can be done
	//Probably unnecessary, do later?
}

void stop(const dpp::slashcommand_t& event) {
	//Todo: Stop event with given name in events playing list
}

void stop_now(const dpp::slashcommand_t& event) {
	//Todo: same as above but with different stop type
}

void stop_all(const dpp::slashcommand_t& event) {
	//Todo: For Each in events playing list, stop_now
}

void join(const dpp::slashcommand_t& event) {
	dpp::guild* guild = dpp::find_guild(event.command.guild_id);						//Get the Guild aka Server
	dpp::voiceconn* currentVC = event.from->get_voice(event.command.guild_id);			//Get the bot's current voice channel
	bool join_vc = true;
	if (currentVC) {																	//If already in voice...
		auto users_vc = guild->voice_members.find(event.command.get_issuing_user().id);	//get channel of user
		if ((users_vc != guild->voice_members.end()) && (currentVC->channel_id == users_vc->second.channel_id)) {
			join_vc = false;			//skips joining a voice chat below
		}
		else {
			event.from->disconnect_voice(event.command.guild_id);						//We're in a different VC, so leave it and join the new one below
			join_vc = true;																//possibly redundant assignment?
		}
	}
	if (join_vc) {																		//If we need to join a call above...
		if (!guild->connect_member_voice(event.command.get_issuing_user().id)) {		//try to connect, return false if we fail
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
	dpp::voiceconn* currentVC = event.from->get_voice(event.command.guild_id);
	if (currentVC) {
		// Todo: Stop all events immediately
		// Todo: Wait until 
		event.from->disconnect_voice(event.command.guild_id);
		event.reply(dpp::message("Bye bye! I hope I played good sounds!").set_flags(dpp::m_ephemeral));
		std::cout << "Leaving voice channel." << std::endl;
		isConnected = false;
	}
}

void init() {
	std::cout << "###########################" << std::endl;
	std::cout << "###                     ###" << std::endl;
	std::cout << "###  Session Music Bot  ###" << std::endl;
	std::cout << "###                     ###" << std::endl;
	std::cout << "###########################" << std::endl;

	//file paths
	exe_path = getExecutableFolder();						//Special function from SessionMusicBot_Utils.h
	banks_path = exe_path.append("soundbanks");
	workpath = banks_path;
	workpath.append(master_bank);							//Sets workpath to default file, to ensure there's always at least a valid path

	//FMOD Init
	std::cout << "Initializing FMOD...";
	ERRCHECK(FMOD::Studio::System::create(&pSystem));
	ERRCHECK(pSystem->getCoreSystem(&pCoreSystem));
	//ERRCHECK(pCoreSystem->setDSPBufferSize(4096, 4));
	ERRCHECK(pSystem->initialize(128, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, nullptr));
	std::cout << "Done." << std::endl;

	//Load Master Bank and Master Strings
	std::cout << "Loading Master banks...";
	ERRCHECK(pSystem->loadBankFile(workpath.replace_filename(master_bank).string().c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &pMasterBank));
	ERRCHECK(pSystem->loadBankFile(workpath.replace_filename(masterstrings_bank).string().c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &pMasterStringsBank));
	std::cout << "Done." << std::endl;

	//Also get the Master Bus, set volume, and get the related Channel Group
	std::cout << "Getting Busses and Channel Groups...";
	ERRCHECK(pSystem->getBus("bus:/", &pMasterBus));
	ERRCHECK(pMasterBus->setVolume(dBToFloat(-10.0f)));
	ERRCHECK(pMasterBus->lockChannelGroup());					//Tell the Master Channel Group to always exist even when events arn't playing...
	ERRCHECK(pSystem->flushCommands());							//And wait until all previous commands are done (ensuring Channel Group exists)...
	ERRCHECK(pMasterBus->getChannelGroup(&pMasterBusGroup));	//Or else this fails immediately, and we'll have DSP problems.
	std::cout << "Done." << std::endl;
	

	//Define and create our capture DSP on the Master Channel Group.
	//Copied from FMOD's examples, unsure why this works and why it must be in brackets.
	{
		FMOD_DSP_DESCRIPTION dspdesc;
		memset(&dspdesc, 0, sizeof(dspdesc));
		strncpy_s(dspdesc.name, "LH_captureDSP", sizeof(dspdesc.name));
		dspdesc.version = 0x00010000;
		dspdesc.numinputbuffers = 1;
		dspdesc.numoutputbuffers = 1;
		dspdesc.read = captureDSPReadCallback;
		//dspdesc.userdata = (void*)0x12345678;
		ERRCHECK(pCoreSystem->createDSP(&dspdesc, &mCaptureDSP));
	}
	ERRCHECK(pMasterBusGroup->addDSP(FMOD_CHANNELCONTROL_DSP_TAIL, mCaptureDSP));		//Adds the newly defined dsp

	//Setting Listener positioning for 3D, in case it's used 
	std::cout << "Setting up Listener...";
	listenerAttributes.position = { 0.0f, 0.0f, 0.0f };
	listenerAttributes.forward = { 0.0f, 1.0f, 0.0f };
	listenerAttributes.up = { 0.0f, 0.0f, 1.0f };
	ERRCHECK(pSystem->setListenerAttributes(0, &listenerAttributes));
	std::cout << "Done." << std::endl;

	// Create event instance, for testing.
	// In the future we'll have a vector/list of Event Descriptions for every known event,
	// and probably a vector/list of Event Instances too.
	//std::cout << "Creating Test Event Instance...";
	//ERRCHECK(pSystem->getEvent("event:/Master/Test_Sine", &pEventDescription));
	//ERRCHECK(pSystem->getEvent("event:/Master/Music/TitleTheme", &pEventDescription));
	//ERRCHECK(pEventDescription->createInstance(&pEventInstance));
	//std::cout << "Done." << std::endl;

	//Debug details
	int samplerate; FMOD_SPEAKERMODE speakermode; int numrawspeakers;
	ERRCHECK(pCoreSystem->getSoftwareFormat(&samplerate, &speakermode, &numrawspeakers));
	ERRCHECK(pSystem->flushCommands());
	std::cout << "###########################" << std::endl;
	std::cout << "FMOD System Info:\n  Sample Rate- " << samplerate << "\n  Speaker Mode- " << speakermode
		<< "\n  Num Raw Speakers- " << numrawspeakers << std::endl;


	std::cout << "###########################" << std::endl;
	std::cout << "###                     ###" << std::endl;
	std::cout << "###   All systems go!   ###" << std::endl;
	std::cout << "###                     ###" << std::endl;
	std::cout << "###########################" << std::endl;
}

int main() {

	init();

#ifndef NDEBUG
	//Time stuff for debugging
	std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
	std::chrono::system_clock::time_point end = std::chrono::system_clock::now();
	std::chrono::system_clock::time_point last = end;
	std::chrono::duration<double, std::milli> elapsed;
	std::chrono::duration<double, std::milli> elapsedFrame;

	bool timerNotRunning = true;		//To make sure our times start at the first update loop where we know we've connected
	int allSamplesAdded = 0;
#endif

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
				{ "list_banks", "Search for, and list, all audio banks.", bot.me.id},
				{ "list_events", "List all found audio events.", bot.me.id},
				{ "list_params", "List all found audio events.", bot.me.id},
				{ "list_all", "List all found audio events.", bot.me.id},
				{ "join", "Join your current voice channel.", bot.me.id},
				{ "leave", "Leave the current voice channel.", bot.me.id}
			};
			bot.global_bulk_command_create(commands);
		}
	});

	/* Handle slash commands */
	bot.on_slashcommand([&bot](const dpp::slashcommand_t& event) {
		if (event.command.get_command_name() == "ping") { ping(event); }
		else if (event.command.get_command_name() == "list_banks") { list_banks(event); }
		else if (event.command.get_command_name() == "list_events") { list_events(event); }
		else if (event.command.get_command_name() == "list_params") { list_events(event); }
		else if (event.command.get_command_name() == "list_all") { list_all(event); }
		else if (event.command.get_command_name() == "join") { join(event); }
		else if (event.command.get_command_name() == "leave") { leave(event); }
	});

	bot.on_voice_ready([&bot](const dpp::voice_ready_t& event) {
		std::cout << "Voice Ready\n";
		currentClient = event.voice_client;							//Get the bot's current voice channel
		currentClient->set_send_audio_type(dpp::discord_voice_client::satype_live_audio);
		isConnected = true;											//Tell the rest of the program we've connected
		ERRCHECK(pEventInstance->start());							//Start test audio event...
		ERRCHECK(pEventInstance->release());						//...and release its resources when it stops playing.
	});

	/* Start the bot */
	bot.start();

	//Program loop
	while (isRunning) {

		//Update time
		last = end;
		end = std::chrono::system_clock::now();

		//Send PCM data to D++, if applicable
		if (isConnected) {

#ifndef NDEBUG
			//Start timer the first time we enter "isConnected"
			if (timerNotRunning) {
				start = std::chrono::system_clock::now();
				end = std::chrono::system_clock::now();
				timerNotRunning = false;
			}
			elapsed = end - start;
			elapsedFrame = end - last;
			std::cout << "Frame time: " << elapsedFrame << " || Samples added: " << samplesAddedCounter << " || Samples in buffer: " << myPCMData.size() << std::endl;
			samplesAddedCounter = 0;
#endif

			
			if (myPCMData.size() > dpp::send_audio_raw_max_length * 2) {								//If buffer is full enough (allows slight wiggle room)
#ifndef NDEBUG
				std::cout << "Sending PCM Data at time: " << elapsed << std::endl;
#endif

				while (myPCMData.size() > dpp::send_audio_raw_max_length * 2) {										//Until minimum size we want our buffer
					currentClient->send_audio_raw((uint16_t*)myPCMData.data(), dpp::send_audio_raw_max_length);		//Send the buffer (method takes 11520 BYTES, so 5760 samples)
					myPCMData.erase(myPCMData.begin(), myPCMData.begin() + (dpp::send_audio_raw_max_length * 0.5));	//Trim our main buffer of the data just sent
				}
			}
#ifndef NDEBUG
			else {																						//Else just report how much is left in the D++ buffer
				std::cout << "D++ Seconds remaining: " << currentClient->get_secs_remaining() << std::endl;
			}
#endif
			//Todo: what if the audio ends? We should stop trying to transmit normally, right? Probably would go here.
			// Possible approach: !eventsPlaying && output is silent, fromSilence = true.
		}
		//Update FMOD processes. Just before "Sleep" which gives FMOD time to process without main thread interference.
		pSystem->update();
		Sleep(20);
	}

	//Program quit. We never actually reach here as it stands, but we'll deal with that later.
	std::cout << "Quitting program. Releasing resources..." << std::endl;

	//Todo: If in voice, leave chat before dying

	//Remove DSP from master channel group, and release the DSP
	pMasterBusGroup->removeDSP(mCaptureDSP);
	mCaptureDSP->release();

	//Unload and release FMOD Studio System
	pSystem->unloadAll();
	pSystem->release();

	return 0;
}