#pragma once

//---UTILS---//
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
		exit(-1 * result);									//Gives the FMOD error code, but as negative because negative is bad
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