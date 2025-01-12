#include "main.h"			//Pre-written sanity checks for versions
#include "utils.h"			//Utility functions and all other necessary includes

using namespace trbdrUtils;


//---Constants you might change---//
static const std::string masterBankFile = "Master.bank";			// The name of the Master Bank file
static const std::string masterStringsFile = "Master.strings.bank";	// The name of the Master Strings Bank file
static const std::string soundbanksFolder = "soundbanks";			// The folder where the program's built FMOD Studio banks are located
static const std::string soundfilesFolder = "soundfiles";			// The folder where loose sound files can be found and played.
static const std::string callableEventPrefix = "event:/Master/";	// The FMOD-internal path where our callable events exist
static const float fmodMasterBusVolOffset = -10.0f;					// How much to pull down the Master Bus fader when running
static const size_t sendAudioThresh = dpp::send_audio_raw_max_length / 2;		//How many PCM samples we need before sending them to DPP
static const dpp::embed basicEmbed = dpp::embed()					// Generic embed, to be duplicated from for each embed response
	.set_color(dpp::colors::construction_cone_orange)
	.set_timestamp(time(0));

//---Constants that should never change---//
static const std::string bankPrefix = "bank:/";						// FMOD-internal path prefix for banks
static const std::string eventPrefix = "event:/";					// ...for events
static const std::string busPrefix = "bus:/";						// ...for busses
static const std::string vcaPrefix = "vca:/";						// ...for vcas
static const std::string snapshotPrefix = "snapshot:/";				// ...for snapshots
static const std::string paramPrefix = "parameter:/";				// ...for parameters


//---File Paths---//
static std::filesystem::path exePath;
static std::filesystem::path banksDirPath;
static std::filesystem::path soundsDirPath;

//---FMOD Declarations---//
static FMOD::Studio::System* pSystem = nullptr;						// FMOD Studio system
static FMOD::System* pCoreSystem = nullptr;							// FMOD Core system
static FMOD::Studio::Bus* pMasterBus = nullptr;						// Master bus
static FMOD::ChannelGroup* pMasterBusGroup = nullptr;				// Channel Group of the master bus
static FMOD::DSP* mCaptureDSP = nullptr;							// DSP to attach to Master Channel Group for stealing output
static FMOD::ChannelGroup* pCoreGroup = nullptr;					// The group we'll route low-level playback through

// Banks
static FMOD::Studio::Bank* pMasterBank = nullptr;					// Master Bank, always loads first and contains shared content
static FMOD::Studio::Bank* pMasterStringsBank = nullptr;			// Master Strings, allows us to refer to events by name instead of GUID
static std::vector<std::filesystem::path> bankPaths;				// Vector of paths to the respective .bank files (at time of load)
static std::vector<FMOD::Studio::Bank*> pBanks;						// Vector of all other banks

// Events
static std::vector<std::string> eventPaths;							// Vector of FMOD-internal paths the user can call
static std::map<std::string, sessionEventDesc> pEventDescriptions;	// Map of all Event Descriptions and their parameters (with Nice Names as keys)
static std::map<std::string, sessionEventInstance> pEventInstances;	// Map of all Event Instances (with User-given names as keys)

// Busses
static std::vector<std::string> busPaths;							// Vector of FMOD-internal paths to each bus
static std::map <std::string, FMOD::Studio::Bus*> pBusses;			// Map of pointers to each bus object, by their path/name

// VCAs
static std::vector<std::string> vcaPaths;							// Same as busses but for VCAs
static std::map <std::string, FMOD::Studio::VCA*> pVCAs;			// ...

// Snapshots
static std::vector<std::string> snapshotPaths;						// Same as events but for Snapshots
static std::map<std::string, FMOD::Studio::EventDescription*> pSnapshotDescriptions;
static std::map<std::string, FMOD::Studio::EventInstance*> pSnapshotInstances;

// Global Parameters
static std::vector<std::string> globalParamNames;					// Similar but for Global Params (Local live with each event)
static std::map<std::string, FMOD_STUDIO_PARAMETER_DESCRIPTION> globalParamDescriptions;

// Loose audio files
static std::vector<std::string> soundPaths;							// Similar but for loose sound files
static std::map<std::string, FMOD::Sound*> pSounds;					// Like Event Descriptions but created on-the-fly so users have playback options
static std::map<std::string, FMOD::Channel*> pChannels;				// Like Event Instances, sorta


//---Misc Bot Declarations---//
static dpp::application botapp;									// Application object of the bot. Defined from a callback during startup
static dpp::discord_voice_client* currentClient = nullptr;		// Current Voice Client of the bot. Only designed to run on one server
static std::vector<int16_t> pcmDataBuffer;						// Our buffer of PCM audio data, which FMOD adds to and D++ cuts "frames" from
static bool exitRequested = false;								// Set to "true" when you want off Mr. Bones Wild Tunes.
static bool isConnected = false;								// Set to "true" when bot is connected to a Voice Channel.
static std::set<dpp::snowflake> authorizedUsers;				// Whitelisted users, including Owner.



//---FMOD and Audio Functions---//

// Callback for stealing sample data from the Master Bus
static FMOD_RESULT F_CALL captureDSPReadCallback(FMOD_DSP_STATE* dsp_state, float* inbuffer,
	float* outbuffer, unsigned int length, int inchannels, int* outchannels) {

	if (isConnected) {
		switch (inchannels) {
			case 1:																				//Mono Input
				if (*outchannels == 1) {
					for (unsigned int samp = 0; samp < length; samp++) {
						outbuffer[(samp * *outchannels)] = 0.0f;										//Brute force mutes system output
						pcmDataBuffer.push_back(floatToPCM(inbuffer[samp]));					//Adds sample to PCM buffer...
						pcmDataBuffer.push_back(floatToPCM(inbuffer[samp]));					//...twice, because D++ expects stereo input
					}
				}
				else {
					std::cout << "Capture DSP Read Error: Mono in, but not mono out!" << std::endl;
				}
				break;

			case 2:																				//Stereo Input
				for (unsigned int samp = 0; samp < length; samp++) {
					for (int chan = 0; chan < *outchannels; chan++) {
						pcmDataBuffer.push_back(floatToPCM(inbuffer[(samp * inchannels) + chan]));			//Adds sample to PCM buffer in approprate channel
					}
				}
				break;

			//Add cases here for other input channel configurations. Ultimately, all must mix down to stereo.

			default:
				std::cout << "Capture DSP Read needs mono or stereo in and out!" << std::endl;
				return FMOD_ERR_DSP_DONTPROCESS;
				break;
		}
	}
	return FMOD_ERR_DSP_SILENCE;		//ensures System output is silent without manually telling every sample to be 0.0f
}

// Callback that triggers when an Event Instance is released
static FMOD_RESULT F_CALL eventInstanceDestroyedCallback(FMOD_STUDIO_EVENT_CALLBACK_TYPE type,
	FMOD_STUDIO_EVENTINSTANCE* event, void* parameters) {

	FMOD::Studio::EventInstance* myEvent = (FMOD::Studio::EventInstance*)event;		// Cast approved by Firelight in the documentation
	// Despite the name, this callback will receive callbacks of all types, and so must filter them out
	if (type == FMOD_STUDIO_EVENT_CALLBACK_DESTROYED) {								// Redundant due to callback mask?
		std::cout << "Event Destroyed Callback Triggered." << std::endl;

		// Get some data to differentiate Events from Snapshots
		FMOD::Studio::EventDescription* myEventDesc = nullptr;
		myEvent->getDescription(&myEventDesc);
		bool isSnapshot = false;
		myEventDesc->isSnapshot(&isSnapshot);

		// Iterate through the respective Instance map and erase the entry associated with this Instance.
		if (!isSnapshot) {		// Event
			for (const auto& [niceName, sessionEventInstance] : pEventInstances) {
				if (myEvent == sessionEventInstance.instance) {
					std::cout << "Event Instance destroyed, erasing key from pEventInstances: " << niceName << std::endl;
					pEventInstances.erase(niceName);
				}
			}
		}
		else {					// Snapshot
			for (const auto& [niceName, snapshotInstance] : pSnapshotInstances) {
				if (myEvent == snapshotInstance) {
					std::cout << "Snapshot destroyed, erasing key from pSnapshotInstances: " << niceName << std::endl;
					pSnapshotInstances.erase(niceName);
				}
			}
		}
	}
	return FMOD_OK;
}

static FMOD_RESULT F_CALL soundChannelControlCallback(FMOD_CHANNELCONTROL *channelcontrol,
	FMOD_CHANNELCONTROL_TYPE controltype, FMOD_CHANNELCONTROL_CALLBACK_TYPE callbacktype,
	void *commanddata1, void *commanddata2) {

	coreCallbackChannelControlObj callbackObj{.channel = nullptr, .channelGroup = nullptr};

	// Cast depending on the type, Channel or ChannelGroup
	// Should usually be a channel, but you never know
	switch (controltype) {
	case FMOD_CHANNELCONTROL_CHANNEL:
		callbackObj.channel = (FMOD::Channel*)channelcontrol;
		break;
	case FMOD_CHANNELCONTROL_CHANNELGROUP:
		callbackObj.channelGroup = (FMOD::ChannelGroup*)channelcontrol;
		break;
	default:
		std::cout << "soundChannelControlCallback on unrecognized thing! Isn't a channel, nor a ChannelGroup." <<
			" This is definitely a bug of some kind." << std::endl;
	}

	//Only one callback allowed per-sound, so filter here depending on callback type
	switch (callbacktype) {
	// This case should only work if the callbackObj is a Channel (not Channel Group)
	case FMOD_CHANNELCONTROL_CALLBACK_END:
		FMOD_MODE mode;
		callbackObj.channel->getMode(&mode);
		std::cout << "sound mode: " << std::to_string(mode) << std::endl;
		if (mode == FMOD_LOOP_OFF) {
			callbackObj.channel->stop();
			// Channels don't get released, FMOD handles that for us,
			// so just remove it from our list (if found with reverse search).
			// Also don't release Sounds, that'll unload the file itself.

			//std::string foundKey;
			for (auto it = pChannels.begin(); it != pChannels.end(); ++it) {
				if (it->second == callbackObj.channel) {
					//foundKey = it->first;
					pChannels.erase(it->first);
				}
			}
		}
		break;
	default:
		break;
	}

	return FMOD_OK;
}

//---Bot Functions---//

// Simple ping, responds in chat and output log
static void ping(const dpp::slashcommand_t& event) {
	event.reply(dpp::message("Pong! I'm alive!").set_flags(dpp::m_ephemeral));
	std::cout << "Responding to Ping command." << std::endl;
}

// Simple Help function, meant to list all commands. Must be manually updated.
static void help(const dpp::slashcommand_t& event) {

	// Command and Subcommand data
	dpp::command_interaction cmd_data = event.command.get_command_interaction();
	bool isPublic = false;

	// Check the input variables are good
	unsigned int count = (unsigned int)cmd_data.options.size();
	if (count > 2) {
		std::cout << "Help command arrived with too many arguments. Bad juju!" << std::endl;
		event.reply(dpp::message("Help command sent with too many arguments. That shouldn't happen.").set_flags(dpp::m_ephemeral));
		return;
	}

	isPublic = std::get<bool>(cmd_data.options.at(0).value);

	dpp::embed helpEmbed = basicEmbed;		// Create the embed and set non-standard details
	helpEmbed.set_title("Available Commands")
		.set_description("A bot to make audio for Tabletop Games more interesting through Discord, using Game Audio tools and techniques.")

		.add_field("/ping", "Ping the bot to ensure it's alive.")
		.add_field("/playable", "List all playable Events, their Parameters, and Snapshots, as well as all Sound files.")
		.add_field("/list", "Show all playing Event and Snapshot instances, as well as their Parameters, and all loose Sounds.")
		.add_field("/play", "Play a new Event, Snapshot, or Sound.")
		.add_field("/pause", "Pause a currently playing Event.")
		.add_field("/unpause", "Resume a currently paused Event.")
		.add_field("/keyoff", "Key off a sustain point, if the Event has any.")
		.add_field("/stop", "Stop a currently playing Event, Snapshot, or Sound.")
		.add_field("/stopall", "Stop all Events, Snapshots, and Sounds immediately.")
		.add_field("/param", "Set a Parameter, globally or on an Event instance.")
		.add_field("/volume", "Set the volume of a Bus or VCA.")
		.add_field("/banks", "List all banks in the Soundbanks folder.")
		.add_field("/join", "Join your current voice channel.")
		.add_field("/leave", "Leave the current voice channel.")
		.add_field("/user", "List, Add, or Remove user permissions.")
		.add_field("/quit", "Leave voice and exit the program.")
		.add_field("/help", "Show this message again!");

	if (isPublic) { event.reply(dpp::message(helpEmbed)); }
	else { event.reply(dpp::message(helpEmbed).set_flags(dpp::m_ephemeral)); }
}

// Base function, called on startup and when requested by List Banks command
static void banks() {

	std::cout << "Checking Banks path: " << banksDirPath.string() << "\n";

	std::set<std::filesystem::path> sortedOutput;

	//Form set of .bank files from the soundbanks folder
	for (const auto& entry : std::filesystem::directory_iterator(banksDirPath)) {		// For every entry found in the banks folder
		if (entry.is_directory()) {														// Skip if directory...
			std::cout << "   Skipped: " << entry.path().string() << " -- is a directory." << "\n";
			continue;
		}
		else if (entry.path().extension() != ".bank") {				// Skip if not a bank...
			std::cout << "   Skipped: " << entry.path().string() << " -- Extension is "
				<< entry.path().extension() << " which isn't an FMOD bank." << "\n";
			continue;
		}
		else if ((entry.path().filename() == "Master.bank")			// Skip if Master or Strings...
			|| (entry.path().filename() == "Master.strings.bank")) {
			std::cout << "   Skipped: " << entry.path().string() << " -- is Master or Strings bank." << "\n";
		}
		else {
			std::cout << "   Accepted: " << entry.path().string() << "\n";			// Accepted!
			sortedOutput.insert(entry.path());
		}
	}
	// Now, for each key pass to the bank Paths vector
	// We do this to automatically sort, by virtue of how std::set works
	for (auto & key : sortedOutput) {
		bankPaths.push_back(key);
	}

	std::cout << std::endl;

	//Get list of already loaded banks
	int count = 0;
	errorCheckFMODHard(pSystem->getBankCount(&count));
	std::vector<FMOD::Studio::Bank*> loadedBanks; loadedBanks.resize(count);
	int writtenCount = 0;
	errorCheckFMODHard(pSystem->getBankList(loadedBanks.data(), count, &writtenCount));

	std::cout << "Loading Banks...\n";

	// For every accepted bank path, add to output list, and attempt to load
	for (int i = 0; i < (int)bankPaths.size(); i++) {					// For every accepted bank path

		FMOD::Studio::Bank* newBank = nullptr;						// Load, if bank qualifies
		FMOD_RESULT result = pSystem->loadBankFile(bankPaths[i].string().c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &newBank);
		if (result == FMOD_OK) {
			pBanks.push_back(newBank);
			std::cout << "   Loaded: " << bankPaths[i].string() << "\n";
		}
		else if (result == FMOD_ERR_EVENT_ALREADY_LOADED) {
			std::cout << "   Skipped Load (bank already loaded in FMOD Studio): " << bankPaths[i].string() << "\n";
		}
		else {
			errorCheckFMODHard(result);
		}
	}
	std::cout << std::endl;

	// We deliberately don't unload banks that are no longer used.
	// This can be a problem over a long runtime if the user is adding and removing a ton of banks,
	// especially if they want to load a _new_ bank with the same name as a previous bank,
	// but it's an exceptionally hard problem to solve that can be fixed by simply restarting.
	// If that changes in the future...unload unused banks here!
}

// Indexes and Prints all valid .bank files
static void banks(const dpp::slashcommand_t& event) {

	if (pEventInstances.size() > 0) {	// Unsafe to load/unload banks while events are active
		event.reply(dpp::message("It's dangerous to mess with banks while the bot is playing audio! Please stop all events first.").set_flags(dpp::m_ephemeral));
		return;
	}
	
	bankPaths.clear();		// Clear current Bank vector

	//Show "Thinking..." while putting the list together
	event.thinking(true, [event](const dpp::confirmation_callback_t& callback) {
		banks();					// Re-list what banks exist, load new ones

		dpp::embed bankListEmbed = basicEmbed;		// Create the embed and set non-standard details
		bankListEmbed.set_title("Found FMOD Banks");

		// Known banks, program fails if these don't load / exist
		bankListEmbed.add_field("Required Banks", "- Master.bank\n- Master.strings.bank");

		std::string bankPathsOutput = "";
		std::cout << "BankPaths size: " << std::to_string(bankPaths.size()) << "\n";

		// For every additional bank, add the shortened path as a field
		for (int i = 0; i < (int)bankPaths.size(); i++) {					// For every path
			bankPathsOutput.append(bankPaths[i].filename().string());   
			continue;
		}

		if (!bankPathsOutput.empty()) {
			bankListEmbed.add_field("Additional Banks", bankPathsOutput);
		}

		event.edit_original_response(dpp::message(event.command.channel_id, bankListEmbed));
	});
}

// Indexes all Events, Busses, VCAs, Snapshots, etc. on Startup ONLY.
static void indexStudio() {
	// Make sure vectors and maps are clear
	eventPaths.clear();
	busPaths.clear();
	vcaPaths.clear();
	snapshotPaths.clear();

	int count = 0;
	errorCheckFMODHard(pMasterStringsBank->getStringCount(&count));
	if (count < 1) {
		std::cout << "Invalid strings count of " << count << ", that's a problem." << std::endl;
		std::cout << "Double check the Master.strings.bank file was loaded properly." << std::endl;
		return;
	}

	// Dump each section into sets to sort them, then we'll print after
	std::set<std::string> eventSet;
	std::set<std::string> busSet;
	std::set<std::string> vcaSet;
	std::set<std::string> snapshotSet;
	std::set<std::string> paramSet;
	std::set<std::string> bankSet;
	std::set<std::string> unrecognizedSet;

	// Loop through every entry
	for (int i = 0; i < count; i++) {
		FMOD_GUID pathGUID;
		std::vector<char> pathStringChars(256);
		char* pathStringCharsptr = pathStringChars.data();
		int retrieved = 0;

		// If the string doesn't fit in the allotted size, resize appropriately and continue
		// We do this because FMOD Studio will tell us exactly how long it _should_ be, but only after trying once
		FMOD_RESULT result = pMasterStringsBank->getStringInfo(i, &pathGUID, pathStringCharsptr, 256, &retrieved);
		if (result != FMOD_OK) {
			if (result == FMOD_ERR_TRUNCATED) {
				pathStringChars.resize(retrieved);
				pathStringCharsptr = pathStringChars.data();
			}
			else { errorCheckFMODHard(result); }
		}

		std::string pathString(pathStringCharsptr);

		// Is it an event?
		if ((pathString.find(eventPrefix, 0) == 0)) { eventSet.insert(pathString); }
		// Is it a bus?
		else if ((pathString.find(busPrefix, 0) == 0)) { busSet.insert(pathString); }
		// Is it a VCA?
		else if ((pathString.find(vcaPrefix, 0) == 0)) { vcaSet.insert(pathString); }
		// Is it a Snapshot?
		else if ((pathString.find(snapshotPrefix, 0) == 0)) { snapshotSet.insert(pathString); }
		// Is it a parameter?
		else if (pathString.find(paramPrefix) == 0) { paramSet.insert(pathString); }
		// Is it a bank?
		else if (pathString.find(bankPrefix, 0) == 0) { bankSet.insert(pathString); }
		// If it's none of the above, then we have NO idea what this thing is.
		else { unrecognizedSet.insert(pathString); }
	}

	// Print each sorted set if it has any entries, otherwise simply skip
	if (!eventSet.empty()) {
		std::cout << "Events:\n";
		for (auto& entry : eventSet) {
			// Skip it if not in the Master folder
			if (entry.find(callableEventPrefix, 0) != 0) {
				std::cout << "   Skipped as Event: " << entry << " -- Not in Master folder." << "\n";
				continue;
			}

			// What's left should be good for our eventPaths vector
			std::cout << "   Accepted as Event: " << entry << "\n";
			eventPaths.push_back(entry);

			// Grab associated Event Description
			FMOD::Studio::EventDescription* newEventDesc = nullptr;
			errorCheckFMODHard(pSystem->getEvent(entry.c_str(), &newEventDesc));
			sessionEventDesc newSessionEventDesc; newSessionEventDesc.description = newEventDesc;

			// Grab the name of each associated non-built-in parameter
			int descParamCount = 0;
			newSessionEventDesc.description->getParameterDescriptionCount(&descParamCount);

			for (int i = 0; i < descParamCount; i++) {
				FMOD_STUDIO_PARAMETER_DESCRIPTION parameter;
				newSessionEventDesc.description->getParameterDescriptionByIndex(i, &parameter);
				if (parameter.type == FMOD_STUDIO_PARAMETER_GAME_CONTROLLED) {
					std::string paramName(parameter.name);
					std::string coutString = "      ";

					coutString.append(" - Parameter: " + paramName);
					coutString.append(" " + paramMinMaxString(parameter));
					coutString.append(" " + paramAttributesString(parameter));
					std::cout << coutString;
					newSessionEventDesc.params.push_back(parameter);
				}
			}
			pEventDescriptions.insert({ truncateEventPath(entry), newSessionEventDesc });	// Add to map, connected to a trimmed "easy" path name
		}
	}
	
	if (!busSet.empty()) {
		std::cout << "Busses:\n";
		for (auto& entry : busSet) {
			if (entry == busPrefix) {		//The Master Bus is just "bus:/"
				std::cout << "   Accepted as Bus: " << entry << " -- Is Master Bus.\n";
			}
			else {
				// Get the Bus and add it to the map
				FMOD::Studio::Bus* newBus = nullptr;
				errorCheckFMODHard(pSystem->getBus(entry.c_str(), &newBus));
				pBusses.insert({ truncateBusPath(entry), newBus });
				busPaths.push_back(entry);
				std::cout << "   Accepted as Bus: " << entry << " || Nice Name: " << truncateBusPath(entry) << "\n";
			}
		}
	}
	
	if (!vcaSet.empty()) {
		std::cout << "VCAs:\n";
		for (auto& entry : vcaSet) {
			// Get the VCA and add it to the map
			FMOD::Studio::VCA* newVCA = nullptr;
			errorCheckFMODHard(pSystem->getVCA(entry.c_str(), &newVCA));
			pVCAs.insert({ truncateVCAPath(entry), newVCA });

			vcaPaths.push_back(entry);
			std::cout << "   Accepted as VCA: " << entry << " || Nice Name: " << truncateVCAPath(entry) << "\n";
		}
	}
	
	if (!snapshotSet.empty()) {
		std::cout << "Snapshots:\n";
		for (auto& entry : snapshotSet) {
			// Get the Snapshot and add it to the map
			FMOD::Studio::EventDescription* newSnapshot = nullptr;
			errorCheckFMODHard(pSystem->getEvent(entry.c_str(), &newSnapshot));

			bool isSnapshot = false;
			newSnapshot->isSnapshot(&isSnapshot);
			if (!isSnapshot) {		//If this event description isn't actually a Snapshot
				std::cout << "   Skipped as Snapshot: " << entry << " -- Not actually a snapshot!" << "\n";
			}
			else {
				pSnapshotDescriptions.insert({ truncateSnapshotPath(entry), newSnapshot });
				snapshotPaths.push_back(entry);
				std::cout << "   Accepted as Snapshot: " << entry << " || Nice Name: " << truncateSnapshotPath(entry) << "\n";
			}
		}
	}
	
	// List the skipped items, if there are any
	if (!paramSet.empty() || !bankSet.empty() || !unrecognizedSet.empty()) {
		std::cout << "Skipped:\n";

		if (!paramSet.empty()) {
			for (auto& entry : paramSet) {
				std::cout << "   Skipped as Parameter: " << entry << "\n";
			}
		}
		
		if (!bankSet.empty()) {
			for (auto& entry : bankSet) {
				std::cout << "   Skipped as Bank: " << entry << "\n";
			}
		}
		
		if (!unrecognizedSet.empty()) {
			for (auto& entry : unrecognizedSet) {
				std::cout << "   Skipped as unrecognized string: " << entry << "\n";
			}
		}
		
		std::cout << std::endl;
	}
	

	// Seperately, get the list of Global Parameters
	// For this purpose we assume there's at least 1 Global Param
	std::vector<FMOD_STUDIO_PARAMETER_DESCRIPTION> paramVector(1);
	FMOD_STUDIO_PARAMETER_DESCRIPTION* paramVectorPtr = paramVector.data();
	int paramCount = 0;
	errorCheckFMODHard(pSystem->getParameterDescriptionCount(&paramCount));
	paramVector.resize(paramCount);
	paramVectorPtr = paramVector.data();
	errorCheckFMODHard(pSystem->getParameterDescriptionList(paramVectorPtr, paramCount, &paramCount));

	// Add them to the vector, one-by-one
	std::cout << "   Global Parameters:\n";
	if (paramCount < 1) { std::cout << "      ...none\n"; }
	for (int i = 0; i < paramCount; i++) {
		globalParamNames.push_back(paramVector[i].name);
		globalParamDescriptions.insert({ paramVector[i].name, paramVector[i] });

		std::string coutString = "      - ";
		coutString.append(paramVector[i].name);
		coutString.append(" " + paramMinMaxString(paramVector[i]));
		coutString.append(" " + paramAttributesString(paramVector[i], false));
		std::cout << coutString << "\n";
	}

}

// Indexes all loose sound files, for playback with FMOD Core. On Startup ONLY.
static void indexCore() {
	// Make sure vectors and maps are clear
	pSounds.clear();
	//for (auto& entry : pChannels) { entry.second->stop(); }
	pChannels.clear();

	// Get a set of the valid files (not necessarily sounds) in the soundfiles folder
	std::set<std::filesystem::path> files = getSoundFiles(soundsDirPath);

	// Attempt to load each sound
	for (auto& entry : files) {
		FMOD::Sound* newSound = nullptr;
		FMOD_RESULT result = pCoreSystem->createSound(entry.string().c_str(), FMOD_DEFAULT, nullptr, &newSound);

		// If there's an error, give a warning in the console and toss it out
		if (result != FMOD_OK) {
			errorCheckFMODSoft(result);
			std::cout << "Offending file: " << entry.string() << "\n\n";
			files.erase(entry);
		}
		else {
			// Some sanitization to translate filepath to user-friendly paths
			std::cout << "  Accepted: " << entry.string() << "\n";
			pSounds.insert({ formatPathToSoundfile(entry, soundsDirPath), newSound});
		}
	}
	if (pSounds.size() > 0) {
		std::cout << "Sounds:\n";
		for (auto& entry : pSounds) {
			std::cout << "   " << entry.first << "\n";
		}
	}
	
}

// Prints all currently indexed Events, Snapshots, Global Parameters, Busses, and VCAs.
static void list(const dpp::slashcommand_t& event) {
	dpp::command_interaction cmd_data = event.command.get_command_interaction();

	// Check the input variables are good
	unsigned int count = (unsigned int)cmd_data.options.size();
	if (count > 2) {
		std::cout << "List command received with too many arguments.\n";
		event.reply(dpp::message("List command received with too many arguments.").set_flags(dpp::m_ephemeral));
		return;
	}

	event.thinking(true, [event](const dpp::confirmation_callback_t& callback) {

		// Basic setup
		dpp::embed listEmbed = basicEmbed;
		listEmbed.set_title("Dashboard List");
		std::cout << "Listing current:" << "\n";

		// Event Instances
		if (pEventInstances.empty()) {
			std::cout << "   No playing Event Instances.\n";
			listEmbed.add_field("Active Events", "- No active events");
		}
		else {
			std::string eventInstanceList = "";
			for (auto const& inst : pEventInstances) {		// For each Event Instance

				// Get the Event Instance name
				std::string instName = inst.first;

				// Get the related Event Description
				FMOD::Studio::EventDescription* instDesc;
				inst.second.instance->getDescription(&instDesc);

				// Get the Event Description's name
				std::vector<char> pathchars(256);
				char* pathptr = pathchars.data();
				int retrieved = 0;
				FMOD_RESULT result = instDesc->getPath(pathptr, 256, &retrieved);
				if (result != FMOD_OK) {
					if (result == FMOD_ERR_TRUNCATED) {
						pathchars.resize(retrieved);
						pathptr = pathchars.data();
					}
					else { errorCheckFMODSoft(result); }
				}

				std::string instDescName(pathptr);					// Make string, then format
				instDescName = truncateEventPath(instDescName);

				eventInstanceList.append("- __" + instName + "__");	// Append the event Instance name

				if (!inst.second.params.empty()) {			// If this Instance has any parameters associated...
					std::string paramOutString = " - source event: " + instDescName + "\n";	// add in the event name
					for (unsigned int i = 0; i < inst.second.params.size(); i++) {
						// get current parameter name & value
						std::string paramName(inst.second.params[i].name);
						float paramVal = 0; float paramFinalVal = 0;
						errorCheckFMODHard(inst.second.instance->getParameterByID(inst.second.params[i].id, &paramVal, &paramFinalVal));
						std::string paramValStr = paramValueString(paramVal, inst.second.params[i]);

						// Get the min/max
						std::string paramMinMaxStr = paramMinMaxString(inst.second.params[i]);
						// Get the Attributes
						std::string paramAttributesStr = paramAttributesString(inst.second.params[i]);

						// Glue 'em all together, adding new lines per-parameter
						paramOutString.append(" - " + paramName + ": " + paramValStr + "  " + paramMinMaxStr + paramAttributesStr);
						if (i > 0) { paramOutString.append("\n"); }
					}
					eventInstanceList.append("\n" + paramOutString);
				}
				eventInstanceList.append("\n");		// Close it off, new line, next event
			}
			listEmbed.add_field("Active Events", eventInstanceList);
		}


		// Global Parameters
		if (globalParamDescriptions.empty()) {
			std::cout << "   No current Global Parameters." << std::endl;
			listEmbed.add_field("Global Parameters", "- No Global Parameters");
		}
		else {
			std::string globalParametersList = "";

			for (auto const& param : globalParamDescriptions) {

				// Get the Global Parameter's name
				std::string paramName = param.first;

				// Get the Parameter's current value
				float paramVal = 0;
				errorCheckFMODHard(pSystem->getParameterByName(param.second.name, &paramVal));
				std::string paramValStr = paramValueString(paramVal, param.second);

				// Get the min/max
				std::string paramMinMaxStr = paramMinMaxString(param.second);
				// Get the Attributes
				std::string paramAttributesStr = paramAttributesString(param.second);

				// Glue 'em all together, adding new lines per-parameter
				globalParametersList.append("- " + paramName + ": " + paramValStr + "  " + paramMinMaxStr + paramAttributesStr + "\n");
			}
			listEmbed.add_field("Global Parameters", globalParametersList);
		}


		// Snapshots
		if (pSnapshotInstances.empty()) {
			std::cout << "   No current Snapshots." << std::endl;
			listEmbed.add_field("Active Snapshots", "- No Snapshots active");
		}
		else {
			std::string snapshotsList = "";

			for (auto const& snapshot : pSnapshotInstances) {

				// Get the Event Instance name
				std::string snapName = snapshot.first;

				// Get the related Event Description
				FMOD::Studio::EventDescription* instDesc = nullptr;
				snapshot.second->getDescription(&instDesc);

				// Get the Event Description's name
				std::vector<char> pathchars(256);
				char* pathptr = pathchars.data();
				int retrieved = 0;
				FMOD_RESULT result = instDesc->getPath(pathptr, 256, &retrieved);
				if (result != FMOD_OK) {
					if (result == FMOD_ERR_TRUNCATED) {
						pathchars.resize(retrieved);
						pathptr = pathchars.data();
					}
					else { errorCheckFMODSoft(result); }
				}

				std::string instDescName(pathptr);							// Make string, then format
				instDescName = truncateEventPath(instDescName);
				snapshotsList.append("- " + snapName + "\n");	// Append the event Instance name
			}
			listEmbed.add_field("Active Snapshots", snapshotsList);
		}
		
		// Some workarounds to extract the values from "event" rather than passing them in (forbidden due to lambda)
		int count = (int)event.command.get_command_interaction().options.size();
		bool showFaders = false;
		if (count > 0) {
			showFaders = std::get<bool>(event.get_parameter(event.command.get_command_interaction().options[0].name));
		}

		// Busses and VCAs
		if (showFaders) {
			// Busses
			if (pBusses.empty()) {
				std::cout << "   No current Busses." << std::endl;
				listEmbed.add_field("Busses", "- No Busses, somehow. This is either a bug, or you've messed up the FMOD project.");
			}
			else {
				std::string bussesList = "";
				
				//Todo: Sort the Busses in a hierarchical way. Maybe by number of slash chars in name? Maybe when these are indexed?
				for (auto const& bus : pBusses) {

					// Get the Bus name
					std::string busName = bus.first;

					float value = 0;
					errorCheckFMODHard(bus.second->getVolume(&value));
					value = floatTodB(value);

					bussesList.append("- " + busName + ": " + volumeString(value) + "\n");
				}
				listEmbed.add_field("Busses", bussesList);
			}

			// VCAs
			if (pVCAs.empty()) {
				std::cout << "   No current VCAs." << std::endl;
				listEmbed.add_field("VCAs", "- No Current VCAs");
			}
			else {
				std::string vcaList = "";

				for (auto const& vca : pVCAs) {

					// Get the VCA name
					std::string vcaName = vca.first;

					float value = 0;
					errorCheckFMODHard(vca.second->getVolume(&value));
					value = floatTodB(value);

					vcaList.append("- " + vcaName + ": " + volumeString(value) + "\n");
				}
				listEmbed.add_field("VCAs", vcaList);
			}
		}

		// Send it off, finally
		event.edit_original_response(dpp::message(event.command.channel_id, listEmbed));
	});
}

// Base function, called when requested by Playable command
static void playable() {
	// Get the number of strings in the Master Strings bank
	int count = 0;
	errorCheckFMODHard(pMasterStringsBank->getStringCount(&count));
	if (count <= 0) {
		std::cout << "Invalid strings count of " << count << ", that's a problem." << "\n";
		std::cout << "Double check the Master.strings.bank file was loaded properly." << std::endl;
		return;
	}

	std::cout << "Refreshing playables list..." << "\n";

	// Get a copy of the event Paths list, and the same for the Snapshots path list
	std::vector<std::string> existingEvents = eventPaths;
	std::vector<std::string> existingSnapshots = snapshotPaths;

	// For every entry
	for (int i = 0; i < count; i++) {
		// Get the Event Description's name and details
		std::vector<char> pathStringChars(256);
		char* pathStringCharsptr = pathStringChars.data();
		int retrieved = 0;
		FMOD_RESULT result = pMasterStringsBank->getStringInfo(i, nullptr, pathStringCharsptr, 256, &retrieved);
		if (result != FMOD_OK) {
			if (result == FMOD_ERR_TRUNCATED) {
				pathStringChars.resize(retrieved);
				pathStringCharsptr = pathStringChars.data();
			}
			else { errorCheckFMODHard(result); }
		}
		std::string pathString(pathStringCharsptr);

		// Discard if this string isn't an event or snapshot
		if ((pathString.find(eventPrefix, 0) != 0) && (pathString.find(snapshotPrefix, 0) != 0)) {	// Skip if not Event or Snapshot...
			std::cout << "   Skipped: " << pathString << " -- Not Event or Snapshot." << std::endl;
			continue;
		}
		// Discard if it's an event outside the Master folder
		if ((pathString.find(eventPrefix, 0) == 0) && (pathString.find(callableEventPrefix, 0) != 0)) {								// Skip if not in Master folder...
			std::cout << "   Skipped: " << pathString << " -- Event not in Master folder." << std::endl;
			continue;
		}

		// Discard strings that are already loaded
		// Also remove them from their respective "existing" lists, such that whatever remains
		// on those lists is no longer available (removed during Live Connect?)
		if (!eventPaths.empty()) {
			bool alreadyExistsEvent = false;
			for (unsigned int j = 0; i < (int)eventPaths.size(); j++) {
				if (eventPaths[i] == pathString) { alreadyExistsEvent = true; break; }
			}
			if (alreadyExistsEvent) {
				std::cout << "   Skipped: " << pathString << " -- Event already listed." << std::endl;
				for (unsigned int k = 0; k < existingEvents.size(); k++) {
					if (pathString == existingEvents[k]) {
						existingEvents.erase(existingEvents.begin() + k);
					}
				}
				continue;
			}
		}

		if (!snapshotPaths.empty()) {
			bool alreadyExistsSnapshot = false;
			for (unsigned int j = 0; i < (int)snapshotPaths.size(); j++) {
				if (snapshotPaths[i] == pathString) { alreadyExistsSnapshot = true; break; }
			}
			if (alreadyExistsSnapshot) {
				std::cout << "   Skipped: " << pathString << " -- Snapshot already listed." << std::endl;
				for (unsigned int k = 0; k < existingSnapshots.size(); k++) {
					if (pathString == existingSnapshots[k]) {
						existingSnapshots.erase(existingSnapshots.begin() + k);
					}
				}
				continue;
			}
		}

		// What's left should be good for our vectors
		
		// Events
		if ((pathString.find(eventPrefix, 0) == 0)) {
			std::cout << "   Accepted as Event: " << pathString << std::endl;
			eventPaths.push_back(pathString);

			// Grab associated Event Description
			FMOD::Studio::EventDescription* newEventDesc = nullptr;
			errorCheckFMODHard(pSystem->getEvent(pathString.c_str(), &newEventDesc));
			sessionEventDesc newSessionEventDesc; newSessionEventDesc.description = newEventDesc;

			// Grab the name of each associated non-built-in parameter
			int descParamCount = 0;
			newSessionEventDesc.description->getParameterDescriptionCount(&descParamCount);
			for (int i = 0; i < descParamCount; i++) {
				FMOD_STUDIO_PARAMETER_DESCRIPTION parameter;
				newSessionEventDesc.description->getParameterDescriptionByIndex(i, &parameter);
				if (parameter.type == FMOD_STUDIO_PARAMETER_GAME_CONTROLLED) {
					std::string paramName(parameter.name);
					std::string coutString = "      ";

					coutString.append(" - Parameter: " + paramName);
					coutString.append(" " + paramMinMaxString(parameter));
					coutString.append(" " + paramAttributesString(parameter));
					std::cout << coutString;
					newSessionEventDesc.params.push_back(parameter);
				}
			}
			pEventDescriptions.insert({ truncateEventPath(pathString), newSessionEventDesc });		// Add to map, connected to a trimmed "easy" path name
		}

		// Snapshots
		else {
			std::cout << "   Accepted as Snapshot: " << pathString << std::endl;
			snapshotPaths.push_back(pathString);

			// Grab Snapshot's Event Description and add to Snapshots map
			FMOD::Studio::EventDescription* newSnapshotDesc = nullptr;
			errorCheckFMODHard(pSystem->getEvent(pathString.c_str(), &newSnapshotDesc));
			pSnapshotDescriptions.insert({truncateSnapshotPath(pathString), newSnapshotDesc});
		}
	}
	std::cout << "...Done!" << "\n" << std::endl;
}

// Indexes and Prints all playable Event Descriptions & Parameters.
static void playable(const dpp::slashcommand_t& event) {

	if (!pMasterStringsBank->isValid() || pMasterStringsBank == nullptr) {
		std::cout << "Master Strings bank is invalid or nullptr. Bad juju!" << std::endl;
		event.reply(dpp::message("Master Strings bank is invalid or nullptr. Bad juju!")
			.set_flags(dpp::m_ephemeral));
		return;
	}

	dpp::command_interaction cmd_data = event.command.get_command_interaction();

	// Check the input variables are good
	unsigned int count = (unsigned int)cmd_data.options.size();
	if (count > 1) {
		std::cout << "Playable command received with too many arguments." << std::endl;
		event.reply(dpp::message("Playable command received with too many arguments.").set_flags(dpp::m_ephemeral));
		return;
	}

	event.thinking(true, [event](const dpp::confirmation_callback_t& callback) {

		// Get whether the command came with the optional "reindex" parameter
		unsigned int count = (unsigned int)event.command.get_command_interaction().options.size();
		bool reindex = false;
		if (count > 0) {
			reindex = std::get<bool>(event.get_parameter(event.command.get_command_interaction().options[0].name));
		}

		// If told to, clear all event description vectors/maps, then re-index
		if (reindex) {
			eventPaths.clear();
			pEventDescriptions.clear();
			snapshotPaths.clear();
			pSnapshotDescriptions.clear();

			playable();
		}
		else { std::cout << "Listing Playables without re-indexing." << std::endl; }

		// And now print 'em to Discord!
		if ((eventPaths.size() == 0) && (snapshotPaths.size() == 0)) {
			// If no playables are found, say so.
			event.edit_original_response(dpp::message("No playable Events or Snapshots found!"));
		}
		else {
			dpp::embed paramListEmbed = basicEmbed;								// Create the embed and set non-standard details
			paramListEmbed.set_title("Playables");

			std::string playableEventsOutput = "";

			// Events
			for (int i = 0; i < (int)eventPaths.size(); i++) {								// For every path	
				std::vector<FMOD_STUDIO_PARAMETER_DESCRIPTION> eventParams = pEventDescriptions.at(truncateEventPath(eventPaths[i])).params;
				playableEventsOutput.append("- " + truncateEventPath(eventPaths[i]) + "\n");

				if (eventParams.size() != 0) {
					std::string paramOutString = "";
					for (int j = 0; j < (int)eventParams.size(); j++) {			// as well as each associated parameters and their ranges, if any.
						paramOutString.append("  - ");
						paramOutString.append(eventParams[j].name);
						paramOutString.append(" ");
						paramOutString.append(paramMinMaxString(eventParams[j]) + paramAttributesString(eventParams[j]));
					}
					playableEventsOutput.append(paramOutString);
				}
			}
			paramListEmbed.add_field("Events", playableEventsOutput);

			std::string playableSnapshotsOutput = "";
			// Snapshots 
			for (int i = 0; i < (int)snapshotPaths.size(); i++) {						// For every path	
				playableSnapshotsOutput.append("- ");
				playableSnapshotsOutput.append(truncateSnapshotPath(snapshotPaths[i]) + "\n");
			}
			paramListEmbed.add_field("Snapshots", playableSnapshotsOutput);

			std::string playableFilesOutput = "";
			// Files
			for (auto& entry : pSounds) {
				playableFilesOutput.append("- ");
				playableFilesOutput.append(entry.first + "\n");
			}
			paramListEmbed.add_field("Files", playableFilesOutput);

			event.edit_original_response(dpp::message(event.command.channel_id, paramListEmbed));
		}
	});
}

// Play Sub-Command: create a new Instance of an event.
static void play_event(const dpp::slashcommand_t& event, const std::string& eventToPlay, const std::string& inputName) {

	std::string newName = inputName;
	std::string cleanName = newName;
	int iterator = 1;
	while (pEventInstances.find(newName) != pEventInstances.end()) {		// If that name already exists, quietly give it a number
		newName = cleanName + "-" + std::to_string(iterator);				// Keep counting up until valid.
		iterator++;
	}

	std::cout << "Play Event command issued." << "\n";
	std::cout << "Event to Play: " << eventToPlay << " || Instance name: " << newName << std::endl;

	FMOD::Studio::EventDescription* newEventDesc = nullptr;

	if (pEventDescriptions.contains(eventToPlay)) {
		newEventDesc = pEventDescriptions.at(eventToPlay).description;
	}

	if ((newEventDesc != nullptr) && (newEventDesc->isValid())) {
		FMOD::Studio::EventInstance* newEventInst = nullptr;
		errorCheckFMODHard(newEventDesc->createInstance(&newEventInst));
		std::vector<FMOD_STUDIO_PARAMETER_DESCRIPTION> newEventParams = pEventDescriptions.at(eventToPlay).params;
		sessionEventInstance newSessionEventInst;
		newSessionEventInst.instance = newEventInst;
		newSessionEventInst.params = newEventParams;
		pEventInstances.insert({ newName, newSessionEventInst });
		errorCheckFMODHard(pEventInstances.at(newName).instance->setCallback(eventInstanceDestroyedCallback, FMOD_STUDIO_EVENT_CALLBACK_DESTROYED));
		errorCheckFMODHard(pEventInstances.at(newName).instance->start());
		errorCheckFMODHard(pEventInstances.at(newName).instance->release());

		std::cout << "Playing event: " << eventToPlay << " with Instance name: " << newName << std::endl;
		event.reply(dpp::message("Playing event: " + eventToPlay + " with Instance name: " + newName).set_flags(dpp::m_ephemeral));
	}
	else {
		std::cout << "No valid Event found with the given path." << std::endl;
		event.reply(dpp::message("No valid Event found with the given path.").set_flags(dpp::m_ephemeral));
	}
}

// Play Sub-Command: create a new Instance of a snapshot.
static void play_snapshot(const dpp::slashcommand_t& event, const std::string& eventToPlay, const std::string& inputName) {

	std::string newName = inputName;
	std::string cleanName = newName;
	int iterator = 1;
	while (pSnapshotInstances.find(newName) != pSnapshotInstances.end()) {		// If that name already exists, quietly give it a number
		newName = cleanName + "-" + std::to_string(iterator);				// Keep counting up until valid.
		iterator++;
	}

	std::cout << "Play Snapshot command issued." << "\n";
	std::cout << "Snapshot to Play: " << eventToPlay << " || Instance name: " << newName << std::endl;

	FMOD::Studio::EventDescription* newSnapDesc = nullptr;
	
	if (pSnapshotDescriptions.contains(eventToPlay)) {
		newSnapDesc = pSnapshotDescriptions.at(eventToPlay);
	}

	if ((newSnapDesc != nullptr) && (newSnapDesc->isValid())) {
		FMOD::Studio::EventInstance* newSnapInst = nullptr;
		errorCheckFMODHard(newSnapDesc->createInstance(&newSnapInst));
		errorCheckFMODHard(newSnapInst->setCallback(eventInstanceDestroyedCallback, FMOD_STUDIO_EVENT_CALLBACK_DESTROYED));
		errorCheckFMODHard(newSnapInst->start());
		errorCheckFMODHard(newSnapInst->release());
		pSnapshotInstances.insert({ newName, newSnapInst });

		std::cout << "Playing snapshot: " << eventToPlay << " with Instance name: " << newName << std::endl;
		event.reply(dpp::message("Playing snapshot: " + eventToPlay + " with Instance name: " + newName).set_flags(dpp::m_ephemeral));
	}
	else {
		std::cout << "No valid Snapshot found with the given path." << std::endl;
		event.reply(dpp::message("No valid Snapshot found with the given path.").set_flags(dpp::m_ephemeral));
	}
}

// Play Sub-Command: create a new Channel and play a sound through it immediately.
static void play_file(const dpp::slashcommand_t& event, const std::string& soundToPlay, const std::string& inputName, const bool& isLoop) {
	//Determine Channel and Instance name
	std::string newName = inputName;
	std::string cleanName = newName;
	int iterator = 1;
	while (pChannels.find(newName) != pChannels.end()) {		// If that name already exists, quietly give it a number
		newName = cleanName + "-" + std::to_string(iterator);				// Keep counting up until valid.
		iterator++;
	}

	FMOD::Sound* newSound = nullptr;
	if (pSounds.contains(soundToPlay)) {
		newSound = pSounds.at(soundToPlay);
	}

	// Todo: find other error-checking methods here, to fill-in for Studio's isValid() method
	if (newSound != nullptr) {
		FMOD::Channel* newChannel = nullptr;
		if (isLoop) { newChannel->setMode(FMOD_LOOP_NORMAL); }
		else { newChannel->setMode(FMOD_LOOP_OFF); }
		pCoreSystem->playSound(newSound, pCoreGroup, true, &newChannel);
		newChannel->setCallback(soundChannelControlCallback);
		newChannel->setPaused(false);
		pChannels.insert({ newName, newChannel });

		std::cout << "Playing Sound: " << soundToPlay << " with Instance name: " << newName << std::endl;
		event.reply(dpp::message("Playing Sound: " + soundToPlay + " with Instance name: " + newName).set_flags(dpp::m_ephemeral));
	}
	else {
		std::cout << "No valid Sound found with the given path and filename." << std::endl;
		event.reply(dpp::message("No valid Sound found with the given path.").set_flags(dpp::m_ephemeral));
	}
}

// Creates and starts a new Event Instance.
static void play(const dpp::slashcommand_t& event) {
	// Command and Subcommand data
	dpp::command_interaction cmd_data = event.command.get_command_interaction();
	dpp::command_data_option subcommand = cmd_data.options[0];

	// Check the input variables are good
	int count = (int)cmd_data.options.size();
	if (count < 1) {
		std::cout << "Play command arrived with no arguments or subcommands. Bad juju!" << std::endl;
		event.reply(dpp::message("Play command sent with no arguments or subcommands. Bad juju!").set_flags(dpp::m_ephemeral));
		return;
	}
	else if (count > 2) {
		std::cout << "Play command arrived with too many arguments. Bad juju!" << std::endl;
		event.reply(dpp::message("Play command sent with too many arguments. Bad juju!").set_flags(dpp::m_ephemeral));
		return;
	}

	int subcount = (int)subcommand.options.size();
	if (subcount < 1) {
		std::cout << "Play " << subcommand.name << " command arrived without enough arguments.Bad juju!" << std::endl;
		event.reply(dpp::message("Play " + subcommand.name + " command sent without enough arguments. Bad juju!").set_flags(dpp::m_ephemeral));
		return;
	}
	else if (((subcommand.name == "file") && (subcount > 3)) || (subcount > 2)) {
		std::cout << "Play " << subcommand.name << " command arrived with too many arguments. Bad juju!" << std::endl;
		event.reply(dpp::message("Play " + subcommand.name + " command sent with too many arguments. Bad juju!").set_flags(dpp::m_ephemeral));
		return;
	}

	std::string eventToPlay = std::get<std::string>(event.get_parameter(subcommand.options[0].name));

	// Checking the Instance name
	// If the user gave a name in the command, use that
	// If the user gave no name, use the Event name
	std::string inputName;
	if (subcount > 1) {
		inputName = std::get<std::string>(event.get_parameter(subcommand.options[1].name));
		if (inputName == "" || inputName.size() == 0) { inputName = eventToPlay; }		// If the input was bogus, pretend there was no input
	}
	else { inputName = eventToPlay; }

	// Divert to the proper subcommand
	if (subcommand.name == "event") { play_event(event, eventToPlay, inputName); }
	else if (subcommand.name == "snapshot") { play_snapshot(event, eventToPlay, inputName); }
	else if (subcommand.name == "file") {
		// Extra logic for Play File cmd's extra isLoop parameter
		bool isLoop = false;
		if (subcommand.options[1].type == dpp::co_boolean) { isLoop = std::get<bool>(event.get_parameter(subcommand.options[1].name)); }
		else if (subcommand.options[2].type == dpp::co_boolean) { isLoop = std::get<bool>(event.get_parameter(subcommand.options[2].name)); }
		play_file(event, eventToPlay, inputName, isLoop);
	}
	else { event.reply(dpp::message("Used Play event without subcommand. This is a bug and not supported.").set_flags(dpp::m_ephemeral)); }
}

// Pauses Event with given name in events playing list.
static void pause(const dpp::slashcommand_t& event) {
	//Very similar to Unpause and Stop
	dpp::command_interaction cmd_data = event.command.get_command_interaction();
	int count = (int)cmd_data.options.size();
	if (count < 1) {
		std::cout << "Pause command arrived with no arguments. Bad juju!" << std::endl;
		event.reply(dpp::message("Pause command sent with no arguments. Bad juju!").set_flags(dpp::m_ephemeral));
		return;
	}

	std::string inputName = std::get<std::string>(event.get_parameter(cmd_data.options[0].name));

	std::cout << "Pause command issued." << std::endl;
	std::cout << "Instance Name: " << inputName << std::endl;
	if (pEventInstances.find(inputName) != pEventInstances.end()) {
		pEventInstances.at(inputName).instance->setPaused(true);
		std::cout << "Pause command carried out." << std::endl;
		event.reply(dpp::message("Pausing Event Instance: " + inputName).set_flags(dpp::m_ephemeral));
	}
	else if (pSnapshotInstances.find(inputName) != pSnapshotInstances.end()) {
		pSnapshotInstances.at(inputName)->setPaused(true);
		std::cout << "Pause command carried out." << std::endl;
		event.reply(dpp::message("Pausing Snapshot: " + inputName).set_flags(dpp::m_ephemeral));
	}
	else {
		std::cout << "Couldn't find Instance with given name." << std::endl;
		event.reply(dpp::message("No Event Instance found with given name: " + inputName).set_flags(dpp::m_ephemeral));
	}
}

// Unpauses event with given name in events playing list.
static void unpause(const dpp::slashcommand_t& event) {
	//Very similar to Pause and Stop
	dpp::command_interaction cmd_data = event.command.get_command_interaction();
	int count = (int)cmd_data.options.size();
	if (count < 1) {
		std::cout << "Unpause command arrived with no arguments. Bad juju!" << std::endl;
		event.reply(dpp::message("Unpause command sent with no arguments. Bad juju!").set_flags(dpp::m_ephemeral));
		return;
	}

	std::string inputName = std::get<std::string>(event.get_parameter(cmd_data.options[0].name));

	std::cout << "Unpause command issued." << std::endl;
	std::cout << "Instance Name: " << inputName << std::endl;
	if (pEventInstances.find(inputName) != pEventInstances.end()) {
		pEventInstances.at(inputName).instance->setPaused(false);
		std::cout << "Unpause command carried out." << std::endl;
		event.reply(dpp::message("Unpausing Event Instance: " + inputName).set_flags(dpp::m_ephemeral));
	}
	else if (pSnapshotInstances.find(inputName) != pSnapshotInstances.end()) {
		pSnapshotInstances.at(inputName)->setPaused(false);
		std::cout << "Unpause command carried out." << std::endl;
		event.reply(dpp::message("Unpausing Snapshot: " + inputName).set_flags(dpp::m_ephemeral));
	}
	else {
		std::cout << "Couldn't find Instance with given name." << std::endl;
		event.reply(dpp::message("No Event Instance found with given name: " + inputName).set_flags(dpp::m_ephemeral));
	}
}

// Key Off for the given Event Instance.
static void keyoff(const dpp::slashcommand_t& event) {
	//Very similar to Pause, Unpause, and Stop.
	dpp::command_interaction cmd_data = event.command.get_command_interaction();
	int count = (int)cmd_data.options.size();
	if (count < 1) {
		std::cout << "Keyoff command arrived with no arguments. Bad juju!" << std::endl;
		event.reply(dpp::message("Keyoff command sent with no arguments. Bad juju!").set_flags(dpp::m_ephemeral));
		return;
	}

	std::string inputName = std::get<std::string>(event.get_parameter(cmd_data.options[0].name));

	std::cout << "Key Off command issued." << std::endl;
	std::cout << "Instance Name: " << inputName << std::endl;
	if (pEventInstances.find(inputName) != pEventInstances.end()) {
		FMOD::Studio::EventDescription* eventDesc = nullptr;
		errorCheckFMODHard(pEventInstances.at(inputName).instance->getDescription(&eventDesc));
		bool hasSusPoint = false;
		eventDesc->hasSustainPoint(&hasSusPoint);
		if (hasSusPoint) {
			pEventInstances.at(inputName).instance->keyOff();
			std::cout << "KeyOff command carried out." << std::endl;
			event.reply(dpp::message("Keying Off event instance: " + inputName).set_flags(dpp::m_ephemeral));
		}
		else {
			std::cout << "KeyOff command skipped: event has no keys to off." << std::endl;
			event.reply(dpp::message("Event Instance " + inputName + " has no keys to off.").set_flags(dpp::m_ephemeral));
		}
	}
	else {
		std::cout << "Couldn't find Event Instance with given name." << std::endl;
		event.reply(dpp::message("No Event Instance found with given name: " + inputName).set_flags(dpp::m_ephemeral));
	}
}

// Stops Event or Snapshot with given name in events playing list.
static void stop(const dpp::slashcommand_t& event) {
	// Very similar to Pause and Unpause
	dpp::command_interaction cmd_data = event.command.get_command_interaction();
	int count = (int)cmd_data.options.size();
	if (count < 1) {			// If somehow this was sent without arguments, that's bad.
		std::cout << "Stop command arrived with no arguments. Bad juju!" << std::endl;
		event.reply(dpp::message("Stop command sent with no arguments. Bad juju!").set_flags(dpp::m_ephemeral));
		return;
	}

	std::cout << "Stop command issued." << std::endl;
	std::string inputName = std::get<std::string>(event.get_parameter(cmd_data.options[0].name));
	bool value;
	if (count > 1) { value = std::get<bool>(event.get_parameter(cmd_data.options[2].name)); }
	else { value = false; }

	std::cout << "Instance Name: " << inputName << std::endl;
	if (pEventInstances.find(inputName) != pEventInstances.end()) {
		if (value) { pEventInstances.at(inputName).instance->stop(FMOD_STUDIO_STOP_ALLOWFADEOUT); }
		else { pEventInstances.at(inputName).instance->stop(FMOD_STUDIO_STOP_IMMEDIATE); }
		//Callback should handle removing this instance from our map when the event is done.
		std::cout << "Stop command carried out." << std::endl;
		event.reply(dpp::message("Stopping Event Instance: " + inputName).set_flags(dpp::m_ephemeral));
	}
	else if (pSnapshotInstances.find(inputName) != pSnapshotInstances.end()) {
		if (value) { pSnapshotInstances.at(inputName)->stop(FMOD_STUDIO_STOP_ALLOWFADEOUT); }
		else { pSnapshotInstances.at(inputName)->stop(FMOD_STUDIO_STOP_IMMEDIATE); }
		std::cout << "Stop command carried out." << std::endl;
		event.reply(dpp::message("Stopping Snapshot: " + inputName).set_flags(dpp::m_ephemeral));
	}
	else {
		std::cout << "Couldn't find Instance with given name." << std::endl;
		event.reply(dpp::message("No Event Instance or Snapshot found with given name: " + inputName).set_flags(dpp::m_ephemeral));
	}
	
}

// Base function, called in a few places as part of other methods like quit().
static void stopall_events() {
	std::cout << "Stopping all events...";
	//For each instance in events playing list, stop_now
	for (const auto& [niceName, sessionEventInstance] : pEventInstances) {
		sessionEventInstance.instance->stop(FMOD_STUDIO_STOP_IMMEDIATE);
	}
	std::cout << "Done." << std::endl;
}

// Base function, stops all snapshots.
static void stopall_snapshots() {
	std::cout << "Stopping Snapshots...";
	for (const auto& [niceName, snapshotInstance] : pSnapshotInstances) {
		snapshotInstance->stop(FMOD_STUDIO_STOP_IMMEDIATE);
	}
	std::cout << "Done." << std::endl;
}

// Base function, stops all loose files.
static void stopall_files() {
	std::cout << "Stopping Files...";
	for (auto& entry : pChannels) {
		entry.second->stop();
	}
	std::cout << "Done." << std::endl;
}

// Base function, stops all Events, Snapshots, and Files.
static void stopall() {
	stopall_events();
	stopall_snapshots();
	stopall_files();
}

// Stops all playing Events, Snapshots, and Files in the list.
static void stopall(const dpp::slashcommand_t& event) {
	stopall();
	event.reply(dpp::message("All events stopped.").set_flags(dpp::m_ephemeral));
}

// Param Sub-Command: Sets parameter with given name and value, globally.
static void param_global(const dpp::slashcommand_t& event, const dpp::command_data_option& subcommand) {
	int count = (int)subcommand.options.size();
	if (count < 2) {
		std::cout << "Set Parameter command arrived with no arguments. Bad juju!" << std::endl;
		event.reply(dpp::message("Set Parameter command sent with no arguments. Bad juju!").set_flags(dpp::m_ephemeral));
	}

	std::cout << "Set Parameter command issued." << std::endl;
	std::string paramName = std::get<std::string>(event.get_parameter(subcommand.options[0].name));
	float value = (float)std::get<double>(event.get_parameter(subcommand.options[1].name));

	// Check for parameter in list of known params
	if (!globalParamDescriptions.contains(paramName)) {					// If that parameter name isn't in our list of Global Params
		event.reply(dpp::message("Parameter " + paramName + " not found in Global Parameter list.").set_flags(dpp::m_ephemeral));
		return;
	}

	// Set parameter
	errorCheckFMODHard(pSystem->setParameterByName(paramName.c_str(), value));
	std::cout << "Command carried out." << std::endl;
	event.reply(dpp::message("Setting Global Parameter: " + paramName + " with value " + paramValueString(value, globalParamDescriptions.at(paramName)))
		.set_flags(dpp::m_ephemeral));
}

// Param Sub-Command: Sets parameter with given name and value on given Event Instance.
static void param_event(const dpp::slashcommand_t& event, const dpp::command_data_option& subcommand) {
	int count = (int)subcommand.options.size();
	if (count < 3) {
		std::cout << "Set Parameter command arrived with no arguments. Bad juju!" << std::endl;
		event.reply(dpp::message("Set Parameter command sent with no arguments. Bad juju!").set_flags(dpp::m_ephemeral));
	}

	std::cout << "Set Parameter command issued." << std::endl;
	std::string instanceName = std::get<std::string>(event.get_parameter(subcommand.options[0].name));
	std::string paramName = std::get<std::string>(event.get_parameter(subcommand.options[1].name));
	float value = (float)std::get<double>(event.get_parameter(subcommand.options[2].name));

	std::cout << "Instance Name: " << instanceName << std::endl;
	if (pEventInstances.find(instanceName) == pEventInstances.end()) {
		std::cout << "Couldn't find Instance with given name." << std::endl;
		event.reply(dpp::message("No Event Instance found with given name: " + instanceName).set_flags(dpp::m_ephemeral));
	}
	else if (pEventInstances.at(instanceName).params.size() == 0) {
		std::cout << "Instance has no parameters." << std::endl;
		event.reply(dpp::message("Instance " + instanceName + " has no parameters associated with it.").set_flags(dpp::m_ephemeral));
	}

	int foundParamIndex = -1;
	for (int i = 0; i < (int)pEventInstances.at(instanceName).params.size(); i++) {
		if (pEventInstances.at(instanceName).params[i].name == paramName) {
			foundParamIndex = i;
		}
	}
	if (foundParamIndex < 0) {
		std::cout << "Parameter with that name couldn't be found." << std::endl;
		event.reply(dpp::message("Instance " + instanceName + " has no parameters of name " + paramName + " associated with it.").set_flags(dpp::m_ephemeral));
	}

	// Finally set the parameter
	errorCheckFMODHard(pEventInstances.at(instanceName).instance->setParameterByName(paramName.c_str(), value));
	std::cout << "Command carried out." << std::endl;
	event.reply(dpp::message("Setting Parameter: " + paramName + " on Instance " + instanceName + " with value " + std::to_string(value)).set_flags(dpp::m_ephemeral));
}

// Sets parameter with given name and value, either Globally or on an Event Instance.
static void param(const dpp::slashcommand_t& event) {
	dpp::command_interaction cmd_data = event.command.get_command_interaction();
	dpp::command_data_option subcommand = cmd_data.options[0];
	if (subcommand.name == "event") { param_event(event, subcommand); }
	else if (subcommand.name == "global") { param_global(event, subcommand); }
}

// Sets the volume of a given Bus or VCA.
static void volume(const dpp::slashcommand_t& event) {
	dpp::command_interaction cmd_data = event.command.get_command_interaction();
	int count = (int)cmd_data.options.size();
	if (count < 2) {
		std::cout << "Set Volume command arrived with improper arguments. Bad juju!" << std::endl;
		event.reply(dpp::message("Set Volume command arrived with improper arguments. Bad juju!").set_flags(dpp::m_ephemeral));
	}

	std::cout << "Set Volume command issued." << std::endl;
	std::string busOrVCAName = std::get<std::string>(event.get_parameter(cmd_data.options[0].name));
	float value = (float)std::get<double>(event.get_parameter(cmd_data.options[1].name));
	if (value > 10.0f) { value *= -1; }
	value = dBToFloat(value);

	// If found in Busses map
	if (pBusses.contains(busOrVCAName)) {
		errorCheckFMODHard(pBusses.at(busOrVCAName)->setVolume(value));
		event.reply(dpp::message("Setting Bus: " + busOrVCAName + " to volume: " + std::to_string(floatTodB(value))).set_flags(dpp::m_ephemeral));
	}
	// Else if "Master" (not kept in Busses map)
	else if (busOrVCAName == "Master" || busOrVCAName == "master") {
		errorCheckFMODHard(pMasterBus->setVolume(value + fmodMasterBusVolOffset));
		event.reply(dpp::message("Setting Bus: Master to volume: " + std::to_string(floatTodB(value))).set_flags(dpp::m_ephemeral));
	}
	// Else if found in VCA map
	else if (pVCAs.contains(busOrVCAName)) {
		errorCheckFMODHard(pVCAs.at(busOrVCAName)->setVolume(value));
		event.reply(dpp::message("Setting VCA: " + busOrVCAName + " to volume: " + std::to_string(floatTodB(value))).set_flags(dpp::m_ephemeral));
	}
}

// Joins the voice channel of the user who gives the slash command.
static void join(const dpp::slashcommand_t& event) {
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
		std::cout << "Joined channel of user." << std::endl;
		event.reply(dpp::message("Joined your voice channel!").set_flags(dpp::m_ephemeral));

	}
	else {
		event.reply(dpp::message("I am already living in your walls.").set_flags(dpp::m_ephemeral));
		std::cout << "Already living in your walls." << std::endl;
	}
}

// Leaves the current voice channel.
static void leave(const dpp::slashcommand_t& event, bool eventRespond = true) {
	dpp::voiceconn* currentVC = event.from->get_voice(event.command.guild_id);
	if (currentVC) {
		std::cout << "Leaving voice channel." << std::endl;
		stopall();			// Stop all events and snapshots immediately

		isConnected = false;
		currentClient->stop_audio();
		currentClient = nullptr;

		event.from->disconnect_voice(event.command.guild_id);	// Disconnect from Voice (triggers callback laid out in main)
		
		if (eventRespond) {
			event.reply(dpp::message("Bye bye! I hope I played good sounds!").set_flags(dpp::m_ephemeral));
		}
	}
	else {
		if (eventRespond) {
			event.reply(dpp::message("A problem occured when trying to leave the voice channel.").set_flags(dpp::m_ephemeral));
		}
	}
}

// Quit the program, leaving the voice channel if we're in one.
static void quit(const dpp::slashcommand_t& event) {
	if (isConnected && (currentClient != nullptr)) {
		leave(event, false);		//Leave voice, but don't reply to the event
	}
	exitRequested = true;
	std::cout << "Quit command received." << std::endl;
	event.reply(dpp::message("Shutting down. Bye bye! I hope I played good sounds!").set_flags(dpp::m_ephemeral));
}

// Lists all authorized users (usernames if known and definitely their snowflake ID's).
static void user_list(const dpp::slashcommand_t& event/*, const dpp::command_data_option& subcommand*/) {
	std::set<dpp::snowflake> authorizedList = getAuthorizedUsers();
	dpp::embed userListEmbed = basicEmbed;

	userListEmbed.set_title("Authorized Users");

	if (authorizedList.size() > 0) {
		for (auto& snowflake : authorizedList) {
			dpp::user *test = dpp::find_user(snowflake);
			if (test != nullptr) {
				userListEmbed.add_field(test->username, snowflake.str());
			}
			else {
				userListEmbed.add_field("(Unresolved Username)", snowflake.str());
			}
		}
	}
	else {
		userListEmbed.set_description("No Authorized Users found...this shouldn't be possible.");
	}

	event.reply(dpp::message(event.command.channel_id, userListEmbed).set_flags(dpp::m_ephemeral));
}

// Adds an authorized user to the list.
static void user_add(const dpp::slashcommand_t& event, const dpp::command_data_option& subcommand) {

	int subcount = (int)subcommand.options.size();
	if (subcount < 1) {
		std::cout << "Play " << subcommand.name << " command arrived without enough arguments.Bad juju!" << std::endl;
		event.reply(dpp::message("Play " + subcommand.name + " command sent without enough arguments. Bad juju!").set_flags(dpp::m_ephemeral));
		return;
	}
	else if (subcount > 2) {
		std::cout << "Play " << subcommand.name << " command arrived with too many arguments. Bad juju!" << std::endl;
		event.reply(dpp::message("Play " + subcommand.name + " command sent with too many arguments. Bad juju!").set_flags(dpp::m_ephemeral));
		return;
	}

	dpp::snowflake snowflakeToAdd = std::get<dpp::snowflake>(subcommand.options[0].value);
	dpp::user* userToAdd = dpp::find_user(snowflakeToAdd);

	if (userToAdd != nullptr) {
		if (addAuthorizedUser(snowflakeToAdd)) {
			std::cout << "Added user to Authorized Users list: " << userToAdd->username << std::endl;
			event.reply(dpp::message("Added user " + userToAdd->username + " with Snowflake ID " + snowflakeToAdd.str() + " to Authorized Users list.").set_flags(dpp::m_ephemeral));
		}
		else {
			std::cout << "Some error occured with adding the Authorized User." << std::endl;
			event.reply(dpp::message("Some error occured with adding the Authorized User: " + userToAdd->username).set_flags(dpp::m_ephemeral));

		}
	}
	else {
		std::cout << "Adding uncached user with Snowflake ID: " << snowflakeToAdd.str() << "\n";
		if (addAuthorizedUser(snowflakeToAdd)) {
			std::cout << "Added user to Authorized Users list." << std::endl;
			event.reply(dpp::message("Added user (unresolved) with Snowflake ID " + snowflakeToAdd.str() + " to Authorized Users list.").set_flags(dpp::m_ephemeral));
		}
		else {
			std::cout << "Some error occured with adding the Authorized User." << std::endl;
			event.reply(dpp::message("Some error occured with adding the Authorized User.").set_flags(dpp::m_ephemeral));
		}
	}
}

// Removes an authorized user from the list.
static void user_remove(const dpp::slashcommand_t& event, const dpp::command_data_option& subcommand) {

	dpp::snowflake snowflakeToAdd = std::get<dpp::snowflake>(subcommand.options[0].value);
	dpp::user* userToAdd = dpp::find_user(snowflakeToAdd);

	if (userToAdd != nullptr) {
		std::cout << "Removing Authorized User " << userToAdd->username << " with Snowflake ID: " << snowflakeToAdd << "\n";
		dpp::snowflake ownerSnowflake(botapp.owner.id);
		if (removeAuthorizedUser(snowflakeToAdd, authorizedUsers, ownerSnowflake)) {
			std::cout << "User removed successfully." << std::endl;
			event.reply(dpp::message("User " + userToAdd->username + " removed successfully.").set_flags(dpp::m_ephemeral));
		}
		else {
			std::cout << "User removal failed. User may not exist in the list, or there may have been a failure in removal." << std::endl;
			event.reply(dpp::message("An error occured with removing the Authorized User: " + userToAdd->username).set_flags(dpp::m_ephemeral));
		}
	}
	else {
		std::cout << "Removing Authorized User with Snowflake ID: " << snowflakeToAdd << "\n";
		if (removeAuthorizedUser(snowflakeToAdd, authorizedUsers)) {
			std::cout << "User removed successfully." << std::endl;
			event.reply(dpp::message("User successfully removed.").set_flags(dpp::m_ephemeral));
		}
		else {
			std::cout << "User removal failed. User may not exist in the list, or there may have been a failure in removal." << std::endl;
			event.reply(dpp::message("An error occured with removing the Authorized User.").set_flags(dpp::m_ephemeral));
		}
	}
}

// General function for interacting with user permissions for this bot.
static void user(const dpp::slashcommand_t& event) {
	dpp::command_interaction cmd_data = event.command.get_command_interaction();

	// Check the input variables are good
	int count = (int)cmd_data.options.size();
	if (count < 1) {
		std::cout << "Play command arrived with no arguments or subcommands. Bad juju!" << std::endl;
		event.reply(dpp::message("Play command sent with no arguments or subcommands. Bad juju!").set_flags(dpp::m_ephemeral));
		return;
	}
	else if (count > 2) {
		std::cout << "Play command arrived with too many arguments. Bad juju!" << std::endl;
		event.reply(dpp::message("Play command sent with too many arguments. Bad juju!").set_flags(dpp::m_ephemeral));
		return;
	}

	dpp::command_data_option subcommand = cmd_data.options[0];

	if (subcommand.name == "list") { user_list(event/*, subcommand*/); }
	else if (subcommand.name == "add") { user_add(event, subcommand); }
	else if (subcommand.name == "remove") { user_remove(event, subcommand); }
}

// Callback function called when bot's Application object is acquired
static void onBotAppGet(const dpp::confirmation_callback_t& callbackObj) {
	if (!callbackObj.is_error()) {
		botapp = callbackObj.get<dpp::application>();
		std::cout << "Owner added with Username: " << botapp.owner.username << " and Snowflake ID: " << botapp.owner.id << "\n";
		addAuthorizedUser(botapp.owner.id, true);
		authorizedUsers.insert(botapp.owner.id);
		// Get other authorized users

	}
	else {
		std::cout << "Error getting bot application object: " << callbackObj.get_error().human_readable << std::endl;
		exit(0);
	}
}

// Initialize FMOD, DSP, and filepaths for later reference
static void init() {
	std::cout << "###########################" << "\n";
	std::cout << "###                     ###" << "\n";
	std::cout << "###  Session Music Bot  ###" << "\n";
	std::cout << "###                     ###" << "\n";
	std::cout << "###########################" << "\n";
	std::cout << std::endl;

	// file paths
	exePath = getExecutableFolder();
	banksDirPath = exePath / ("soundbanks");		// Append method would affect exePath (bad)
	soundsDirPath = exePath / ("soundfiles");		// Slash operator concatenates paths

	// FMOD Init
	std::cout << "Initializing FMOD...";
	errorCheckFMODHard(FMOD::Studio::System::create(&pSystem));
	errorCheckFMODHard(pSystem->getCoreSystem(&pCoreSystem));
	errorCheckFMODHard(pSystem->initialize(128, FMOD_STUDIO_INIT_LIVEUPDATE, FMOD_INIT_NORMAL, nullptr));
	std::cout << "Done." << std::endl;

	// Load Master Bank and Master Strings
	std::cout << "Loading Master banks...";
	std::string basePath = banksDirPath.string() + "\\";
	errorCheckFMODHard(pSystem->loadBankFile((basePath + masterBankFile).c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &pMasterBank));
	errorCheckFMODHard(pSystem->loadBankFile((basePath + masterStringsFile).c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &pMasterStringsBank));
	std::cout << "Done." << std::endl;

	// Also get the Master Bus, set volume, and get the related Channel Group
	std::cout << "Getting Busses and Channel Groups...";
	errorCheckFMODHard(pSystem->getBus("bus:/", &pMasterBus));
	errorCheckFMODHard(pMasterBus->setVolume(dBToFloat(fmodMasterBusVolOffset)));
	errorCheckFMODHard(pMasterBus->lockChannelGroup());			// Tell the Master Channel Group to always exist even when events arn't playing...
	errorCheckFMODHard(pSystem->flushCommands());				// And wait until all previous commands are done (ensuring Channel Group exists)...
	errorCheckFMODHard(pMasterBus->getChannelGroup(&pMasterBusGroup));	// Or else this fails immediately and we'll have DSP problems.
	
	FMOD::Studio::Bus* pCoreBus = nullptr;
	pSystem->getBus("bus:/SubMaster/Files", &pCoreBus);
	pCoreBus->lockChannelGroup();
	pSystem->flushCommands();
	pCoreBus->getChannelGroup(&pCoreGroup);

	std::cout << "Done." << std::endl;
	

	// Define and create our capture DSP on the Master Channel Group.
	// Copied from FMOD's examples. Unsure why this specifically must be in brackets?
	std::cout << "Setting up Capture DSP...";
	{
		FMOD_DSP_DESCRIPTION dspdesc;
		memset(&dspdesc, 0, sizeof(dspdesc));
		strncpy_s(dspdesc.name, "LH_captureDSP", sizeof(dspdesc.name));
		dspdesc.version = 0x00010000;
		dspdesc.numinputbuffers = 1;
		dspdesc.numoutputbuffers = 1;
		// Important: "Read" must point to an appropriate F_CALL function that's always valid (not bound).
		// Since it also needs program data (isConnected and pcmDataBuffer), this is tough to refactor.
		dspdesc.read = captureDSPReadCallback;
		errorCheckFMODHard(pCoreSystem->createDSP(&dspdesc, &mCaptureDSP));
	}
	// Adds the newly defined dsp
	errorCheckFMODHard(pMasterBusGroup->addDSP(FMOD_CHANNELCONTROL_DSP_TAIL, mCaptureDSP));
	std::cout << "Done." << std::endl;

	// Setting Listener positioning for 3D, in case it's used 
	std::cout << "Setting up Listener...";
	FMOD_3D_ATTRIBUTES listenerAttributes;
	listenerAttributes.position = { 0.0f, 0.0f, 0.0f };
	listenerAttributes.forward = { 0.0f, 1.0f, 0.0f };
	listenerAttributes.up = { 0.0f, 0.0f, 1.0f };
	errorCheckFMODHard(pSystem->setListenerAttributes(0, &listenerAttributes));
	std::cout << "Done." << std::endl;

	// Debug details
	int samplerate; FMOD_SPEAKERMODE speakermode; int numrawspeakers;
	errorCheckFMODHard(pCoreSystem->getSoftwareFormat(&samplerate, &speakermode, &numrawspeakers));
	errorCheckFMODHard(pSystem->flushCommands());	// Ensure everything above is done before displaying details
	std::cout << "\n###########################\n\n";
	std::cout << "FMOD System Info:\n  Sample Rate- " << samplerate << "\n  Speaker Mode- " << speakermode
		<< "\n  Num Raw Speakers- " << numrawspeakers << "\n";
	std::cout << std::endl;
}

// Init function specifically for user-defined needs (loading banks, indexing events & params, etc)
static void init_session() {
	std::cout << "###########################\n\n";
	std::cout << "Loading and Indexing all other banks...\n\n";
	banks();
	std::cout << "...Done!\n" << std::endl;

	std::cout << "Indexing FMOD Studio objects...\n";
	indexStudio();
	std::cout << "...Done!\n\n";

	std::cout << "Indexing loose sound files...\n";
	indexCore();
	std::cout << "...Done!\n\n";
	std::cout << "###########################\n";
	std::cout << std::endl;
}

// Exit function to release FMOD resources before quitting the program
static void releaseFMOD() {
	// Stop everything, just in case
	stopall();

	// Remove DSP from master channel group, and release the DSP
	pMasterBusGroup->removeDSP(mCaptureDSP);
	mCaptureDSP->release();

	// Unload and release any FMOD Core sounds
	for (auto& sound : pSounds) {
		sound.second->release();
	}
	pSounds.clear();

	// Unload and release FMOD Studio System
	// This should unload and release the connected Core objects too
	pSystem->unloadAll();
	pSystem->release();
}

int main() {

	init();
	init_session();

	std::cout << "Starting Bot...\n" << std::endl;

	/* Create bot cluster */
	dpp::cluster bot(getBotToken());

	/* Output simple log messages to stdout */
	bot.on_log(dpp::utility::cout_logger());

	// Get the bot application, and add the Owner to the Owning Users list (for permissions)
	bot.current_application_get(onBotAppGet);

	/* Register slash command here in on_ready */
	bot.on_ready([&bot](const dpp::ready_t& event) {
		/* Wrap command registration in run_once to make sure it doesnt run on every full reconnection */
		if (dpp::run_once<struct register_bot_commands>()) {
			std::vector<dpp::slashcommand> commands {
				{ "playable", "List all playable Events, their Parameters, and Snapshots.", bot.me.id},
				{ "list", "Show all playing Event instances and their Parameters.", bot.me.id},
				{ "play", "Play a new Event, Snapshot, or Sound.", bot.me.id},
				{ "pause", "Pause a currently playing Event or Sound.", bot.me.id},
				{ "unpause", "Resume a currently paused Event or Sound.", bot.me.id},
				{ "keyoff", "Key off a sustain point, if the Event has any.", bot.me.id},
				{ "stop", "Stop a currently playing Event, Snapshot, or Sound.", bot.me.id},
				{ "stopall", "Stop all Events, Snapshots, and Sounds immediately.", bot.me.id},
				{ "param", "Set a Parameter, globally or on an Event instance.", bot.me.id},
				{ "volume", "Set the volume of a Bus or VCA.", bot.me.id},
				{ "ping", "Ping the bot to ensure it's alive.", bot.me.id },
				{ "banks", "List all banks in the Soundbanks folder.", bot.me.id},
				{ "join", "Join your current voice channel.", bot.me.id},
				{ "leave", "Leave the current voice channel.", bot.me.id},
				{ "quit", "Leave voice and exit the program.", bot.me.id},
				{ "user", "Add or Remove user permissions.", bot.me.id},
				{ "help", "List available commands and other info.", bot.me.id}
			};

			// Playable options
			commands[0].add_option(
				dpp::command_option(dpp::co_boolean, "should-reindex", "Whether to re-check all available Events and Snapshots, helpful for Live Connect.", false)
			);

			// List options
			commands[1].add_option(
				dpp::command_option(dpp::co_boolean, "include-faders", "Show available busses and VCAs. Snapshots are the suggested (easier) way of setting these values.", false)
			);
			
			// Play options
			// Sub-Command: Play Event
			dpp::command_option playEventSubCmd = dpp::command_option(dpp::co_sub_command, "event", "Create a new Event Instance.");
			playEventSubCmd.add_option(dpp::command_option(dpp::co_string, "event-name", "The Event you wish to play.", true).set_auto_complete(true));
			playEventSubCmd.add_option(dpp::command_option(dpp::co_string, "instance-name", "Optional: name used for interactions with this new Instance. Defaults to the name of the Event.", false));
			commands[2].add_option(playEventSubCmd);

			// Sub-Command: Play Snapshot
			dpp::command_option playSnapshotSubCmd = dpp::command_option(dpp::co_sub_command, "snapshot", "Create a new Snapshot.");
			playSnapshotSubCmd.add_option(dpp::command_option(dpp::co_string, "snapshot-name", "The Snapshot you wish to activate.", true).set_auto_complete(true));
			playSnapshotSubCmd.add_option(dpp::command_option(dpp::co_string, "instance-name", "Optional: name used for interactions with this new Instance. Defaults to the name of the Snapshot.", false));
			commands[2].add_option(playSnapshotSubCmd);

			// Sub-Command: Play File
			dpp::command_option playFileSubCmd = dpp::command_option(dpp::co_sub_command, "file", "Play a loose audio file.");
			playFileSubCmd.add_option(dpp::command_option(dpp::co_string, "file-name", "The file to play.", true).set_auto_complete(true));
			playFileSubCmd.add_option(dpp::command_option(dpp::co_string, "instance-name", "Optional: name used for interactions with this instance of the sound. Defaults to the filename.", false));
			playFileSubCmd.add_option(dpp::command_option(dpp::co_boolean, "is-loop", "Optional: whether to loop the file when it reaches the end. Default is False.", false));
			commands[2].add_option(playFileSubCmd);

			// Pause options
			commands[3].add_option(
				dpp::command_option(dpp::co_string, "instance-name", "The name of the Instance to pause.", true).set_auto_complete(true)
			);

			// Unpause options
			commands[4].add_option(
				dpp::command_option(dpp::co_string, "instance-name", "The name of the Instance to unpause.", true).set_auto_complete(true)
			);

			// KeyOff options
			commands[5].add_option(
				dpp::command_option(dpp::co_string, "instance-name", "The name of the Instance to key off.", true).set_auto_complete(true)
			);

			// Stop options
			commands[6].add_option(
				dpp::command_option(dpp::co_string, "instance-name", "The name of the Instance to stop. May be an Event or Snapshot.", true).set_auto_complete(true)
			);
			commands[6].add_option(
				dpp::command_option(dpp::co_boolean, "stop-immediately", "Optional: stop the Instance NOW, without fadeouts?", false)
			);

			// Stop_All options
			commands[7].add_option(
				dpp::command_option(dpp::co_boolean, "stop-immediately", "Optional: stop everything NOW, without fadeouts?", false)
			);

			// Param options
			// Sub-Command: Event Instance
			dpp::command_option eventInstSubCmd = dpp::command_option(dpp::co_sub_command, "event", "Set a local parameter.");
			eventInstSubCmd.add_option(dpp::command_option(dpp::co_string, "instance-name", "The name of the event instance to set parameters on.", true).set_auto_complete(true));
			eventInstSubCmd.add_option(dpp::command_option(dpp::co_string, "parameter-name", "The name of the parameter to set.", true).set_auto_complete(true));
			eventInstSubCmd.add_option(dpp::command_option(dpp::co_number, "value", "What you want the parameter to be.", true));
			commands[8].add_option(eventInstSubCmd);

			// Sub-Command: Global
			dpp::command_option globalSubCmd = dpp::command_option(dpp::co_sub_command, "global", "Set a Global parameter.");
			globalSubCmd.add_option(dpp::command_option(dpp::co_string, "parameter-name", "The name of the parameter to set.", true).set_auto_complete(true));
			globalSubCmd.add_option(dpp::command_option(dpp::co_number, "value", "What you want the parameter to be.", true));
			commands[8].add_option(globalSubCmd);

			// Sub-commands for Volume
			commands[9].add_option(
				dpp::command_option(dpp::co_string, "bus-or-vca-name", "The name of the Bus or VCA to adjust the volume of.", true).set_auto_complete(true)
			);
			commands[9].add_option(
				dpp::command_option(dpp::co_number, "value",
					"The target volume in dB. Values above +10 will be assumed negative, for your ears' sake.", true)
			);

			// User options
			// Sub-Command: List
			dpp::command_option listUsersSubCmd = dpp::command_option(dpp::co_sub_command, "list", "List current authorized users.");
			commands[15].add_option(listUsersSubCmd);

			// Sub-Command: Add
			dpp::command_option addUsersSubCmd = dpp::command_option(dpp::co_sub_command, "add", "Add a user to the Authorized list.");
			addUsersSubCmd.add_option(dpp::command_option(dpp::co_mentionable, "user", "The user to add. Will automatically grab their Snowflake ID.", true));
			commands[15].add_option(addUsersSubCmd);

			// Sub-Command: Remove
			dpp::command_option removeUsersSubCmd = dpp::command_option(dpp::co_sub_command, "remove", "Remove a user from the Authorized list.");
			removeUsersSubCmd.add_option(dpp::command_option(dpp::co_mentionable, "user", "The user to remove. Will automatically grab their Snowflake ID.", true));
			commands[15].add_option(removeUsersSubCmd);

			// Help
			commands[16].add_option(
				dpp::command_option(dpp::co_boolean, "post-publicly",
					"Whether to post the help message for everyone in the channel, or just for you.", false)
			);

			// Permissions. Show commands for only those who can use slash commands in a server.
			// Permission to _run_ the commands will be checked locally at runtime.
			for (unsigned int i = 0; i > commands.size(); i++) {
				commands[i].default_member_permissions.has(dpp::permissions::p_use_application_commands);
			}

			bot.global_bulk_command_create(commands);
		}
	});

	/* Handle slash commands */
	bot.on_slashcommand([&bot](const dpp::slashcommand_t& event) {

		// Filter out non-Owners from enacting commands
		std::cout << "Command received" << std::endl;
		dpp::user cmdSender = event.command.get_issuing_user();

		std::cout << "Command sent by " << cmdSender.username << " with Snowflake ID: " << cmdSender.id << "\n";
		std::cout << "authorizedUser snowflakes: \n";
		for (auto& user : authorizedUsers) { std::cout << "   " << user.str() << "\n"; }
		
		if (!authorizedUsers.contains(cmdSender.id)) {
			event.reply(dpp::message("Sorry, only authorized users can run commands for me.").set_flags(dpp::m_ephemeral));
		}
		else {
			if (event.command.get_command_name() == "playable") { playable(event); }
			else if (event.command.get_command_name() == "list") { list(event); }
			else if (event.command.get_command_name() == "play") { play(event); }
			else if (event.command.get_command_name() == "pause") { pause(event); }
			else if (event.command.get_command_name() == "unpause") { unpause(event); }
			else if (event.command.get_command_name() == "keyoff") { keyoff(event); }
			else if (event.command.get_command_name() == "stop") { stop(event); }
			else if (event.command.get_command_name() == "stopall") { stopall(event); }
			else if (event.command.get_command_name() == "param") { param(event); }
			else if (event.command.get_command_name() == "volume") { volume(event); }
			else if (event.command.get_command_name() == "ping") { ping(event); }
			else if (event.command.get_command_name() == "banks") { banks(event); }
			else if (event.command.get_command_name() == "join") { join(event); }
			else if (event.command.get_command_name() == "leave") { leave(event); }
			else if (event.command.get_command_name() == "quit") { quit(event); }
			else if (event.command.get_command_name() == "user") { user(event); }
			else if (event.command.get_command_name() == "help") { help(event); }
			else {
				event.reply(dpp::message("Sorry, " + event.command.get_command_name()
					+ " isn't a command I understand. Apologies.").set_flags(dpp::m_ephemeral));
			}
		}
	});

	/* Handle Auto-Complete for relevant commands */
	bot.on_autocomplete([&bot](const dpp::autocomplete_t& event) {
		// First because it's likely the most often used
		if (event.name == "play") {
			// Determine between the sub-commands to determine which list to pull from
			auto& subcmd = event.options[0];

			if (subcmd.name == "event") {
				for (auto& opt : subcmd.options) {
					// For each Event Description in our list, if the user's typed text exists in the name,
					// add it as an autocomplete option. Probably some clever way to cache this?
					if (opt.focused) {
						std::string uservalue = std::get<std::string>(opt.value);
						dpp::interaction_response eventDescList(dpp::ir_autocomplete_reply);
						// Add all the events in the prepared list
						for (unsigned int i = 0; i < eventPaths.size(); i++) {
							std::string pathOption = truncateEventPath(eventPaths.at(i));	//Could pre-compute sets of these
							// Only list matching event names; if empty, list all
							if ((pathOption.find(uservalue, 0) != std::string::npos) || (uservalue == "")) {
								eventDescList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
							}
						}
						bot.interaction_response_create(event.command.id, event.command.token, eventDescList);
					}
				}
			}
			else if (subcmd.name == "snapshot") {
				for (auto& opt : subcmd.options) {
					// Same but for Snapshot Descriptions
					if (opt.focused) {
						std::string uservalue = std::get<std::string>(opt.value);
						dpp::interaction_response snapshotDescList(dpp::ir_autocomplete_reply);
						for (unsigned int i = 0; i < snapshotPaths.size(); i++) {
							std::string pathOption = truncateSnapshotPath(snapshotPaths.at(i));		//Could pre-compute sets of these
							if ((pathOption.find(uservalue, 0) != std::string::npos) || (uservalue == "")) {
								snapshotDescList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
							}
						}
						bot.interaction_response_create(event.command.id, event.command.token, snapshotDescList);
					}
				}
			}
			else if (subcmd.name == "file") {
				for (auto& opt : subcmd.options) {
					// Similar for Files
					if (opt.focused) {
						std::string uservalue = std::get<std::string>(opt.value);
						dpp::interaction_response soundsList(dpp::ir_autocomplete_reply);
						for (auto& entry : pSounds) {
							std::string pathOption = entry.first;
							if ((pathOption.find(uservalue, 0) != std::string::npos) || (uservalue == "")) {
								soundsList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
							}
						}
						bot.interaction_response_create(event.command.id, event.command.token, soundsList);
					}
				}
			}
		}

		// Pause, Unpause, and KeyOff all use the same list of Event Instances
		else if (event.name == "pause" || event.name == "unpause") {
			for (auto& opt : event.options) {
				if (opt.focused) {
					std::string uservalue = std::get<std::string>(opt.value);
					dpp::interaction_response eventInstanceList(dpp::ir_autocomplete_reply);

					// For each Event Instance (Events, not Snapshots)
					for (std::map<std::string, sessionEventInstance>::iterator it = pEventInstances.begin(); it != pEventInstances.end(); ++it) {
						std::string pathOption = it->first;
						if ((pathOption.find(uservalue, 0) != std::string::npos) || (uservalue == "")) {
							eventInstanceList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
						}
					}

					// For each File Instance
					for (std::map<std::string, FMOD::Channel*>::iterator it = pChannels.begin(); it != pChannels.end(); ++it) {
						std::string pathOption = it->first;
						if ((pathOption.find(uservalue, 0) != std::string::npos) || (uservalue == "")) {
							eventInstanceList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
						}
					}

					bot.interaction_response_create(event.command.id, event.command.token, eventInstanceList);
				}
			}
		}

		// Keyoff is very similar to Pause/Unpause, but only applies to Event Instances
		else if (event.name == "keyoff") {
			for (auto& opt : event.options) {
				if (opt.focused) {
					std::string uservalue = std::get<std::string>(opt.value);
					dpp::interaction_response eventInstanceList(dpp::ir_autocomplete_reply);

					// For each Event Instance (Events, not Snapshots)
					for (std::map<std::string, sessionEventInstance>::iterator it = pEventInstances.begin(); it != pEventInstances.end(); ++it) {
						std::string pathOption = it->first;
						if ((pathOption.find(uservalue, 0) != std::string::npos) || (uservalue == "")) {
							eventInstanceList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
						}
					}

					bot.interaction_response_create(event.command.id, event.command.token, eventInstanceList);
				}
			}
		}

		// Stop applies to both Event, Snapshot, and File Instances
		else if (event.name == "stop") {
			for (auto& opt : event.options) {
				if (opt.focused) {
					std::string uservalue = std::get<std::string>(opt.value);
					dpp::interaction_response stoppableList(dpp::ir_autocomplete_reply);

					// For each Event Instance (Events, not Snapshots)
					for (std::map<std::string, sessionEventInstance>::iterator it = pEventInstances.begin(); it != pEventInstances.end(); ++it) {
						std::string pathOption = it->first;
						if ((pathOption.find(uservalue, 0) != std::string::npos) || (uservalue == "")) {
							stoppableList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
						}
					}
					// and For each Snapshot Instance
					for (std::map<std::string, FMOD::Studio::EventInstance*>::iterator it = pSnapshotInstances.begin(); it != pSnapshotInstances.end(); ++it) {
						std::string pathOption = it->first;
						if ((pathOption.find(uservalue, 0) != std::string::npos) || (uservalue == "")) {
							stoppableList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
						}
					}
					// and For each File instance
					for (std::map<std::string, FMOD::Channel*>::iterator it = pChannels.begin(); it != pChannels.end(); ++it) {
						std::string pathOption = it->first;
						if ((pathOption.find(uservalue, 0) != std::string::npos) || (uservalue == "")) {
							stoppableList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
						}
					}

					bot.interaction_response_create(event.command.id, event.command.token, stoppableList);
				}
			}
		}

		// Param covers both Global (in a list) and Local (dependent on the Event Instance)
		else if (event.name == "param") {
			auto& subcmd = event.options[0];
			bool isGlobal = (subcmd.name == "global") ? true : false;
			// Covering both possible subcommands in one swoop, since they're so similar
			for (auto& opt : subcmd.options) {
				// Don't autocomplete options the user isn't looking at
				if (!opt.focused) { continue; }

				// Instance Name only applies to Local parameters
				if (opt.name == "instance-name" && !isGlobal) {
					std::string uservalue = std::get<std::string>(opt.value);
					dpp::interaction_response eventInstanceList(dpp::ir_autocomplete_reply);

					// For each Event Instance (Events, not Snapshots)
					for (std::map<std::string, sessionEventInstance>::iterator it = pEventInstances.begin(); it != pEventInstances.end(); ++it) {
						std::string pathOption = it->first;
						if (pathOption.find(uservalue, 0) != 0) {
							eventInstanceList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
						}
					}
					bot.interaction_response_create(event.command.id, event.command.token, eventInstanceList);
				}
				else if (opt.name == "parameter-name") {
					std::string uservalue = std::get<std::string>(opt.value);
					dpp::interaction_response paramList(dpp::ir_autocomplete_reply);

					// If Parameter is Global, simply pull from the Global list, otherwise if Local dig deeper from that instance's list
					if (isGlobal) {
						for (unsigned int i = 0; i < globalParamNames.size(); i++) {
							std::string pathOption = globalParamNames.at(i);
							if ((pathOption.find(uservalue, 0) != std::string::npos) || (uservalue == "")) {
								paramList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
							}
						}
					}
					else {
						// Should probably find a more robust way to make sure we're getting the value of instance-name specifically
						auto& instanceNameCmdOption = subcmd.options.at(0);

						try {
							std::vector<FMOD_STUDIO_PARAMETER_DESCRIPTION>& params = pEventInstances.at(instanceNameCmdOption.name).params;
							for (unsigned int i = 0; i < params.size(); i++) {
								std::string pathOption = params.at(i).name;
								if ((pathOption.find(uservalue, 0) != std::string::npos) || (uservalue == "")) {
									paramList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
								}
							}
						}
						catch (std::out_of_range ex) {
							std::cout << "Out of Range Exception! " << ex.what() << "\n";
							std::cout << "Most likely caused by no event found in pEventInstances with the name: " << instanceNameCmdOption.name << std::endl;
						}
					}
					bot.interaction_response_create(event.command.id, event.command.token, paramList);
				}
			}
		}

		// Volume uniquely covers all Busses and VCAs from a list, similar to Stop
		else if (event.name == "volume") {
			for (auto& opt : event.options) {
				if (opt.focused) {
					std::string uservalue = std::get<std::string>(opt.value);
					dpp::interaction_response busVcaList(dpp::ir_autocomplete_reply);
					for (unsigned int i = 0; i < busPaths.size(); i++) {
						std::string pathOption = truncateBusPath(busPaths.at(i));
						if ((pathOption.find(uservalue, 0) != 0) || (uservalue == "")) {
							busVcaList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
						}
					}
					for (unsigned int i = 0; i < vcaPaths.size(); i++) {
						std::string pathOption = truncateVCAPath(vcaPaths.at(i));
						if ((pathOption.find(uservalue, 0) != 0) || (uservalue == "")) {
							busVcaList.add_autocomplete_choice(dpp::command_option_choice(pathOption, pathOption));
						}
					}
					bot.interaction_response_create(event.command.id, event.command.token, busVcaList);
				}
			}
		}
	});
	
	/* Set currentClient and tell the program we're connected */
	bot.on_voice_ready([&bot](const dpp::voice_ready_t& event) {
		std::cout << "Voice Ready" << std::endl;
		currentClient = event.voice_client;							// Get the bot's current voice channel
		currentClient->set_send_audio_type(dpp::discord_voice_client::satype_live_audio);
		isConnected = true;											// Tell the rest of the program we've connected
	});

	/* Just confirm when we're leaving Voice. Everything is stopped
	   and null'd before this is called, but it's good to have just in case.
	*/
	bot.on_voice_client_disconnect([&bot](const dpp::voice_client_disconnect_t& event) {
		std::cout << "Voice Disconnecting." << std::endl;
		isConnected = false;
	});

	/* Start the bot */
	try { bot.start(); }
	catch (dpp::exception ex) {
		std::cout << "\n\nException " << ex.code() << " when starting Bot:\n    " << ex.what() << "\n";
		std::cout << "    Please also make sure your token.txt file has your Bot Token in it, and that the Token is correct!\n";
		releaseFMOD();
		exit(ex.code());
	}

	/* Program loop */
	while (!exitRequested) {
		// Send PCM data to D++, if applicable
		if (isConnected) {
			if (pcmDataBuffer.size() > sendAudioThresh) {							// If buffer is full enough
				while (pcmDataBuffer.size() > sendAudioThresh) {					// Until minimum size we want our buffer
					currentClient->send_audio_raw((uint16_t*)pcmDataBuffer.data(),
						dpp::send_audio_raw_max_length);							// Send the buffer (method takes 11520 BYTES, so 5760 samples)
					pcmDataBuffer.erase(pcmDataBuffer.begin(),
						pcmDataBuffer.begin() + (int)(dpp::send_audio_raw_max_length * 0.5));	// Trim our main buffer of the data just sent
				}
			}
			// Todo: what if the audio ends? Or if completely silent? We should stop trying to transmit normally, right? Probably would go here.
			// Possible approach: !eventsPlaying && output is silent, fromSilence = true.
		}
		// Update FMOD processes. Just before "Sleep" which gives FMOD some time to process without main thread interference.
		pSystem->update();
		Sleep(20);
	}

	// Quitting program.
	std::cout << "Quitting program. Releasing resources...";

	// Todo: If in voice, leave chat before dying?

	// Release FMOD and the bot cluster
	releaseFMOD();
	//bot.~cluster();

	std::cout << std::endl;

	return 0;
}