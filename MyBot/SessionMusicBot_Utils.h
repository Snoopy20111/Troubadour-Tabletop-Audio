#pragma once

//---UTILS---//

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

// Returns a random signed floating point value
float randomFloat() {
    float result = (float)(rand()) / (float)(RAND_MAX);
    bool isPositive = ((float)(rand()) > ((float)(RAND_MAX) / 2));
    if (!isPositive) { result *= -1; }
    return result;
}

// Shorthand for converting dB value into decimal (0-1). Good for setting volumes.
float dBToFloat(float input) {
    return (float)pow(10, (input * 0.05));
}

// Shorthand for converting normalized float values (0-1) to dB. Good for making volume values human-readable.
float floatTodB(float input) {
	return log10f(input) * 20.0f;
}

// Converts a floating point sample value to signed 16-bit.
// Used extensively to convert samples "stolen" by our DSP for Discord.
int16_t floatToPCM(float inSample) {
	if (inSample >= 1.0) { return 32767; }					//Ceiling
	else if (inSample <= -1.0) { return -32767;}			//Floor
	return (int16_t)roundf(inSample * 32767.0f);			//Normal conversion
	//Todo: Dithering? Probably not necessary
}

// Hard FMOD Error Check, will quit program if encountered
// Best to use in places where continuing will without a doubt cause weirder issues, corrupt data, etc.
void ERRCHECK_HARD(FMOD_RESULT result) {
	if (result != FMOD_OK) {
		std::cout << "\n\n";
		printf("FMOD Error! (%d) %s\n", result, FMOD_ErrorString(result));
		std::cout << "\n" << std::endl;
		exit(result);								//Gives the FMOD error code again
	}
}

// Soft FMOD Error Check, will simply print the result. More like a warning.
void ERRCHECK_SOFT(FMOD_RESULT result) {
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

std::set<dpp::snowflake> getAuthorizedUsers() {
	// Read from config file, grab each Discord username,
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
					std::cout << "Invalid character: "<< ch << " in users.config at line: " << currentLine << "\n";
				}
			}

			//If it's good, add to our set
			//String -> char*[] -> unsigned int -> dpp::snowflake
			if (isValid) {
				dpp::snowflake snowflake(atoi(line.c_str()));
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

bool addAuthorizedUser(dpp::snowflake newAuth, bool checkAgainstFile = false) {
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

bool addAuthorizedUser(dpp::snowflake newAuth, std::set<dpp::snowflake> userSet) {
	if (!userSet.contains(newAuth)) {
		return addAuthorizedUser(newAuth);
	}
	std::cout << "User already exists in authorized users set, not adding." << std::endl;
	return false;
}

/*bool addAuthorizedUser(unsigned int newAuth) {
	return addAuthorizedUser(dpp::snowflake(newAuth));
}

bool addAuthorizedUser(std::string newAuth) {
	return addAuthorizedUser(dpp::snowflake(atoi(newAuth.c_str())));
}*/

bool removeAuthorizedUsers() {
	// Read from config file, grab each Discord username,
	// and return them to be added as authorized users
	std::ifstream myfile("users.config");

	std::set<dpp::snowflake> authUsers;
	std::string line;

	if (myfile.is_open()) {
	}
	else {
		std::cout << "Owner.config file unable to be opened." << std::endl;
		return false;
	}
	return true;
}


// Turns paths of FMOD format ("bank:/") to the filepath it would've loaded from
std::string formatBankToFilepath(std::string bankPath, std::filesystem::path bank_dir_path) {
	bankPath.erase(0, 6);										// Remove "bank:/" at start
	bankPath.append(".bank");									// Add ".bank" at end
	bankPath = bank_dir_path.string() + "\\" + bankPath;		// Add banks directory path at start
	return bankPath;
}

// Removes "event:/Master/" from the path of a given event, for displaying lists and using as keys
std::string truncateEventPath(std::string input) {
	return input.erase(0, 14);
}

// Removes "bus:/" from the path of a given event, for displaying lists and using as keys
std::string truncateBusPath(std::string input) {
	return input.erase(0, 5);
}

// Removes "vca:/" from the path of a given event, for displaying lists and using as keys
std::string truncateVCAPath(std::string input) {
	return input.erase(0, 5);
}

// Removes "snapshot:/" from the path of a given event, for displaying lists and using as keys
std::string truncateSnapshotPath(std::string input) {
	return input.erase(0, 10);
}

// 
std::string paramMinMaxString(FMOD_STUDIO_PARAMETER_DESCRIPTION param) {
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

std::string paramAttributesString(FMOD_STUDIO_PARAMETER_DESCRIPTION param, bool includeGlobal = true) {

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

std::string paramValueString(float inputValue, FMOD_STUDIO_PARAMETER_DESCRIPTION param) {
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

// Format volume levels displayed
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

// Returns true if the Opus-sized packet has any signal at all.
// Some minor fudging included here to keep from checking
// _every_ sample, in the name of performance
bool containsSignal(std::vector<int16_t> pcmdata) {
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
														// If all checked samples were zero, odds are EXTREMELY slim of
	return false;										// the rest being silence too.
}


/* Struct to contain each Event Description and all associated parameters.
 * This can be easily adjusted and expanded to contain all parameter details,
 * such as IDs, min/max/default values, types, etc.
 */
struct sessionEventDesc {
	FMOD::Studio::EventDescription* description = nullptr;
	std::vector<FMOD_STUDIO_PARAMETER_DESCRIPTION> params;
};

/* Struct to contain each Event Instance and all associated parameters.
 * Instance points to the EventInstance itself.
 * Params is a copy of the Params vector from the related EventDescription.
 * This is potentially wasteful compared to pointers, might have Live Connect issues,
 * but is also the simplest approach to get started.
 */
struct sessionEventInstance {
	FMOD::Studio::EventInstance* instance = nullptr;
	std::vector<FMOD_STUDIO_PARAMETER_DESCRIPTION> params;
};