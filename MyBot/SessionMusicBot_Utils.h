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

std::filesystem::path getExecutableFolder() {
    std::filesystem::path exePath = getExecutablePath();
    return exePath.parent_path();
}

float randomFloat() {
    float result = (float)(rand()) / (float)(RAND_MAX);
    bool isPositive = ((float)(rand()) > ((float)(RAND_MAX) / 2));
    if (!isPositive) { result *= -1; }
    return result;
}

float dBToFloat(float input) {
    return (float)pow(10, (input / 20));
}

int16_t floatToPCM(float inSample) {
	if (inSample >= 1.0) { return 32767; }					//Ceiling
	else if (inSample <= -1.0) { return -32767;}			//Floor
	return (int16_t)roundf(inSample * 32767.0f);			//Normal conversion
	//Todo: Dithering?
}

void ERRCHECK(FMOD_RESULT result) {
	if (result != FMOD_OK) {
		printf("FMOD Error! (%d) %s\n", result, FMOD_ErrorString(result));
		exit(result);									//Gives the FMOD error code
	}
}

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

std::string formatBankToFilepath(std::string bankPath, std::filesystem::path bank_dir_path) {
	bankPath.erase(0, 6);										// Remove "bank:/" at start
	bankPath.append(".bank");									// Add ".bank" at end
	bankPath = bank_dir_path.string() + "\\" + bankPath;		// Add banks directory path at start
	return bankPath;
}

std::string truncateEventPath(std::string input) {
	std::string truncatedPath = input;
	truncatedPath.erase(0, 14);
	return truncatedPath;
}