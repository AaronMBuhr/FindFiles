// FindFiles.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h" // Assuming Visual Studio precompiled header
#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <iomanip> // For std::setw, std::left, std::right
#include <regex>   // For std::wregex, std::regex_search, std::regex_match, std::smatch
#include <algorithm> // For std::sort
#include <windows.h> // For Windows API functions like FindFirstFileW, GetLastError, etc.
#include <sstream>   // For std::wstringstream (if needed, though not explicitly used in provided snippets)
#include <tchar.h>     // For _TCHAR related macros (though direct wchar_t is mostly used)
#include <io.h>        // For _setmode
#include <fcntl.h>     // For _O_U16TEXT
#include <map>         // For std::map (used in verbose mode)
#include <shellapi.h>  // For CommandLineToArgvW (used for main function fix)

// -----------------------------------------------------------------------------
// Forward declarations (default arguments specified here **only**)
// -----------------------------------------------------------------------------
struct FileInfo; // Forward declare FileInfo struct

void printFileInfo(const FileInfo& info,
                   bool singleTabMode  = false,
                   bool bareMode       = false,
                   bool verboseMode    = false,
                   bool conciseMode    = false,
                   const std::wstring& directory = L"",
                   const std::wstring& filename  = L"");

void printColumnHeaders(bool singleTabMode = false,
                        bool verboseMode   = false);
// -----------------------------------------------------------------------------

// Function to get the console width
int getConsoleWidth() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int consoleWidth = 79; // Default fallback width
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        consoleWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        consoleWidth -= 1; // Avoid automatic line wrapping
        if (consoleWidth < 50) consoleWidth = 50;
    }
    return consoleWidth;
}

struct FileInfo {
    std::wstring path;
    std::chrono::system_clock::time_point creationTime;
    std::chrono::system_clock::time_point modificationTime;
    uintmax_t size;

    bool operator<(const FileInfo& other) const { return path < other.path; }
    bool operator==(const FileInfo& other) const { return path == other.path; }
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
                    equal = (a.path == b.path);
                    break;
                case SortField::Name: {
                    size_t aLastSlash = a.path.find_last_of(L'\\');
                    size_t bLastSlash = b.path.find_last_of(L'\\');
                    std::wstring aName = (aLastSlash != std::wstring::npos) ? a.path.substr(aLastSlash + 1) : a.path;
                    std::wstring bName = (bLastSlash != std::wstring::npos) ? b.path.substr(bLastSlash + 1) : b.path;
                    result = aName < bName;
                    equal = (aName == bName);
                    break;
                }
                case SortField::Size:
                    result = a.size < b.size;
                    equal = (a.size == b.size);
                    break;
                case SortField::CreationDate:
                    result = a.creationTime < b.creationTime;
                    equal = (a.creationTime == b.creationTime);
                    break;
                case SortField::ModificationDate:
                    result = a.modificationTime < b.modificationTime;
                    equal = (a.modificationTime == b.modificationTime);
                    break;
            }
            if (!equal) {
                return option.ascending ? result : !result;
            }
        }
        return a.path < b.path; // Default tie-breaker
    });
}

// Function to parse date string into time_point
std::optional<std::chrono::system_clock::time_point> parseDateTime(const std::wstring& dateStr) {
    std::wregex dateFormats[] = {
        std::wregex(L"^(\\d{4})(\\d{2})(\\d{2})$"), // YYYYMMDD
        std::wregex(L"^(\\d{4})(\\d{2})(\\d{2})(\\d{2})(\\d{2})$"), // YYYYMMDDHHMM
        std::wregex(L"^(\\d{4})(\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{2})$"), // YYYYMMDDHHMMSS
        std::wregex(L"^(\\d{4})/(\\d{2})/(\\d{2})$"), // YYYY/MM/DD
        std::wregex(L"^(\\d{4})/(\\d{2})/(\\d{2})-(\\d{2}):(\\d{2})$"), // YYYY/MM/DD-HH:MM
        std::wregex(L"^(\\d{4})/(\\d{2})/(\\d{2})-(\\d{2}):(\\d{2}):(\\d{2})$") // YYYY/MM/DD-HH:MM:SS
    };
    for (int i = 0; i < 6; ++i) {
        std::wsmatch match;
        if (std::regex_match(dateStr, match, dateFormats[i])) {
            tm timeinfo = {};
            timeinfo.tm_year = std::stoi(match[1].str()) - 1900;
            timeinfo.tm_mon = std::stoi(match[2].str()) - 1;
            timeinfo.tm_mday = std::stoi(match[3].str());
            if (i == 1 || i == 4) { // YYYYMMDDHHMM or YYYY/MM/DD-HH:MM
                timeinfo.tm_hour = std::stoi(match[4].str());
                timeinfo.tm_min = std::stoi(match[5].str());
            } else if (i == 2 || i == 5) { // YYYYMMDDHHMMSS or YYYY/MM/DD-HH:MM:SS
                timeinfo.tm_hour = std::stoi(match[4].str());
                timeinfo.tm_min = std::stoi(match[5].str());
                timeinfo.tm_sec = std::stoi(match[6].str());
            }
            time_t tt = mktime(&timeinfo);
            if (tt == -1) return std::nullopt;
            return std::chrono::system_clock::from_time_t(tt);
        }
    }
    return std::nullopt;
}

// Filter files based on date criteria
std::vector<FileInfo> filterFilesByDate(
    const std::vector<FileInfo>& files,
    const std::optional<std::chrono::system_clock::time_point>& createdStart,
    const std::optional<std::chrono::system_clock::time_point>& createdEnd,
    const std::optional<std::chrono::system_clock::time_point>& modifiedStart,
    const std::optional<std::chrono::system_clock::time_point>& modifiedEnd) {
    std::vector<FileInfo> filtered;
    for (const auto& file : files) {
        bool include = true;
        if (createdStart && file.creationTime < *createdStart) include = false;
        if (createdEnd && file.creationTime >= *createdEnd) include = false;
        if (modifiedStart && file.modificationTime < *modifiedStart) include = false;
        if (modifiedEnd && file.modificationTime >= *modifiedEnd) include = false;
        if (include) filtered.push_back(file);
    }
    return filtered;
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

class FileFinder {
public:
    static std::vector<FileInfo> findFiles(
        const std::wstring& directory,
        const std::wstring& pattern,
        bool useRegex = false,
        bool shallow = false,
        bool debug = false,
        bool pathMatch = false) {
        std::vector<FileInfo> results;
        std::wregex regexPattern;

        if (debug) {
            std::wcout << L"Directory: " << directory << std::endl;
            std::wcout << L"Pattern: " << pattern << std::endl;
        }

        try {
            if (useRegex) {
                regexPattern = std::wregex(pattern, std::regex_constants::icase);
            } else {
                std::wstring regexStr = dosPatternToRegex(pattern, pathMatch);
                regexPattern = std::wregex(regexStr, std::regex_constants::icase);
            }
        } catch (const std::regex_error& e) {
            std::string what_str = e.what();
            std::wcerr << L"Invalid regex pattern: " << std::wstring(what_str.begin(), what_str.end()) << std::endl;
            return results;
        }

        std::wstring searchPath = directory;
        if (!searchPath.empty() && searchPath.back() != L'\\') {
            searchPath += L'\\';
        }
        searchPath += L'*';

        if (debug) {
            std::wcout << L"Search path: " << searchPath << std::endl;
        }

        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);

        if (hFind == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND) { // Don't print error if dir just doesn't exist or is empty
                LPVOID lpMsgBuf;
                FormatMessageW(
                    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&lpMsgBuf, 0, NULL);
                std::wcerr << L"Error searching directory: " << error << L" - " << (wchar_t*)lpMsgBuf << L" Directory: " << directory << std::endl;
                LocalFree(lpMsgBuf);
            }
            return results;
        }

        do {
            if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
                continue;
            }

            std::wstring fullPath = directory;
            if (!fullPath.empty() && fullPath.back() != L'\\') {
                fullPath += L'\\';
            }
            fullPath += findData.cFileName;

            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!shallow) {
                    std::vector<FileInfo> subDirResults = findFiles(fullPath, pattern, useRegex, shallow, debug, pathMatch);
                    results.insert(results.end(), subDirResults.begin(), subDirResults.end());
                }
            } else {
                std::wstring stringToMatch = pathMatch ? fullPath : findData.cFileName;
                if (std::regex_search(stringToMatch, regexPattern)) {
                    FileInfo info;
                    info.path = fullPath;

                    FILETIME ftCreate = findData.ftCreationTime;
                    FILETIME ftWrite = findData.ftLastWriteTime;
                    SYSTEMTIME stCreate, stWrite;
                    FileTimeToSystemTime(&ftCreate, &stCreate);
                    FileTimeToSystemTime(&ftWrite, &stWrite);

                    tm tmCreate = {stCreate.wSecond, stCreate.wMinute, stCreate.wHour, stCreate.wDay, stCreate.wMonth - 1, stCreate.wYear - 1900};
                    info.creationTime = std::chrono::system_clock::from_time_t(_mkgmtime(&tmCreate));
                    
                    tm tmWrite = {stWrite.wSecond, stWrite.wMinute, stWrite.wHour, stWrite.wDay, stWrite.wMonth - 1, stWrite.wYear - 1900};
                    info.modificationTime = std::chrono::system_clock::from_time_t(_mkgmtime(&tmWrite));

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
    static std::wstring dosPatternToRegex(const std::wstring& pattern, bool pathMatch) {
        std::wstring result;
        result.reserve(pattern.length() * 2);
        if (!pathMatch) result += L'^';

        for (wchar_t c : pattern) {
            switch (c) {
                case L'*': result += L".*"; break;
                case L'?': result += L"."; break;
                case L'.': case L'[': case L']': case L'(': case L')':
                case L'{': case L'}': case L'+': case L'^': case L'$':
                case L'|': case L'\\':
                    result += L'\\';
                    result += c;
                    break;
                default: result += c; break;
            }
        }
        if (!pathMatch) result += L'$';
        return result;
    }
}; 

// Execute a command with substituted parameters
bool executeCommand(const std::wstring& commandTemplate, const FileInfo& fileInfo, bool dryRun, bool debugMode) {
    const std::wstring& filePath = fileInfo.path;
    std::wstring directory, filename;
    size_t lastSlash = filePath.find_last_of(L'\\');
    if (lastSlash != std::wstring::npos) {
        directory = filePath.substr(0, lastSlash);
        filename = filePath.substr(lastSlash + 1);
    } else {
        directory = L".";
        filename = filePath;
    }
    std::wstring command = commandTemplate;
    auto replacePlaceholder = [&](std::wstring& cmd, const std::wstring& placeholder, const std::wstring& value) {
        size_t pos = 0;
        std::wstring quotedValue = L"\"" + value + L"\"";
        while ((pos = cmd.find(placeholder, pos)) != std::wstring::npos) {
            cmd.replace(pos, placeholder.length(), quotedValue);
            pos += quotedValue.length();
        }
    };
    replacePlaceholder(command, L"%d", directory);
    replacePlaceholder(command, L"%n", filename);
    replacePlaceholder(command, L"%f", filePath);

    if (dryRun) {
        if (debugMode) std::wcout << filePath << L" -> " << command << std::endl;
        else std::wcout << command << std::endl;
        return true;
    }

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi;
    wchar_t* cmdLine = _wcsdup(command.c_str());
    if (!cmdLine) {
        std::wcerr << L"Memory allocation failed for command line." << std::endl;
        return false;
    }
    bool success = CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(cmdLine);
    if (success) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (debugMode) std::wcout << filePath << L" -> " << command << L" -> ok" << std::endl;
        else std::wcout << filePath << L"\t-> ok" << std::endl;
    } else {
        std::wcerr << L"Command execution failed: " << GetLastError() << L" for file: " << filePath << std::endl;
    }
    return success;
}

// Definition of printFileInfo (NO default arguments here)
void printFileInfo(const FileInfo& info, bool singleTabMode, bool bareMode, bool verboseMode, bool conciseMode, const std::wstring& directory, const std::wstring& filename) {
    if (bareMode) {
        std::wcout << info.path << std::endl;
        return;
    }
    std::wstring displayItemPath = (verboseMode && !filename.empty()) ? filename : info.path;
    if (verboseMode && conciseMode && !directory.empty()) {
         displayItemPath = directory; // For verbose-concise, first part is directory
    }

    auto formatTime = [singleTabMode](const auto& timePoint) {
        auto tt = std::chrono::system_clock::to_time_t(timePoint);
        struct tm timeinfo;
        localtime_s(&timeinfo, &tt);
        wchar_t buffer[20];
        wcsftime(buffer, 20, singleTabMode ? L"%Y-%m-%d %H:%M:%S" : L"%Y-%m-%d %H:%M", &timeinfo);
        return std::wstring(buffer);
    };
    std::wstring createdTimeStr = formatTime(info.creationTime);
    std::wstring modifiedTimeStr = formatTime(info.modificationTime);

    if (singleTabMode) {
        if (verboseMode && conciseMode) {
             std::wcout << displayItemPath << L'\t' << filename << L'\t' << info.size << L'\t' << createdTimeStr << L'\t' << modifiedTimeStr << std::endl;
        } else {
            std::wcout << displayItemPath << L'\t' << info.size << L'\t' << createdTimeStr << L'\t' << modifiedTimeStr << std::endl;
        }
    } else {
        uintmax_t sizeKB = (info.size + 1023) / 1024;
        std::wstring sizeStr = std::to_wstring(sizeKB);
        const int totalWidth = getConsoleWidth();
        int pathWidthCalc = totalWidth - 10 - 16 - 16 - (2 * 3); // Default for non-verbose

        if (verboseMode && conciseMode) {
            const int dirColWidth = 40;
            const int fileColWidth = pathWidthCalc - dirColWidth -2; 
            
            std::wcout << std::left << std::setw(dirColWidth) << displayItemPath; // Directory
            std::wcout << std::wstring(2, L' ');
            std::wcout << std::left << std::setw(fileColWidth) << filename; // Filename
        } else {
             if (displayItemPath.length() > static_cast<size_t>(pathWidthCalc)) {
                std::wcout << std::left << std::setw(pathWidthCalc) << (displayItemPath.substr(0, pathWidthCalc - 3) + L"...");
            } else {
                std::wcout << std::left << std::setw(pathWidthCalc) << displayItemPath;
            }
        }
        std::wcout << std::wstring(2, L' ') << std::right << std::setw(10) << sizeStr
                   << std::wstring(2, L' ') << std::right << std::setw(16) << createdTimeStr
                   << std::wstring(2, L' ') << std::right << std::setw(16) << modifiedTimeStr
                   << std::endl;
    }
}

// Definition of printColumnHeaders (NO default arguments here)
void printColumnHeaders(bool singleTabMode, bool verboseMode) {
    const int totalWidth = getConsoleWidth();
    const int sizeCol = 10, createdCol = 16, modifiedCol = 16, spacing = 2;
    int pathCol = totalWidth - sizeCol - createdCol - modifiedCol - (spacing * 3);

    if (verboseMode) { // Covers verbose-normal and verbose-concise
        const int dirCol = 40;
        int fileCol = pathCol - dirCol - spacing; // Adjust fileCol if dirCol is present
        if (fileCol < 10) fileCol = 10; // Minimum width for filename

        if (singleTabMode) {
            std::wcout << L"Directory\tFilename\tSize\tCreated Date\tModified Date" << std::endl;
        } else {
            std::wcout << std::left << std::setw(dirCol) << L"Directory"
                       << std::wstring(spacing, L' ') << std::left << std::setw(fileCol) << L"Filename"
                       << std::wstring(spacing, L' ') << std::right << std::setw(sizeCol) << L"Size (KB)"
                       << std::wstring(spacing, L' ') << std::right << std::setw(createdCol) << L"Created"
                       << std::wstring(spacing, L' ') << std::right << std::setw(modifiedCol) << L"Modified"
                       << std::endl;
            std::wcout << std::wstring(dirCol, L'-')
                       << std::wstring(spacing, L' ') << std::wstring(fileCol, L'-')
                       << std::wstring(spacing, L' ') << std::wstring(sizeCol, L'-')
                       << std::wstring(spacing, L' ') << std::wstring(createdCol, L'-')
                       << std::wstring(spacing, L' ') << std::wstring(modifiedCol, L'-')
                       << std::endl;
        }
    } else { // Non-verbose mode
        if (singleTabMode) {
            std::wcout << L"Path\tSize\tCreated Date\tModified Date" << std::endl;
        } else {
            std::wcout << std::left << std::setw(pathCol) << L"Path"
                       << std::wstring(spacing, L' ') << std::right << std::setw(sizeCol) << L"Size (KB)"
                       << std::wstring(spacing, L' ') << std::right << std::setw(createdCol) << L"Created"
                       << std::wstring(spacing, L' ') << std::right << std::setw(modifiedCol) << L"Modified"
                       << std::endl;
            std::wcout << std::wstring(pathCol, L'-')
                       << std::wstring(spacing, L' ') << std::wstring(sizeCol, L'-')
                       << std::wstring(spacing, L' ') << std::wstring(createdCol, L'-')
                       << std::wstring(spacing, L' ') << std::wstring(modifiedCol, L'-')
                       << std::endl;
        }
    }
}

// Function to print files grouped by directory in verbose mode
void printFilesVerbose(const std::vector<FileInfo>& files, bool singleTabMode, bool conciseMode, bool bareMode) {
    if (bareMode) {
        for (const auto& file : files) { std::wcout << file.path << std::endl; }
        return;
    }
    std::map<std::wstring, std::vector<const FileInfo*>> filesByDir;
    for (const auto& file : files) {
        size_t lastSlash = file.path.find_last_of(L'\\');
        std::wstring dir = (lastSlash != std::wstring::npos) ? file.path.substr(0, lastSlash) : L".";
        filesByDir[dir].push_back(&file);
    }
    bool firstDir = true;
    for (const auto& pair : filesByDir) {
        const std::wstring& dir = pair.first;
        const std::vector<const FileInfo*>& dirFiles = pair.second;
        if (conciseMode) { // Verbose Concise: Dir | Filename | Size | Created | Modified (headers are global if shown)
            for (const auto* filePtr : dirFiles) {
                size_t lastSlash = filePtr->path.find_last_of(L'\\');
                std::wstring filenameStr = (lastSlash != std::wstring::npos) ? filePtr->path.substr(lastSlash + 1) : filePtr->path;
                printFileInfo(*filePtr, singleTabMode, false, true, conciseMode, dir, filenameStr);
            }
        } else { // Normal Verbose: Directory Header then file list with its own headers
            if (!firstDir) std::wcout << std::endl;
            firstDir = false;
            std::wcout << dir << L":" << std::endl;
            printColumnHeaders(singleTabMode, true); // Per-directory headers for normal verbose
            for (const auto* filePtr : dirFiles) {
                size_t lastSlash = filePtr->path.find_last_of(L'\\');
                std::wstring filenameStr = (lastSlash != std::wstring::npos) ? filePtr->path.substr(lastSlash + 1) : filePtr->path;
                printFileInfo(*filePtr, singleTabMode, false, true, conciseMode, dir, filenameStr);
            }
        }
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
    std::wcout << L"  -v, --verbose        Group output by directory. In normal mode, shows directory" << std::endl;
    std::wcout << L"                       headers with files listed below. In concise mode, splits" << std::endl;
    std::wcout << L"                       path into separate directory and filename columns." << std::endl;
    std::wcout << L"  -P, --path-match     Match pattern against full path instead of filename" << std::endl;
    std::wcout << L"  --sort <order>       Sort results by specified criteria" << std::endl;
    std::wcout << L"                       p=path, n=name, s=size, c=created date, m=modified date" << std::endl;
    std::wcout << L"                       Prefix a field character with '-' for descending order (e.g., -n)." << std::endl;
    std::wcout << L"  --date-created-start <date>  Filter files created on or after this date (inclusive)" << std::endl;
    std::wcout << L"  --date-created-end <date>    Filter files created before this date (exclusive)" << std::endl;
    std::wcout << L"  --date-modified-start <date> Filter files modified on or after this date (inclusive)" << std::endl;
    std::wcout << L"  --date-modified-end <date>   Filter files modified before this date (exclusive)" << std::endl;
    std::wcout << L"                       Date formats: YYYYMMDD[HHMM[SS]], YYYY/MM/DD[-HH:MM[:SS]]" << std::endl;
    std::wcout << L"  --dry-run            Show commands that would be executed without running them." << std::endl;
    std::wcout << L"  -h, --help           Display this help message" << std::endl;
} 

int main(int /*argc_c*/, char* /*argv_c*/[]) {
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    int argc_w;
    LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);
    if (!argv_w) {
        std::wcerr << L"Error: Could not get command line arguments." << std::endl;
        return 1;
    }

    std::vector<std::wstring> args;
    for (int i = 0; i < argc_w; i++) {
        args.push_back(argv_w[i]);
    }

    std::wstring directory;
    std::wstring pattern = L"*";
    bool useRegex = false, shallow = false, debug = false, singleTabMode = false;
    bool conciseMode = false, bareMode = false, verboseMode = false, pathMatchMode = false;
    std::optional<std::wstring> command, sortOption;
    bool dryRunMode = false, anyCommandFailed = false;

    std::optional<std::chrono::system_clock::time_point> dateCreatedStart, dateCreatedEnd;
    std::optional<std::chrono::system_clock::time_point> dateModifiedStart, dateModifiedEnd;

    if (argc_w < 2) { /* Let positional arg check handle */ }
    else if (strEqualsAny(args[1], {L"-h", L"--help", L"/?"})) {
        printUsage(args[0].c_str());
        LocalFree(argv_w);
        return 0;
    }

    std::vector<std::wstring> positionalArgs;
    for (size_t i = 1; i < args.size(); i++) {
        const std::wstring& arg = args[i];
        if (arg.empty()) continue;

        if (strEqualsAny(arg, {L"-r", L"--regex"})) useRegex = true;
        else if (strEqualsAny(arg, {L"-s", L"--shallow"})) shallow = true;
        else if (strEqualsAny(arg, {L"-d", L"--debug"})) debug = true;
        else if (strEqualsAny(arg, {L"-t", L"--tab"})) singleTabMode = true;
        else if (strEqualsAny(arg, {L"-c", L"--concise"})) conciseMode = true;
        else if (strEqualsAny(arg, {L"-b", L"--bare"})) { bareMode = true; conciseMode = true; }
        else if (strEqualsAny(arg, {L"-v", L"--verbose"})) verboseMode = true;
        else if (strEqualsAny(arg, {L"-P", L"--path-match"})) pathMatchMode = true;
        else if (strEqualsAny(arg, {L"--dry-run"})) dryRunMode = true;
        else if (strEqualsAny(arg, {L"-x", L"--execute"})) {
            if (++i < args.size()) command = args[i];
            else { std::wcerr << L"Error: --execute requires an argument." << std::endl; LocalFree(argv_w); return 1; }
        }
        else if (strEqualsAny(arg, {L"--sort"})) {
            if (++i < args.size()) sortOption = args[i];
            else { std::wcerr << L"Error: --sort requires an argument." << std::endl; LocalFree(argv_w); return 1; }
        }
        else if (strEqualsAny(arg, {L"--date-created-start"})) {
            if (++i < args.size()) { if (auto dt = parseDateTime(args[i])) dateCreatedStart = dt; else {std::wcerr << L"Invalid date for --date-created-start." << std::endl; LocalFree(argv_w); return 1;}}
            else { std::wcerr << L"Error: --date-created-start requires an argument." << std::endl; LocalFree(argv_w); return 1; }
        }
        else if (strEqualsAny(arg, {L"--date-created-end"})) {
            if (++i < args.size()) { if (auto dt = parseDateTime(args[i])) dateCreatedEnd = dt; else {std::wcerr << L"Invalid date for --date-created-end." << std::endl; LocalFree(argv_w); return 1;}}
            else { std::wcerr << L"Error: --date-created-end requires an argument." << std::endl; LocalFree(argv_w); return 1; }
        }
        else if (strEqualsAny(arg, {L"--date-modified-start"})) {
            if (++i < args.size()) { if (auto dt = parseDateTime(args[i])) dateModifiedStart = dt; else {std::wcerr << L"Invalid date for --date-modified-start." << std::endl; LocalFree(argv_w); return 1;}}
            else { std::wcerr << L"Error: --date-modified-start requires an argument." << std::endl; LocalFree(argv_w); return 1; }
        }
        else if (strEqualsAny(arg, {L"--date-modified-end"})) {
            if (++i < args.size()) { if (auto dt = parseDateTime(args[i])) dateModifiedEnd = dt; else {std::wcerr << L"Invalid date for --date-modified-end." << std::endl; LocalFree(argv_w); return 1;}}
            else { std::wcerr << L"Error: --date-modified-end requires an argument." << std::endl; LocalFree(argv_w); return 1; }
        }
        else if (strEqualsAny(arg, {L"-h", L"--help", L"/?"})) { printUsage(args[0].c_str()); LocalFree(argv_w); return 0; }
        else if (arg[0] == L'-') { std::wcerr << L"Unknown option: " << arg << std::endl; printUsage(args[0].c_str()); LocalFree(argv_w); return 1; }
        else { positionalArgs.push_back(arg); }
    }

    if (positionalArgs.empty()) { std::wcerr << L"No directory specified." << std::endl; printUsage(args[0].c_str()); LocalFree(argv_w); return 1; }
    directory = positionalArgs[0];
    if (positionalArgs.size() >= 2) pattern = positionalArgs[1];
    if (positionalArgs.size() > 2) { std::wcerr << L"Too many positional arguments." << std::endl; printUsage(args[0].c_str()); LocalFree(argv_w); return 1; }

    if (dryRunMode && !command) std::wcerr << L"Warning: --dry-run specified without --execute." << std::endl;

    if (debug) {
        std::wcout << L"Searching in directory: " << directory << L"\nPattern: " << pattern << std::endl;
        if (useRegex) std::wcout << L"Using regex pattern matching" << std::endl;
        if (shallow) std::wcout << L"Performing shallow search" << std::endl;
        if (singleTabMode) std::wcout << L"Using single tab formatting" << std::endl;
        if (conciseMode) std::wcout << L"Using concise display" << std::endl;
        if (bareMode) std::wcout << L"Using bare display" << std::endl;
        if (verboseMode) std::wcout << L"Using verbose display" << std::endl;
        if (pathMatchMode) std::wcout << L"Matching pattern against full path" << std::endl;
        if (dryRunMode) std::wcout << L"Dry-run mode enabled" << std::endl;
        if (command) std::wcout << L"Command to execute: " << *command << std::endl;
        if (sortOption) std::wcout << L"Sort option: " << *sortOption << std::endl;
        auto print_debug_date = [](const wchar_t* name, const auto& optDate) {
            if(optDate){
                auto tt = std::chrono::system_clock::to_time_t(*optDate);
                std::tm tminfo;
                localtime_s(&tminfo, &tt);
                wchar_t buf[25];
                wcsftime(buf, sizeof(buf)/sizeof(wchar_t), L"%Y-%m-%d %H:%M:%S", &tminfo);
                std::wcout << name << buf << std::endl;
            }
        };
        print_debug_date(L"Date created start: ", dateCreatedStart);
        print_debug_date(L"Date created end:   ", dateCreatedEnd);
        print_debug_date(L"Date modified start: ", dateModifiedStart);
        print_debug_date(L"Date modified end:   ", dateModifiedEnd);
    }

    std::vector<FileInfo> results = FileFinder::findFiles(directory, pattern, useRegex, shallow, debug, pathMatchMode);
    if (dateCreatedStart || dateCreatedEnd || dateModifiedStart || dateModifiedEnd) {
        results = filterFilesByDate(results, dateCreatedStart, dateCreatedEnd, dateModifiedStart, dateModifiedEnd);
    }
    if (sortOption) {
        sortFiles(results, parseSortOptions(*sortOption));
    }

    bool isExecutingCommand = command.has_value();
    bool isDryRunExecute = isExecutingCommand && dryRunMode;

    if (!isExecutingCommand && !bareMode) {
        if (verboseMode && conciseMode) { // Global headers for verbose-concise
             if (!conciseMode) printColumnHeaders(singleTabMode, true);
        } else if (!verboseMode && !conciseMode) { // Global headers for normal non-verbose
            printColumnHeaders(singleTabMode, false);
        }
        // Normal verbose prints headers per-directory. Concise non-verbose prints no headers.
    }

    if (isExecutingCommand && !bareMode) {
        std::wcout << L"Executing" << (isDryRunExecute ? L" (dry run)" : L"") << std::endl;
        std::wcout << std::wstring(isDryRunExecute ? 19 : 9, L'-') << std::endl;
    }

    if (verboseMode && !isExecutingCommand) {
        printFilesVerbose(results, singleTabMode, conciseMode, bareMode);
    } else {
        for (const auto& file : results) {
            if (isExecutingCommand) {
                if (!executeCommand(*command, file, dryRunMode, debug)) anyCommandFailed = true;
            } else {
                printFileInfo(file, singleTabMode, bareMode, false, conciseMode, L"", L"");
            }
        }
    }

    if (isDryRunExecute) {
        std::wcout << L"Dry run: " << results.size() << L" commands would be generated." << std::endl;
    } else if (isExecutingCommand) {
        std::wcout << results.size() << L" files processed for command execution." << std::endl;
        if (anyCommandFailed) std::wcout << L"One or more command executions failed." << std::endl;
    } else if (!conciseMode) {
        if (!verboseMode || (verboseMode && conciseMode)) { // Print summary if not normal-verbose
             // For non-verbose & non-concise OR verbose-concise modes, print summary separator
            const int totalWidth = getConsoleWidth();
            const int sizeCol = 10, createdCol = 16, modifiedCol = 16, spacing = 2;
            int pathCol = totalWidth - sizeCol - createdCol - modifiedCol - (spacing * 3);
            bool useVerboseHeaderForSummarySep = verboseMode && conciseMode;
            
            if (singleTabMode) {
                 std::wcout << std::wstring(useVerboseHeaderForSummarySep ? 9 : 10, L'-') << L'\t' // Directory or Path
                            << (useVerboseHeaderForSummarySep ? std::wstring(8, L'-') + L'\t' : L"") // Filename (only in verbose-concise)
                            << std::wstring(8, L'-') << L'\t'  // Size
                            << std::wstring(15, L'-') << L'\t' // Created
                            << std::wstring(15, L'-') << std::endl;    // Modified
            } else {
                if(useVerboseHeaderForSummarySep){
                    const int dirCol = 40;
                    int fileCol = pathCol - dirCol - spacing; 
                    if (fileCol < 10) fileCol = 10; 
                     std::wcout << std::wstring(dirCol, L'-')
                               << std::wstring(spacing, L' ') << std::wstring(fileCol, L'-');
                } else {
                    std::wcout << std::wstring(pathCol, L'-');
                }
                 std::wcout << std::wstring(spacing, L' ') << std::wstring(sizeCol, L'-')
                           << std::wstring(spacing, L' ') << std::wstring(createdCol, L'-')
                           << std::wstring(spacing, L' ') << std::wstring(modifiedCol, L'-')
                           << std::endl;
            }
        }
        if (!verboseMode || conciseMode) { // Avoid global summary for normal verbose
            std::wcout << L"Found " << results.size() << L" files" << std::endl;
        }
    }

    LocalFree(argv_w);
    return anyCommandFailed ? 1 : 0;
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
#include <shellapi.h>
#include <map>

#endif //PCH_H
*/ 


