// FindFiles.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <string>
#include <regex>
#include <chrono>
#include <iomanip>
#include <vector>
#include <optional>
#include <windows.h>
#include <sstream>
#include <tchar.h>
#include <algorithm> // For std::sort

// Function to get the console width
int getConsoleWidth() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int consoleWidth = 79; // Default fallback width if we can't detect actual width
    
    // Get the console handle
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE) {
        // Get the console buffer info
        if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
            // Calculate width from the buffer info
            consoleWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            // Subtract 1 to avoid automatic line wrapping
            consoleWidth -= 1;
            
            // Set a minimum reasonable width
            if (consoleWidth < 50) {
                consoleWidth = 50;
            }
        }
    }
    
    return consoleWidth;
}

struct FileInfo {
    std::wstring path;
    std::chrono::system_clock::time_point creationTime;
    std::chrono::system_clock::time_point modificationTime;
    uintmax_t size;
    
    // Standard comparison for sorting
    bool operator<(const FileInfo& other) const {
        return path < other.path;
    }
    
    bool operator==(const FileInfo& other) const {
        return path == other.path;
    }
};

// Sort options enum
enum class SortField {
    Path,
    Name,
    Size,
    CreationDate,
    ModificationDate
};

struct SortOption {
    SortField field;
    bool ascending;
    
    SortOption(SortField f, bool asc) : field(f), ascending(asc) {}
};

// Function to parse sort options
std::vector<SortOption> parseSortOptions(const std::wstring& sortStr) {
    std::vector<SortOption> options;
    bool ascending = true;
    
    for (wchar_t c : sortStr) {
        if (c == L'-') {
            ascending = false;
            continue;
        }
        
        SortField field;
        switch (c) {
            case L'p': field = SortField::Path; break;
            case L'n': field = SortField::Name; break;
            case L's': field = SortField::Size; break;
            case L'c': field = SortField::CreationDate; break;
            case L'm': field = SortField::ModificationDate; break;
            default: continue; // Skip unknown characters
        }
        
        options.push_back(SortOption(field, ascending));
        ascending = true; // Reset for the next field
    }
    
    // If no valid options provided, default to path ascending
    if (options.empty()) {
        options.push_back(SortOption(SortField::Path, true));
    }
    
    return options;
}

// Function to sort files based on sort options
void sortFiles(std::vector<FileInfo>& files, const std::vector<SortOption>& sortOptions) {
    std::sort(files.begin(), files.end(), [&sortOptions](const FileInfo& a, const FileInfo& b) {
        for (const auto& option : sortOptions) {
            bool result = false;
            bool equal = false;
            
            switch (option.field) {
                case SortField::Path:
                    result = a.path < b.path;
                    equal = a.path == b.path;
                    break;
                case SortField::Name: {
                    // Extract filename from path (after last backslash)
                    size_t aLastSlash = a.path.find_last_of(L'\\');
                    size_t bLastSlash = b.path.find_last_of(L'\\');
                    std::wstring aName = (aLastSlash != std::wstring::npos) ? 
                                        a.path.substr(aLastSlash + 1) : a.path;
                    std::wstring bName = (bLastSlash != std::wstring::npos) ? 
                                        b.path.substr(bLastSlash + 1) : b.path;
                    result = aName < bName;
                    equal = aName == bName;
                    break;
                }
                case SortField::Size:
                    result = a.size < b.size;
                    equal = a.size == b.size;
                    break;
                case SortField::CreationDate:
                    result = a.creationTime < b.creationTime;
                    equal = a.creationTime == b.creationTime;
                    break;
                case SortField::ModificationDate:
                    result = a.modificationTime < b.modificationTime;
                    equal = a.modificationTime == b.modificationTime;
                    break;
            }
            
            if (!equal) {
                return option.ascending ? result : !result;
            }
            // If equal, continue to next sort option
        }
        
        // If all criteria are equal, default to path
        return a.path < b.path;
    });
}

class FileFinder {
public:
    static std::vector<FileInfo> findFiles(
        const std::wstring& directory,
        const std::wstring& pattern,
        bool useRegex = false,
        bool shallow = false,
        bool debug = false
    ) {
        std::vector<FileInfo> results;
        std::wregex regexPattern;

        // Debug output for inputs
        if (debug) {
            std::cout << "Directory: ";
            for (wchar_t c : directory) {
                std::cout << (char)c;
            }
            std::cout << std::endl;
            
            std::cout << "Pattern: ";
            for (wchar_t c : pattern) {
                std::cout << (char)c;
            }
            std::cout << std::endl;
        }

        try {
            if (useRegex) {
                regexPattern = std::wregex(pattern, std::regex_constants::icase);
            } else {
                // Convert DOS wildcard to regex
                std::wstring regexStr = dosPatternToRegex(pattern);
                regexPattern = std::wregex(regexStr, std::regex_constants::icase);
            }
        }
        catch (const std::regex_error& e) {
            std::cerr << "Invalid regex pattern: " << e.what() << std::endl;
            return results;
        }

        // Create a search path by appending \* to the directory
        std::wstring searchPath = directory;
        if (!searchPath.empty() && searchPath.back() != L'\\') {
            searchPath += L'\\';
        }
        searchPath += L'*';

        // Debug output for search path
        if (debug) {
            std::cout << "Search path: ";
            for (wchar_t c : searchPath) {
                std::cout << (char)c;
            }
            std::cout << std::endl;
        }

        // Find first file
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);

        if (hFind == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            std::cerr << "Error searching directory: " << error;
            
            // Print a more descriptive error message
            LPVOID lpMsgBuf;
            FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | 
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPWSTR)&lpMsgBuf,
                0, NULL);
            
            std::string errorMsg;
            wchar_t* wMsg = (wchar_t*)lpMsgBuf;
            for (int i = 0; wMsg[i] != L'\0'; i++) {
                errorMsg += (char)wMsg[i];
            }
            
            std::cerr << " - " << errorMsg;
            
            // Add directory name to error message
            std::cerr << " Directory: ";
            for (wchar_t c : directory) {
                std::cerr << (char)c;
            }
            std::cerr << std::endl;
            
            LocalFree(lpMsgBuf);
            return results;
        }

        do {
            // Skip . and .. directories
            if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
                continue;
            }

            std::wstring fullPath = directory;
            if (!fullPath.empty() && fullPath.back() != L'\\') {
                fullPath += L'\\';
            }
            fullPath += findData.cFileName;

            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                // Process subdirectories if not in shallow mode
                if (!shallow) {
                    std::vector<FileInfo> subDirResults = findFiles(fullPath, pattern, useRegex, shallow, debug);
                    results.insert(results.end(), subDirResults.begin(), subDirResults.end());
                }
            } else {
                // Process regular file if it matches the pattern
                std::wstring filename = findData.cFileName;
                if (std::regex_match(filename, regexPattern)) {
                    FileInfo info;
                    info.path = fullPath;
                    
                    // Get file times from WIN32_FIND_DATA
                    FILETIME ftCreate = findData.ftCreationTime;
                    FILETIME ftWrite = findData.ftLastWriteTime;
                    
                    SYSTEMTIME stCreate;
                    FileTimeToSystemTime(&ftCreate, &stCreate);
                    tm tmCreate = {};
                    tmCreate.tm_year = stCreate.wYear - 1900;
                    tmCreate.tm_mon = stCreate.wMonth - 1;
                    tmCreate.tm_mday = stCreate.wDay;
                    tmCreate.tm_hour = stCreate.wHour;
                    tmCreate.tm_min = stCreate.wMinute;
                    tmCreate.tm_sec = stCreate.wSecond;
                    info.creationTime = std::chrono::system_clock::from_time_t(
                        _mkgmtime(&tmCreate));
                    
                    SYSTEMTIME stWrite;
                    FileTimeToSystemTime(&ftWrite, &stWrite);
                    tm tmWrite = {};
                    tmWrite.tm_year = stWrite.wYear - 1900;
                    tmWrite.tm_mon = stWrite.wMonth - 1;
                    tmWrite.tm_mday = stWrite.wDay;
                    tmWrite.tm_hour = stWrite.wHour;
                    tmWrite.tm_min = stWrite.wMinute;
                    tmWrite.tm_sec = stWrite.wSecond;
                    info.modificationTime = std::chrono::system_clock::from_time_t(
                        _mkgmtime(&tmWrite));
                    
                    // Get file size
                    ULARGE_INTEGER fileSize;
                    fileSize.LowPart = findData.nFileSizeLow;
                    fileSize.HighPart = findData.nFileSizeHigh;
                    info.size = fileSize.QuadPart;
                    
                    results.push_back(info);
                }
            }
        } while (FindNextFileW(hFind, &findData));

        FindClose(hFind);
        return results;
    }

private:
    static std::wstring dosPatternToRegex(const std::wstring& pattern) {
        std::wstring result = pattern;
        // Escape special regex characters
        for (size_t i = 0; i < result.length(); ++i) {
            if (result[i] == L'*' || result[i] == L'?' || result[i] == L'.' ||
                result[i] == L'[' || result[i] == L']' || result[i] == L'(' ||
                result[i] == L')' || result[i] == L'{' || result[i] == L'}' ||
                result[i] == L'|' || result[i] == L'+' || result[i] == L'^' ||
                result[i] == L'$' || result[i] == L'\\') {
                result.insert(i, L"\\");
                ++i;
            }
        }
        // Convert DOS wildcards to regex
        size_t pos = 0;
        while ((pos = result.find(L"\\*", pos)) != std::wstring::npos) {
            result.replace(pos, 2, L".*");
            pos += 2;
        }
        pos = 0;
        while ((pos = result.find(L"\\?", pos)) != std::wstring::npos) {
            result.replace(pos, 2, L".");
            pos += 1;
        }
        return L"^" + result + L"$";
    }
};

// Execute a command with substituted parameters
bool executeCommand(const std::wstring& commandTemplate, const std::wstring& filePath) {
    // Get directory, filename
    std::wstring directory;
    std::wstring filename;
    
    // Extract directory and filename from path
    size_t lastSlash = filePath.find_last_of(L'\\');
    if (lastSlash != std::wstring::npos) {
        directory = filePath.substr(0, lastSlash);
        filename = filePath.substr(lastSlash + 1);
    } else {
        directory = L".";
        filename = filePath;
    }
    
    // Create a copy of the command template for substitution
    std::wstring command = commandTemplate;
    
    // Substitute %d with directory
    size_t pos = 0;
    while ((pos = command.find(L"%d", pos)) != std::wstring::npos) {
        command.replace(pos, 2, directory);
        pos += directory.length();
    }
    
    // Substitute %n with filename
    pos = 0;
    while ((pos = command.find(L"%n", pos)) != std::wstring::npos) {
        command.replace(pos, 2, filename);
        pos += filename.length();
    }
    
    // Substitute %f with full path
    pos = 0;
    while ((pos = command.find(L"%f", pos)) != std::wstring::npos) {
        command.replace(pos, 2, filePath);
        pos += filePath.length();
    }
    
    // Create process information structures
    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi;
    
    // Create a modifiable copy of the command for CreateProcessW
    wchar_t* cmdLine = _wcsdup(command.c_str());
    if (!cmdLine) {
        std::cerr << "Memory allocation failed" << std::endl;
        return false;
    }
    
    // Execute the command
    bool success = CreateProcessW(
        NULL,      // No module name (use command line)
        cmdLine,   // Command line
        NULL,      // Process handle not inheritable
        NULL,      // Thread handle not inheritable
        FALSE,     // Set handle inheritance to FALSE
        0,         // No creation flags
        NULL,      // Use parent's environment block
        NULL,      // Use parent's starting directory
        &si,       // Pointer to STARTUPINFO structure
        &pi        // Pointer to PROCESS_INFORMATION structure
    );
    
    // Free the command line duplicate
    free(cmdLine);
    
    if (success) {
        // Wait for the process to finish
        WaitForSingleObject(pi.hProcess, INFINITE);
        
        // Close process and thread handles
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        std::cerr << "Command execution failed: " << GetLastError() << std::endl;
    }
    
    return success;
}

void printFileInfo(const FileInfo& info, bool singleTabMode = false, bool bareMode = false) {
    // If bare mode, only print the path
    if (bareMode) {
        // Convert wstring to string for path
        std::string path;
        for (wchar_t c : info.path) {
            path += static_cast<char>(c);
        }
        std::cout << path << std::endl;
        return;
    }

    // Format creation time
    auto formatTime = [singleTabMode](const auto& time) {
        auto tt = std::chrono::system_clock::to_time_t(time);
        struct tm timeinfo;
        localtime_s(&timeinfo, &tt);
        char buffer[20];
        if (singleTabMode) {
            // For tab mode, include seconds
            strftime(buffer, 20, "%Y-%m-%d %H:%M:%S", &timeinfo);
        } else {
            strftime(buffer, 20, "%Y-%m-%d %H:%M", &timeinfo);
        }
        return std::string(buffer);
    };

    // Get formatted times
    std::string createdTime = formatTime(info.creationTime);
    std::string modifiedTime = formatTime(info.modificationTime);

    // Convert wstring to string for path
    std::string path;
    for (wchar_t c : info.path) {
        path += static_cast<char>(c);
    }

    if (singleTabMode) {
        // Single tab mode - just one tab between columns with full data
        // Use original bytes (not KB) for size
        std::cout << path << '\t' << info.size << '\t' << createdTime << '\t' << modifiedTime << std::endl;
    } else {
        // Convert size to kilobytes, rounded up
        uintmax_t sizeKB = (info.size + 1023) / 1024; // Round up by adding 1023 before division
        std::string sizeStr = std::to_string(sizeKB);
        
        // Get the console width dynamically
        const int totalWidth = getConsoleWidth();
        const int sizeWidth = 10;
        const int createdWidth = 16;
        const int modifiedWidth = 16;
        const int spacing = 2;
        // Calculate path width to use all remaining space
        const int pathWidth = totalWidth - sizeWidth - createdWidth - modifiedWidth - (spacing * 3);
        
        // Print path (left justified, truncated if needed)
        if (path.length() > pathWidth) {
            std::cout << std::left << std::setw(pathWidth) << (path.substr(0, pathWidth - 3) + "...");
        } else {
            // std::left with std::setw(pathWidth) already pads with spaces to reach the width
            std::cout << std::left << std::setw(pathWidth) << path;
        }
        
        // Print remaining columns with proper spacing and right justification
        std::cout << std::string(spacing, ' ') 
                << std::right << std::setw(sizeWidth) << sizeStr
                << std::string(spacing, ' ') 
                << std::right << std::setw(createdWidth) << createdTime
                << std::string(spacing, ' ') 
                << std::right << std::setw(modifiedWidth) << modifiedTime
                << std::endl;
    }
}

void printColumnHeaders(bool singleTabMode = false) {
    // Get the console width dynamically
    const int totalWidth = getConsoleWidth();
    const int sizeWidth = 10;
    const int createdWidth = 16;
    const int modifiedWidth = 16;
    const int spacing = 2;
    // Calculate path width to use all remaining space
    const int pathWidth = totalWidth - sizeWidth - createdWidth - modifiedWidth - (spacing * 3);
    
    if (singleTabMode) {
        // Tab-separated headers for tab mode
        std::cout << "Path\tSize\tCreated Date\tModified Date" << std::endl;
    } else {
        // Print column headers with proper alignment
        std::cout << std::left << std::setw(pathWidth) << "Path"
                 << std::string(spacing, ' ') 
                 << std::right << std::setw(sizeWidth) << "Size (KB)"
                 << std::string(spacing, ' ') 
                 << std::right << std::setw(createdWidth) << "Created"
                 << std::string(spacing, ' ') 
                 << std::right << std::setw(modifiedWidth) << "Modified"
                 << std::endl;
        
        // Print separator line - exact same length as each column header
        std::cout << std::left << std::setw(pathWidth) << std::string(pathWidth, '-')
                 << std::string(spacing, ' ')
                 << std::right << std::setw(sizeWidth) << std::string(sizeWidth, '-')
                 << std::string(spacing, ' ')
                 << std::right << std::setw(createdWidth) << std::string(createdWidth, '-')
                 << std::string(spacing, ' ')
                 << std::right << std::setw(modifiedWidth) << std::string(modifiedWidth, '-')
                 << std::endl;
    }
}

void printUsage(const wchar_t* programName) {
    std::cout << "Usage: FindFiles.exe <directory> <pattern> [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -r, --regex          Treat pattern as regex instead of DOS wildcard" << std::endl;
    std::cout << "  -s, --shallow        Shallow search (do not recurse into subdirectories)" << std::endl;
    std::cout << "  -x, --execute \"cmd\"  Execute command on each found file" << std::endl;
    std::cout << "                       %d = directory, %n = filename, %f = full path" << std::endl;
    std::cout << "  -d, --debug          Show detailed debug information during the search" << std::endl;
    std::cout << "  -t, --tab            Use single tab between columns (better for parsing)" << std::endl;
    std::cout << "  -c, --concise        Display results without headers or summary" << std::endl;
    std::cout << "  -b, --bare           Display only file paths (implies --concise)" << std::endl;
    std::cout << "  --sort <order>       Sort results by specified criteria" << std::endl;
    std::cout << "                       p=path, n=name, s=size, c=created date, m=modified date" << std::endl;
    std::cout << "                       Prefix with - for descending order (e.g., -np for descending name)" << std::endl;
    std::cout << "  -h, --help           Display this help message" << std::endl;
}

// Helper function to check if a string equals any of multiple options
bool strEqualsAny(const std::wstring& str, std::initializer_list<const wchar_t*> options) {
    for (const auto& option : options) {
        if (str == option) {
            return true;
        }
    }
    return false;
}

bool matchesPattern(const std::wstring& filename, const std::wstring& pattern) {
    // If pattern is a simple wildcard like *.cpp, convert to regex
    std::wstring regexPattern = pattern;
    
    // Replace . with \. (escape dots)
    size_t pos = 0;
    while((pos = regexPattern.find(L'.', pos)) != std::wstring::npos) {
        regexPattern.replace(pos, 1, L"\\.");
        pos += 2;
    }
    
    // Replace * with .* (any characters)
    pos = 0;
    while((pos = regexPattern.find(L'*', pos)) != std::wstring::npos) {
        regexPattern.replace(pos, 1, L".*");
        pos += 2;
    }
    
    // Replace ? with . (any character)
    pos = 0;
    while((pos = regexPattern.find(L'?', pos)) != std::wstring::npos) {
        regexPattern.replace(pos, 1, L".");
        pos += 1;
    }
    
    // Make the pattern match the entire filename
    regexPattern = L"^" + regexPattern + L"$";
    
    try {
        std::wregex regex(regexPattern, std::regex_constants::icase);
        return std::regex_match(filename, regex);
    }
    catch (const std::regex_error& e) {
        std::cout << "Invalid pattern: " << e.what() << std::endl;
        return false;
    }
}

int main(int argc, char* argv[]) {
    // Convert arguments to wide strings for easier processing
    std::vector<std::wstring> args;
    for (int i = 0; i < argc; i++) {
        std::string arg = argv[i];
        args.push_back(std::wstring(arg.begin(), arg.end()));
    }

    // Default values
    std::wstring directory;
    std::wstring pattern = L"*";
    bool useRegex = false;
    bool shallow = false;
    bool debug = false;
    bool singleTabMode = false;
    bool conciseMode = false;
    bool bareMode = false;
    std::optional<std::wstring> command;
    std::optional<std::wstring> sortOption;

    // Check for help flag
    if (argc < 2 || strEqualsAny(args[1], {L"-h", L"--help", L"/?"})) {
        printUsage(args[0].c_str());
        return 0;
    }

    // Parse directory (first non-option argument)
    bool foundDirectory = false;
    for (size_t i = 1; i < args.size(); i++) {
        if (args[i].empty() || args[i][0] != L'-') {
            directory = args[i];
            foundDirectory = true;
            
            // If next arg exists and is not an option, it's the pattern
            if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != L'-') {
                pattern = args[i + 1];
                i++; // Skip the pattern in the next iteration
            }
            break;
        }
    }

    if (!foundDirectory) {
        std::cerr << "No directory specified." << std::endl;
        printUsage(args[0].c_str());
        return 1;
    }

    // Parse options
    for (size_t i = 1; i < args.size(); i++) {
        if (args[i].empty()) continue;
        
        if (strEqualsAny(args[i], {L"-r", L"--regex"})) {
            useRegex = true;
        }
        else if (strEqualsAny(args[i], {L"-s", L"--shallow"})) {
            shallow = true;
        }
        else if (strEqualsAny(args[i], {L"-d", L"--debug"})) {
            debug = true;
        }
        else if (strEqualsAny(args[i], {L"-t", L"--tab"})) {
            singleTabMode = true;
            // Tab mode no longer implies concise mode
        }
        else if (strEqualsAny(args[i], {L"-c", L"--concise"})) {
            conciseMode = true;
        }
        else if (strEqualsAny(args[i], {L"-b", L"--bare"})) {
            bareMode = true;
            conciseMode = true; // Bare mode implies concise mode
        }
        else if (strEqualsAny(args[i], {L"-x", L"--execute"}) && i + 1 < args.size()) {
            command = args[++i];
        }
        else if (strEqualsAny(args[i], {L"--sort"}) && i + 1 < args.size()) {
            sortOption = args[++i];
        }
        else if (args[i][0] == L'-') {
            std::cerr << "Unknown option: ";
            for (wchar_t c : args[i]) {
                std::cerr << (char)c;
            }
            std::cerr << std::endl;
            printUsage(args[0].c_str());
            return 1;
        }
    }

    // Only show these lines in debug mode
    if (debug) {
        std::cout << "Searching in directory: ";
        for (wchar_t c : directory) {
            std::cout << (char)c;
        }
        std::cout << std::endl;
        
        std::cout << "Pattern: ";
        for (wchar_t c : pattern) {
            std::cout << (char)c;
        }
        std::cout << std::endl;
        
        if (useRegex) {
            std::cout << "Using regex pattern matching" << std::endl;
        }
        
        if (shallow) {
            std::cout << "Performing shallow search (not recursive)" << std::endl;
        }
        
        if (singleTabMode) {
            std::cout << "Using single tab formatting mode" << std::endl;
        }
        
        if (conciseMode) {
            std::cout << "Using concise display mode" << std::endl;
        }
        
        if (bareMode) {
            std::cout << "Using bare display mode" << std::endl;
        }
        
        if (command) {
            std::cout << "Command to execute: ";
            for (wchar_t c : *command) {
                std::cout << (char)c;
            }
            std::cout << std::endl;
        }
        
        if (sortOption) {
            std::cout << "Sort option: ";
            for (wchar_t c : *sortOption) {
                std::cout << (char)c;
            }
            std::cout << std::endl;
        }
    }
    
    // Find files matching the pattern
    std::vector<FileInfo> results = FileFinder::findFiles(directory, pattern, useRegex, shallow, debug);
    
    // Sort the results if sorting option is provided
    if (sortOption) {
        std::vector<SortOption> sortOptions = parseSortOptions(*sortOption);
        sortFiles(results, sortOptions);
    }
    
    // Display results
    if (!conciseMode) {
        // Print column headers without the "Found X files" message
        printColumnHeaders(singleTabMode);
    }
    
    for (const auto& file : results) {
        printFileInfo(file, singleTabMode, bareMode);
        
        // Execute command if specified
        if (command) {
            executeCommand(*command, file.path);
        }
    }
    
    // Print summary if not in concise or bare mode
    if (!conciseMode) {
        // Get the console width dynamically
        const int totalWidth = getConsoleWidth();
        const int sizeWidth = 10;
        const int createdWidth = 16;
        const int modifiedWidth = 16;
        const int spacing = 2;
        const int pathWidth = totalWidth - sizeWidth - createdWidth - modifiedWidth - (spacing * 3);
        
        // Print separator line at the bottom with proper alignment
        if (singleTabMode) {
            std::cout << std::string(10, '-') << '\t'
                     << std::string(8, '-') << '\t'
                     << std::string(15, '-') << '\t'
                     << std::string(15, '-') << std::endl;
        } else {
            std::cout << std::left << std::setw(pathWidth) << std::string(pathWidth, '-')
                     << std::string(spacing, ' ')
                     << std::right << std::setw(sizeWidth) << std::string(sizeWidth, '-')
                     << std::string(spacing, ' ')
                     << std::right << std::setw(createdWidth) << std::string(createdWidth, '-')
                     << std::string(spacing, ' ')
                     << std::right << std::setw(modifiedWidth) << std::string(modifiedWidth, '-')
                     << std::endl;
        }
        std::cout << "Found " << results.size() << " files" << std::endl;
    }
    
    return 0;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
