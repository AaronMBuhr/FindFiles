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
#include <io.h>      // For _setmode
#include <fcntl.h>   // For _O_U16TEXT (or _O_WTEXT, _O_U8TEXT)

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
        bool debug = false,
        bool pathMatch = false
    ) {
        std::vector<FileInfo> results;
        std::wregex regexPattern;

        // Debug output for inputs
        if (debug) {
            std::wcout << L"Directory: " << directory << std::endl;
            std::wcout << L"Pattern: " << pattern << std::endl;
        }

        try {
            if (useRegex) {
                regexPattern = std::wregex(pattern, std::regex_constants::icase);
            } else {
                // Convert DOS wildcard to regex
                std::wstring regexStr = dosPatternToRegex(pattern, pathMatch);
                regexPattern = std::wregex(regexStr, std::regex_constants::icase);
            }
        }
        catch (const std::regex_error& e) {
            std::string what_str = e.what();
            std::wstring what_wstr(what_str.begin(), what_str.end());
            std::wcerr << L"Invalid regex pattern: " << what_wstr << std::endl;
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
            std::wcout << L"Search path: " << searchPath << std::endl;
        }

        // Find first file
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);

        if (hFind == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            std::wcerr << L"Error searching directory: " << error;
            
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
            
            std::wcerr << L" - " << (wchar_t*)lpMsgBuf;
            
            // Add directory name to error message
            std::wcerr << L" Directory: " << directory << std::endl;
            
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
                    std::vector<FileInfo> subDirResults = findFiles(fullPath, pattern, useRegex, shallow, debug, pathMatch);
                    results.insert(results.end(), subDirResults.begin(), subDirResults.end());
                }
            } else {
                // Process regular file if it matches the pattern
                std::wstring stringToMatch = pathMatch ? fullPath : findData.cFileName;
                if (std::regex_search(stringToMatch, regexPattern)) {
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
    static std::wstring dosPatternToRegex(const std::wstring& pattern, bool pathMatch = false) {
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
        if (pathMatch) {
            return result;
        }
        return L"^" + result + L"$";
    }
};

// Execute a command with substituted parameters
bool executeCommand(const std::wstring& commandTemplate, const FileInfo& fileInfo, bool dryRun, bool debugMode) {
    const std::wstring& filePath = fileInfo.path; // Get filePath from FileInfo

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
        std::wstring quotedDirectory = L"\"" + directory + L"\"";
        command.replace(pos, 2, quotedDirectory);
        pos += quotedDirectory.length();
    }
    
    // Substitute %n with filename
    pos = 0;
    while ((pos = command.find(L"%n", pos)) != std::wstring::npos) {
        std::wstring quotedFilename = L"\"" + filename + L"\"";
        command.replace(pos, 2, quotedFilename);
        pos += quotedFilename.length();
    }
    
    // Substitute %f with full path
    pos = 0;
    while ((pos = command.find(L"%f", pos)) != std::wstring::npos) {
        std::wstring quotedFilePath = L"\"" + filePath + L"\"";
        command.replace(pos, 2, quotedFilePath);
        pos += quotedFilePath.length();
    }

    if (dryRun) {
        if (debugMode) {
            std::wcout << filePath << L" -> " << command << std::endl;
        } else {
            std::wcout << command << std::endl;
        }
        return true; // Dry run "succeeded"
    }
    
    // Create process information structures
    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi;
    
    // Create a modifiable copy of the command for CreateProcessW
    wchar_t* cmdLine = _wcsdup(command.c_str());
    if (!cmdLine) {
        std::wcerr << L"Memory allocation failed" << std::endl;
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

        if (debugMode) {
            std::wcout << filePath << L" -> " << command << L" -> ok" << std::endl;
        } else {
            std::wcout << filePath << L"	-> ok" << std::endl;
        }

    } else {
        std::wcerr << L"Command execution failed: " << GetLastError();
        std::wcerr << L" for file: " << filePath << std::endl;
    }
    
    return success;
}

void printFileInfo(const FileInfo& info, bool singleTabMode = false, bool bareMode = false) {
    // If bare mode, only print the path
    if (bareMode) {
        std::wcout << info.path << std::endl;
        return;
    }

    // Format creation time
    auto formatTime = [singleTabMode](const auto& time) {
        auto tt = std::chrono::system_clock::to_time_t(time);
        struct tm timeinfo;
        localtime_s(&timeinfo, &tt);
        wchar_t buffer[20]; // Use wchar_t buffer
        if (singleTabMode) {
            // For tab mode, include seconds
            wcsftime(buffer, 20, L"%Y-%m-%d %H:%M:%S", &timeinfo); // Use wcsftime
        } else {
            wcsftime(buffer, 20, L"%Y-%m-%d %H:%M", &timeinfo); // Use wcsftime
        }
        return std::wstring(buffer); // Return std::wstring
    };

    // Get formatted times
    std::wstring createdTime = formatTime(info.creationTime);
    std::wstring modifiedTime = formatTime(info.modificationTime);


    if (singleTabMode) {
        // Single tab mode - just one tab between columns with full data
        // Use original bytes (not KB) for size
        std::wcout << info.path << L'	' << info.size << L'	' << createdTime << L'	' << modifiedTime << std::endl;
    } else {
        // Convert size to kilobytes, rounded up
        uintmax_t sizeKB = (info.size + 1023) / 1024; // Round up by adding 1023 before division
        std::wstring sizeStr = std::to_wstring(sizeKB); // Use std::to_wstring
        
        // Get the console width dynamically
        const int totalWidth = getConsoleWidth();
        const int sizeWidth = 10;
        const int createdWidth = 16;
        const int modifiedWidth = 16;
        const int spacing = 2;
        // Calculate path width to use all remaining space
        const int pathWidth = totalWidth - sizeWidth - createdWidth - modifiedWidth - (spacing * 3);
        
        // Print path (left justified, truncated if needed)
        if (info.path.length() > static_cast<size_t>(pathWidth)) { // Explicit cast
            std::wcout << std::left << std::setw(pathWidth) << (info.path.substr(0, pathWidth - 3) + L"...");
        } else {
            // std::left with std::setw(pathWidth) already pads with spaces to reach the width
            std::wcout << std::left << std::setw(pathWidth) << info.path;
        }
        
        // Print remaining columns with proper spacing and right justification
        std::wcout << std::wstring(spacing, L' ') 
                << std::right << std::setw(sizeWidth) << sizeStr
                << std::wstring(spacing, L' ') 
                << std::right << std::setw(createdWidth) << createdTime
                << std::wstring(spacing, L' ') 
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
        std::wcout << L"Path	Size	Created Date	Modified Date" << std::endl;
    } else {
        // Print column headers with proper alignment
        std::wcout << std::left << std::setw(pathWidth) << L"Path"
                 << std::wstring(spacing, L' ') 
                 << std::right << std::setw(sizeWidth) << L"Size (KB)"
                 << std::wstring(spacing, L' ') 
                 << std::right << std::setw(createdWidth) << L"Created"
                 << std::wstring(spacing, L' ') 
                 << std::right << std::setw(modifiedWidth) << L"Modified"
                 << std::endl;
        
        // Print separator line - exact same length as each column header
        std::wcout << std::left << std::setw(pathWidth) << std::wstring(pathWidth, L'-')
                 << std::wstring(spacing, L' ')
                 << std::right << std::setw(sizeWidth) << std::wstring(sizeWidth, L'-')
                 << std::wstring(spacing, L' ')
                 << std::right << std::setw(createdWidth) << std::wstring(createdWidth, L'-')
                 << std::wstring(spacing, L' ')
                 << std::right << std::setw(modifiedWidth) << std::wstring(modifiedWidth, L'-')
                 << std::endl;
    }
}

void printUsage(const wchar_t* programName) {
    std::wcout << L"Usage: " << programName << L" <directory> <pattern> [options]" << std::endl;
    std::wcout << L"Options:" << std::endl;
    std::wcout << L"  -r, --regex          Treat pattern as regex instead of DOS wildcard" << std::endl;
    std::wcout << L"  -s, --shallow        Shallow search (do not recurse into subdirectories)" << std::endl;
    std::wcout << L"  -x, --execute \"cmd\"  Execute command on each found file" << std::endl;
    std::wcout << L"                       %d = directory, %n = filename, %f = full path" << std::endl;
    std::wcout << L"  -d, --debug          Show detailed debug information during the search" << std::endl;
    std::wcout << L"  -t, --tab            Use single tab between columns (better for parsing)" << std::endl;
    std::wcout << L"  -c, --concise        Display results without headers or summary" << std::endl;
    std::wcout << L"  -b, --bare           Display only file paths (implies --concise)" << std::endl;
    std::wcout << L"  -P, --path-match     Match pattern against full path instead of filename" << std::endl;
    std::wcout << L"  --sort <order>       Sort results by specified criteria" << std::endl;
    std::wcout << L"                       p=path, n=name, s=size, c=created date, m=modified date" << std::endl;
    std::wcout << L"                       Prefix a field character with '-' for descending order (e.g., -n for" << std::endl;
    std::wcout << L"                       name descending, p-s for path ascending then size descending)." << std::endl;
    std::wcout << L"                       A '-' applies only to the next field character." << std::endl;
    std::wcout << L"  --dry-run            Show commands that would be executed without running them." << std::endl;
    std::wcout << L"                       If -d (--debug) is also active, prefixes output with the filename." << std::endl;
    std::wcout << L"  -h, --help           Display this help message" << std::endl;
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

int wmain(int argc, wchar_t* argv[]) { // Changed to wmain and wchar_t*
    // Set console output to Unicode (UTF-16)
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);


    // Convert arguments to wide strings for easier processing
    std::vector<std::wstring> args;
    for (int i = 0; i < argc; i++) {
        args.push_back(argv[i]); // argv is already wchar_t*
    }

    // Default values
    std::wstring directory; // Must be specified by user
    std::wstring pattern = L"*";
    bool useRegex = false;
    bool shallow = false;
    bool debug = false;
    bool singleTabMode = false;
    bool conciseMode = false;
    bool bareMode = false;
    bool pathMatchMode = false;
    std::optional<std::wstring> command;
    std::optional<std::wstring> sortOption;
    bool dryRunMode = false;
    bool anyCommandFailed = false; // To track if any executed command fails

    // Check for help flag (can be the first argument or appear later)
    if (argc < 2) { 
        // Let it proceed to positional argument check, which will fail.
    } else if (strEqualsAny(args[1], {L"-h", L"--help", L"/?"})) {
        printUsage(args[0].c_str());
        return 0;
    }

    std::vector<std::wstring> positionalArgs;

    // Parse options and collect positional arguments
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
        }
        else if (strEqualsAny(args[i], {L"-c", L"--concise"})) {
            conciseMode = true;
        }
        else if (strEqualsAny(args[i], {L"-b", L"--bare"})) {
            bareMode = true;
            conciseMode = true; // Bare mode implies concise mode
        }
        else if (strEqualsAny(args[i], {L"-P", L"--path-match"})) {
            pathMatchMode = true;
        }
        else if (strEqualsAny(args[i], {L"--dry-run"})) {
            dryRunMode = true;
        }
        else if (strEqualsAny(args[i], {L"-x", L"--execute"})) {
            if (i + 1 < args.size()) {
                command = args[++i];
            } else {
                std::wcerr << L"Error: Option " << args[i] << L" requires an argument." << std::endl;
                printUsage(args[0].c_str());
                return 1;
            }
        }
        else if (strEqualsAny(args[i], {L"--sort"})) {
            if (i + 1 < args.size()) {
                sortOption = args[++i];
            } else {
                std::wcerr << L"Error: Option " << args[i] << L" requires an argument." << std::endl;
                printUsage(args[0].c_str());
                return 1;
            }
        }
        else if (strEqualsAny(args[i], {L"-h", L"--help", L"/?"})) {
            printUsage(args[0].c_str());
            return 0;
        }
        else if (args[i][0] == L'-') { // Argument starts with '-' but is not a recognized option
            std::wcerr << L"Unknown option: " << args[i] << std::endl;
            printUsage(args[0].c_str());
            return 1;
        }
        else { // Positional argument
            positionalArgs.push_back(args[i]);
        }
    }

    // Assign positional arguments
    if (positionalArgs.empty()) {
        std::wcerr << L"No directory specified." << std::endl;
        printUsage(args[0].c_str());
        return 1;
    }
    
    directory = positionalArgs[0];
    if (positionalArgs.size() >= 2) {
        pattern = positionalArgs[1];
    }
    // If only one positional arg, 'pattern' remains its default L"*"

    if (positionalArgs.size() > 2) {
        std::wcerr << L"Too many positional arguments specified." << std::endl;
        for(size_t k=2; k < positionalArgs.size(); ++k) {
            std::wcerr << L"Unexpected argument: " << positionalArgs[k] << std::endl;
        }
        printUsage(args[0].c_str());
        return 1;
    }

    // Warn if --dry-run is used without --execute
    if (dryRunMode && !command.has_value()) {
        std::wcerr << L"Warning: --dry-run option is specified without --execute. No commands to dry run." << std::endl;
    }

    // Only show these lines in debug mode
    if (debug) {
        std::wcout << L"Searching in directory: " << directory << std::endl;
        std::wcout << L"Pattern: " << pattern << std::endl;
        
        if (useRegex) {
            std::wcout << L"Using regex pattern matching" << std::endl;
        }
        
        if (shallow) {
            std::wcout << L"Performing shallow search (not recursive)" << std::endl;
        }
        
        if (singleTabMode) {
            std::wcout << L"Using single tab formatting mode" << std::endl;
        }
        
        if (conciseMode) {
            std::wcout << L"Using concise display mode" << std::endl;
        }
        
        if (bareMode) {
            std::wcout << L"Using bare display mode" << std::endl;
        }
        
        if (pathMatchMode) {
            std::wcout << L"Matching pattern against full path" << std::endl;
        }
        
        if (dryRunMode) {
            std::wcout << L"Using dry-run mode" << std::endl;
        }
        
        if (command) {
            std::wcout << L"Command to execute: " << *command << std::endl;
        }
        
        if (sortOption) {
            std::wcout << L"Sort option: " << *sortOption << std::endl;
        }
    }
    
    // Find files matching the pattern
    std::vector<FileInfo> results = FileFinder::findFiles(directory, pattern, useRegex, shallow, debug, pathMatchMode);
    
    // Sort the results if sorting option is provided
    if (sortOption) {
        std::vector<SortOption> sortOptions = parseSortOptions(*sortOption);
        sortFiles(results, sortOptions);
    }
    
    bool isExecutingCommand = command.has_value();
    bool isDryRunExecute = isExecutingCommand && dryRunMode;
    
    // Display results
    // Only print headers if not concise and not executing a command (which has its own output format)
    if (!conciseMode && !isExecutingCommand) {
        printColumnHeaders(singleTabMode);
    }

    // Print "Executing" header if -x is used and -b is not
    if (isExecutingCommand && !bareMode) {
        if (isDryRunExecute) {
            std::wcout << L"Executing (dry run)" << std::endl;
            std::wcout << L"-------------------" << std::endl;
        } else {
            std::wcout << L"Executing" << std::endl;
            std::wcout << L"-----------" << std::endl;
        }
    }
    
    for (const auto& file : results) {
        if (isExecutingCommand) {
            if (!executeCommand(*command, file, dryRunMode, debug)) {
                anyCommandFailed = true; // Track if any command execution fails
            }
        } else {
            // Standard display if not executing a command
            printFileInfo(file, singleTabMode, bareMode);
        }
    }
    
    // Print summary
    if (isDryRunExecute) {
        std::wcout << L"Dry run: " << results.size() << L" commands would be generated." << std::endl;
    } else if (isExecutingCommand) { // Actual command execution
        std::wcout << results.size() << L" files processed for command execution." << std::endl;
        if(anyCommandFailed){
             std::wcout << L"One or more command executions failed." << std::endl;
        }
    } else if (!conciseMode) { // Not executing a command, and not concise mode
        // Get the console width dynamically
        const int totalWidth = getConsoleWidth();
        const int sizeWidth = 10;
        const int createdWidth = 16;
        const int modifiedWidth = 16;
        const int spacing = 2;
        const int pathWidth = totalWidth - sizeWidth - createdWidth - modifiedWidth - (spacing * 3);
        
        // Print separator line at the bottom with proper alignment
        if (singleTabMode) {
            std::wcout << std::wstring(10, L'-') << L'	'
                     << std::wstring(8, L'-') << L'	'
                     << std::wstring(15, L'-') << L'	'
                     << std::wstring(15, L'-') << std::endl;
        } else {
            std::wcout << std::left << std::setw(pathWidth) << std::wstring(pathWidth, L'-')
                     << std::wstring(spacing, L' ')
                     << std::right << std::setw(sizeWidth) << std::wstring(sizeWidth, L'-')
                     << std::wstring(spacing, L' ')
                     << std::right << std::setw(createdWidth) << std::wstring(createdWidth, L'-')
                     << std::wstring(spacing, L' ')
                     << std::right << std::setw(modifiedWidth) << std::wstring(modifiedWidth, L'-')
                     << std::endl;
        }
        std::wcout << L"Found " << results.size() << L" files" << std::endl;
    }
    
    return anyCommandFailed ? 1 : 0; // Return 1 if any command failed, 0 otherwise
}

// Ensure pch.h is included if using precompiled headers, or remove if not.
// For a typical Visual Studio C++ console project, pch.h might be:
/*
#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <chrono>
#include <iomanip>
#include <optional>
#include <windows.h>
#include <sstream>
#include <tchar.h>
#include <algorithm>
#include <io.h>
#include <fcntl.h>

#endif //PCH_H
*/

