#include "utils.h"

//---UTILS---//

namespace trbdrUtils {
	// Gets the location of the program executable.
	// If porting away from Windows this is the first code change you should have to make.
	std::filesystem::path getExecutablePath() {
		TCHAR path[MAX_PATH];
		DWORD length = GetModuleFileName(NULL, path, MAX_PATH);
		printf("Executable location Path: %ls\n Exe Path Length: %d\n", path, length);
		return path;
	}

	// Gets the folder where the executable is contained
	std::filesystem::path getExecutableFolder() {
		std::filesystem::path exePath = getExecutablePath();
		return exePath.parent_path();
	}

	// Returns a set of paths to valid files in the soundfiles directory.
	std::set<std::filesystem::path> getSoundFiles(const std::filesystem::path& soundsDir) {
		//std::cout << "Checking directory: " << soundsDir.string() << std::endl;
		std::set<std::filesystem::path> files;
		if (std::filesystem::exists(soundsDir) && std::filesystem::is_directory(soundsDir)) {
			for (const auto& entry : std::filesystem::recursive_directory_iterator(soundsDir)) {
				if (std::filesystem::is_regular_file(entry)) {
					files.insert(entry);
				}
			}
		}
		return files;
	}

	// Shorthand for converting dB value into decimal (0-1). Good for setting volumes.
	float dBToFloat(const float& input) {
		return (float)pow(10, (input * 0.05));
	}

	// Shorthand for converting normalized float values (0-1) to dB. Good for making volume values human-readable.
	float floatTodB(const float& input) {
		return log10f(input) * 20.0f;
	}

	// Converts a floating point sample value to signed 16-bit.
	// Used extensively to convert samples "stolen" by our DSP for Discord.
	int16_t floatToPCM(const float& inSample) {
		if (inSample >= 1.0) { return 32767; }					//Ceiling
		else if (inSample <= -1.0) { return -32767; }			//Floor
		return (int16_t)roundf(inSample * 32767.0f);			//Normal conversion
		//Todo: Dithering? Probably not necessary
	}

	// Hard FMOD Error Check, will quit program if encountered
	// Best to use in places where continuing will without a doubt cause weirder issues, corrupt data, etc.
	void errorCheckFMODHard(const FMOD_RESULT& result) {
		if (result != FMOD_OK) {
			std::cout << "\n\n";
			printf("FMOD Error! (%d) %s\n", result, FMOD_ErrorString(result));
			std::cout << "\n" << std::endl;
			exit(result);								//Gives the FMOD error code again
		}
	}

	// Soft FMOD Error Check, will simply print the result. More like a warning.
	void errorCheckFMODSoft(const FMOD_RESULT& result) {
		if (result != FMOD_OK) {
			std::cout << "\n\n";
			printf("FMOD Error! (%d) %s\n", result, FMOD_ErrorString(result));
			std::cout << "\n" << std::endl;
		}
	}

	// Retrieves the Bot Token string from the token.config file
	std::string getBotToken() {
		// Read from .config (text) file and grab the token
		std::ifstream myfile("token.config");
		std::string token;
		if (myfile.is_open()) { myfile >> token; }
		else {
			std::cout << "Token config file not opened properly. Ensure it's at the correct location and isn't corrupted!";
			exit(-1);
		}
		myfile.close();
		return token;
	}

	// Returns a set of Snowflakes, representing all authorized users.
	std::set<dpp::snowflake> getAuthorizedUsers() {
		// Read from config file, grab each Snowflake,
		// and return them to be added as authorized users
		std::ifstream myfile("users.config");

		std::set<dpp::snowflake> authUsers;
		std::string line;

		if (myfile.is_open()) {
			unsigned int currentLine = 0;
			while (std::getline(myfile, line)) {
				std::cout << "Line read in: " << line << "\n";
				currentLine++;

				// Check all the characters for anything invalid
				// Currently that's only non-numbers and non-null
				char ch; bool isValid = true;
				for (unsigned int i = 0; i < line.length(); i++) {
					ch = line.at(i);
					//todo: if null terminator, not just null
					if (!std::isdigit(ch) && ch != NULL) {
						isValid = false;
						std::cout << "Invalid character: " << ch << " in users.config at line: " << currentLine << "\n";
					}
				}

				//If it's good, add to our set
				//String -> char*[] -> long long int -> dpp::snowflake
				if (isValid) {
					dpp::snowflake snowflake(atoll(line.c_str()));
					std::cout << "Accepting Snowflake: " << snowflake.str() << "\n";
					authUsers.insert(authUsers.end(), snowflake);
				}
				std::cout << std::endl;
			}
			myfile.close();
		}
		else {
			std::cout << "Owner.config file unable to be opened for reading." << std::endl;
			//exit(-1);
		}
		return authUsers;
	}

	// Adds an authorized user, with the option to check against the users.config file and against the Bot Owner.
	bool addAuthorizedUser(const dpp::snowflake& newAuth, const bool& checkAgainstFile, const dpp::snowflake& botOwner) {
		if (botOwner == newAuth) {
			std::cout << "Cannot add botOwner, definitely already exists in the list." << std::endl;
			return false;
		}

		if (checkAgainstFile) {
			std::set<dpp::snowflake> authUsers = getAuthorizedUsers();
			if (authUsers.contains(newAuth)) {
				std::cout << "User already exists in users.config, not adding." << std::endl;
				return false;
			}
		}

		std::ofstream myfile("users.config");
		if (myfile.is_open()) {
			myfile << newAuth.str() << std::endl;
			myfile.close();
		}
		else {
			std::cout << "Owner.config file unable to be opened for writing to." << std::endl;
			return false;
		}
		return true;
	}

	// Adds an authorized user, checking against the currently Set before checking or adding to the file.
	bool addAuthorizedUser(const dpp::snowflake& newAuth, const std::set<dpp::snowflake>& userSet, const dpp::snowflake& botOwner) {
		if (!userSet.contains(newAuth)) {
			return addAuthorizedUser(newAuth, false, botOwner);
		}
		std::cout << "User already exists in authorized users set, not adding." << std::endl;
		return false;
	}

	// Removes an authorized user.
	bool removeAuthorizedUser(const dpp::snowflake& authToRemove, std::set<dpp::snowflake>& userSet, const dpp::snowflake& botOwner) {
		if (!userSet.contains(authToRemove)) {
			std::cout << "Given snowflake doesn't exist in loaded Authorized User set, cannot remove. ";
			std::cout << "If this user snowflake was added manually to the file, please use the /user list command or restart the bot. ";
			std::cout << "Will still attempt to remove from file." << std::endl;
			return false;
		}
		else if (botOwner == authToRemove) {
			std::cout << "Cannot remove the bot owner, as a safety measure. Stop trying to break things." << std::endl;
			return false;
		}
		else {
			std::cout << "Removing user from loaded set." << std::endl;
			userSet.erase(authToRemove);
		}

		std::set<dpp::snowflake> authUsers = getAuthorizedUsers();

		if (authUsers.contains(authToRemove)) {
			std::ofstream myfile("users.config.temp");
			if (myfile.is_open()) {
				for (auto& user : authUsers) {
					myfile << user.str() << std::endl;
				}
				myfile.close();

				// Replace original file with the modified one
				// First try to delete user.config.old, in case anything already exists there
				remove("users.config.old");

				if (rename("users.config", "user.config.old") != 0) {
					std::cout << "Error renaming original file" << std::endl;
					return false;
				}
				if (rename("users.config.temp", "users.config") != 0) {
					std::cout << "Error renaming temp file to users.config!" << std::endl;
					return false;
				}
				std::cout << "Successfully removed user from file and re-saved." << std::endl;
				return true;
			}
			else {
				std::cout << "Owner.config file unable to be opened for modification." << std::endl;
				return false;
			}
		}
		else {
			std::cout << "Given Snowflake not present in Owners.config file. File will not be modified." << std::endl;
		}

		return true;
	}

	// Returns string with name/path of a sound file, relative to the soundfiles directory.
	std::string formatPathToSoundfile(std::filesystem::path soundPath, const std::filesystem::path& soundsDirPath) {
		soundPath.replace_extension("");
		std::string soundPathStr(soundPath.string());
		std::string soundsDirPathStr(soundsDirPath.string() + "\\");

		//std::cout << "  soundPathStr: " << soundPathStr << "\n";
		//std::cout << "  soundsDirPathStr: " << soundsDirPathStr << "\n";

		std::size_t dirLocation = soundPathStr.find(soundsDirPathStr);
		if ((dirLocation != std::string::npos) && (soundPathStr > soundsDirPathStr)) {
			soundPathStr.erase(dirLocation, soundsDirPathStr.length());
		}
		else {
			std::cout << "Error when formatting soundfile name: couldn't find soundsDirPathStr in soundPathStr!" << std::endl;
		}
		return soundPathStr;
	}

	// Returns path of FMOD format ("bank:/") as the filepath it would've loaded from.
	std::string formatBankToFilepath(std::string bankPath, const std::filesystem::path& banksDirPath) {
		bankPath.erase(0, 6);										// Remove "bank:/" at start
		bankPath.append(".bank");									// Add ".bank" at end
		bankPath = banksDirPath.string() + "\\" + bankPath;		// Add banks directory path at start

		// Find and replace forward slashes
		// Windows only. May need replacement or removal for other platforms.
		size_t pos = bankPath.find("/");
		while (pos != std::string::npos) {
			bankPath.replace(pos, 1, "\\");
			pos = bankPath.find("/", pos + 1);
		}

		return bankPath;
	}

	// Returns path of given event without the prefix "event:/Master/", for displaying lists and using as keys.
	std::string truncateEventPath(std::string input) {
		return input.erase(0, 14);
	}

	// Returns path of given event without the prefix "bus:/", for displaying lists and using as keys.
	std::string truncateBusPath(std::string input) {
		return input.erase(0, 5);
	}

	// Returns path of given event without the prefix "vca:/", for displaying lists and using as keys.
	std::string truncateVCAPath(std::string input) {
		return input.erase(0, 5);
	}

	// Returns path of given event without the prefix "snapshot:/", for displaying lists and using as keys.
	std::string truncateSnapshotPath(std::string input) {
		return input.erase(0, 10);
	}

	// Returns a string describing the min/max values of a parameter, with some rounding to look nice.
	std::string paramMinMaxString(const FMOD_STUDIO_PARAMETER_DESCRIPTION& param) {
		std::string paramName = param.name;
		std::string paramMinVal; float paramMinVal_f = param.minimum;
		std::string paramMaxVal; float paramMaxVal_f = param.maximum;

		if ((param.flags >> 3) % 2 == 1) {									// If Discrete or labeled, show no decimal places
			int paramMinVal_i = (int)roundf(paramMinVal_f);
			int paramMaxVal_i = (int)roundf(paramMaxVal_f);
			paramMinVal = std::to_string(paramMinVal_i);
			paramMaxVal = std::to_string(paramMaxVal_i);
		}
		else {
			if (abs(paramMinVal_f - roundf(paramMinVal_f)) < 0.005f) {		// If super close to int, show 1 decimal place
				paramMinVal_f = roundf(paramMinVal_f * 10) * 0.1f;			// Relies on assumption string has 6 digits after decimal
				paramMinVal = std::to_string(paramMinVal_f);
				paramMinVal.resize(paramMinVal.size() - 5);
			}
			else {															// Otherwise show 2 decimal places
				paramMinVal_f = roundf(paramMinVal_f * 100) * 0.01f;			// Relies on assumption string has 6 digits after decimal
				paramMinVal = std::to_string(paramMinVal_f);
				paramMinVal.resize(paramMinVal.size() - 4);
			}

			if (abs(paramMaxVal_f - roundf(paramMaxVal_f)) < 0.005f) {		// Handles max value rounding in the same way
				paramMaxVal_f = roundf(paramMaxVal_f * 10) * 0.1f;
				paramMaxVal = std::to_string(paramMaxVal_f);
				paramMaxVal.resize(paramMaxVal.size() - 5);
			}
			else {
				paramMaxVal_f = roundf(paramMaxVal_f * 100) * 0.01f;
				paramMaxVal = std::to_string(paramMaxVal_f);
				paramMaxVal.resize(paramMaxVal.size() - 4);
			}
		}
		return "[ " + paramMinVal + " - " + paramMaxVal + " ]";
	}

	// Returns a string describing the various attributes of a parameter.
	std::string paramAttributesString(const FMOD_STUDIO_PARAMETER_DESCRIPTION& param, const bool& includeGlobal) {

		std::string output;

		if ((param.flags >> 2) % 2 == 1 && includeGlobal) {			// Global
			output.append(" (Global)");
		}
		if (((param.flags >> 4) % 2 == 1) &&		// Labeled, like "enum"
			((param.flags >> 3) % 2 == 1)) {		// Also triggers Discrete flag, but
			output.append(" (Labeled)");			// we don't want that to show.
		}
		else if ((param.flags >> 3) % 2 == 1) {		// Discrete, like "int"
			output.append(" (Discrete)");
		}
		if ((param.flags % 2) == 1) {				// Read Only
			output.append(" (Read-Only)");
		}
		output.append("\n");

		return output;
	}

	// Returns a string describing a parameter's value, with some rounding to look nice.
	std::string paramValueString(float inputValue, const FMOD_STUDIO_PARAMETER_DESCRIPTION& param) {
		std::string output;
		//Some yucky string cleanup to have a readable version of the value
		if ((param.flags >> 3) % 2 == 1) {				// If Discrete or labeled, show no decimal places
			int inputValue_i = (int)roundf(inputValue);
			output = std::to_string(inputValue_i);
		}
		else {
			if (abs(inputValue - roundf(inputValue)) < 0.005f) {	// If super close to int, show 1 decimal place
				inputValue = roundf(inputValue * 10) * 0.1f;		// Relies on assumption of 6 decimal places shown
				output = std::to_string(inputValue);
				output.resize(output.size() - 5);
			}
			else {													// Else, show 2 decimal places
				inputValue = roundf(inputValue * 100) * 0.01f;
				output = std::to_string(inputValue);
				output.resize(output.size() - 4);
			}
		}
		return output;
	}

	// Returns a decibel volume level as a string (with the dB units), for display.
	std::string volumeString(float inputValue) {
		std::string output;

		if (abs(inputValue - roundf(inputValue)) < 0.005f) {	// If super close to int, show 1 decimal place
			inputValue = roundf(inputValue * 10) * 0.1f;		// Relies on assumption of 6 decimal places shown
			output = std::to_string(inputValue);
			output.resize(output.size() - 5);
		}
		else {													// Else, show 2 decimal places
			inputValue = roundf(inputValue * 100) * 0.01f;
			output = std::to_string(inputValue);
			output.resize(output.size() - 4);
		}

		output.append(" dB");		// Full Scale? I think?

		return output;
	}

	// Returns a random signed floating point value
	/*float randomFloat() {
		float result = (float)(rand()) / (float)(RAND_MAX);
		bool isPositive = ((float)(rand()) > ((float)(RAND_MAX) / 2));
		if (!isPositive) { result *= -1; }
		return result;
	}*/

	// Returns true if the Opus - sized packet has any signal at all.
	/*bool containsSignal(std::vector<int16_t> pcmdata) {
		int limit = (int)(dpp::send_audio_raw_max_length * 0.5);
		for (int i = 0; i < limit; i += 16) {				// Checks every 16th sample of 5760
			//std::cout << "Limit: " << limit << " Index: " << i << " Vector Size: " << pcmdata.size() << " Matches with " << pcmdata.at(i) << std::endl;
			if (pcmdata[i] != 0) {							// If there's any signal at all,
				return true;								// this is likely to return _very_ quickly
			}
		}
		for (int i = limit - 16; i < limit; i++) {			// Double checking for the last few samples, just in case.
			if (pcmdata[i] != 0) {							// Prevents transients from being chopped off in worst-case.
				return true;
			}
		}
		// If all checked samples were zero, odds are EXTREMELY slim of the rest being silence.
		return false;
	}*/
}