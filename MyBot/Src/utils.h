#pragma once

#include "fmod.hpp"
#include <dpp/dpp.h>
#include "fmod_studio.hpp"
#include "fmod_errors.h"
#include <filesystem>

//---UTILS---//

namespace utils {
	// Gets the location of the program executable.
	std::filesystem::path getExecutablePath();

	// Gets the folder where the executable is contained.
	std::filesystem::path getExecutableFolder();

	// Returns a random signed floating point value.
	float randomFloat();

	// Shorthand for converting dB value into decimal (0-1). Good for setting volumes.
	float dBToFloat(const float& input);

	// Shorthand for converting normalized float values (0-1) to dB. Good for making volume values human-readable.
	float floatTodB(const float& input);

	// Converts a floating point sample value to signed 16-bit.
	int16_t floatToPCM(const float& inSample);

	// Hard FMOD Error Check, will quit program if encountered.
	// Best to use in places where continuing will without a doubt cause weirder issues, corrupt data, etc.
	void errorCheckFMODHard(const FMOD_RESULT& result);

	// Soft FMOD Error Check, will simply print the result. More like a warning.
	void errorCheckFMODSoft(const FMOD_RESULT& result);

	// Retrieves the Bot Token string from the token.config file.
	std::string getBotToken();

	// Returns a set of Snowflakes, representing all authorized users.
	std::set<dpp::snowflake> getAuthorizedUsers();

	// Adds an authorized user, with the option to check against the users.config file and against the Bot Owner.
	bool addAuthorizedUser (const dpp::snowflake& newAuth, const bool& checkAgainstFile = false, const dpp::snowflake& botOwner = NULL);

	// Adds an authorized user, checking against the currently Set before checking or adding to the file.
	bool addAuthorizedUser(const dpp::snowflake& newAuth, const std::set<dpp::snowflake>& userSet, const dpp::snowflake& botOwner = NULL);

	// Removes an authorized user.
	bool removeAuthorizedUser(const dpp::snowflake& authToRemove, std::set<dpp::snowflake>& userSet, const dpp::snowflake& botOwner = NULL);

	// Returns path of FMOD format ("bank:/") as the filepath it would've loaded from
	std::string formatBankToFilepath(std::string bankPath, const std::filesystem::path& bank_dir_path);

	// Returns path of given event without the prefix "event:/Master/", for displaying lists and using as keys
	std::string truncateEventPath(std::string input);

	// Returns path of given event without the prefix "bus:/", for displaying lists and using as keys
	std::string truncateBusPath(std::string input);

	// Returns path of given event without the prefix "vca:/", for displaying lists and using as keys
	std::string truncateVCAPath(std::string input);

	// Returns path of given event without the prefix "snapshot:/", for displaying lists and using as keys
	std::string truncateSnapshotPath(std::string input);

	// Returns a string describing the min/max values of a parameter, with some rounding to look nice.
	std::string paramMinMaxString(const FMOD_STUDIO_PARAMETER_DESCRIPTION& param);

	// Returns a string describing the various attributes of a parameter.
	std::string paramAttributesString(const FMOD_STUDIO_PARAMETER_DESCRIPTION& param, const bool& includeGlobal = true);

	// Returns a string describing a parameter's value, with some rounding to look nice.
	std::string paramValueString(float inputValue, const FMOD_STUDIO_PARAMETER_DESCRIPTION& param);

	// Returns a decibel volume level as a string (with the dB units), for display.
	std::string volumeString(float inputValue);

	// Returns true if the Opus - sized packet has any signal at all.
	/*bool containsSignal(std::vector<int16_t> pcmdata);*/


	// Struct to contain each Event Description and all associated parameters.
	struct sessionEventDesc {
		FMOD::Studio::EventDescription* description = nullptr;
		std::vector<FMOD_STUDIO_PARAMETER_DESCRIPTION> params;
	};

	// Struct to contain each Event Instance and all associated parameters.
	struct sessionEventInstance {
		FMOD::Studio::EventInstance* instance = nullptr;
		std::vector<FMOD_STUDIO_PARAMETER_DESCRIPTION> params;
	};
}