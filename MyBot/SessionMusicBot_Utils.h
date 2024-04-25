#pragma once

// UTILS //
std::filesystem::path getExecutablePath() {
    TCHAR path[MAX_PATH];
    DWORD length = GetModuleFileName(NULL, path, MAX_PATH);
    printf("Executable location Path: %ls\nLength: %d\n", path, length);
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