#pragma once

//---UTILS---//

// Gets the location of the program executable.
// If porting away from Windows this is the first (maybe last?) code change you should have to make.
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
    return (float)pow(10, (input / 20));
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
		printf("FMOD Error! (%d) %s\n", result, FMOD_ErrorString(result));
		exit(result);								//Gives the FMOD error code again
	}
}

// Soft FMOD Error Check, will simply print the result. More like a warning.
void ERRCHECK_SOFT(FMOD_RESULT result) {
	if (result != FMOD_OK) {
		printf("FMOD Error! (%d) %s\n", result, FMOD_ErrorString(result));
	}
}

// Retrieves the Bot Token string from the token.config file
std::string getBotToken()
{
	//read from .config (text) file and grab the token
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