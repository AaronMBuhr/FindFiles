# FindFiles

A powerful command-line utility for searching files with flexible output formatting.

## Features

- Fast recursive file search
- Supports both DOS wildcard patterns and regular expressions
- Customizable output formats with:
  - Tab-separated values (for easy parsing by other tools)
  - Formatted columns with automatic width adjustment
  - Bare path output for scripting
- Flexible sorting options by path, name, size, or date
- Execute commands on found files
- Adjusts display to console width
- Case-insensitive matching

## Installation

### Prerequisites
- Windows operating system
- Visual Studio 2019 or newer with C++ desktop development workload

### Building from Source
1. Clone this repository
2. Open `FindFiles.sln` in Visual Studio
3. Build the solution in Release mode

## Usage

```
FindFiles.exe <directory> <pattern> [options]
```

### Parameters

- `<directory>`: Directory to search in
- `<pattern>`: File pattern to match (supports wildcards like *.txt)

### Options

- `-r, --regex`: Treat pattern as regex instead of DOS wildcard
- `-s, --shallow`: Shallow search (do not recurse into subdirectories)
- `-x, --execute "cmd"`: Execute command on each found file
  - `%d` = directory, `%n` = filename, `%f` = full path
- `-d, --debug`: Show detailed debug information during the search
- `-t, --tab`: Use tab-separated output (better for parsing)
- `-c, --concise`: Display results without headers or summary
- `-b, --bare`: Display only file paths (implies --concise)
- `--sort <order>`: Sort results by specified criteria
  - `p` = path (full path)
  - `n` = name (filename only)
  - `s` = size
  - `c` = creation date
  - `m` = modification date
  - Prefix any option with `-` for descending order (e.g., `-np` for descending name, then path)
  - Multiple criteria can be combined (e.g., `ns` for name then size)
- `-h, --help`: Display help message

## Examples

Find all .cpp files in the current directory and subdirectories:
```
FindFiles.exe . "*.cpp"
```

Find all .txt files in D:\Documents, without recursing into subdirectories:
```
FindFiles.exe D:\Documents "*.txt" -s
```

Find all files matching a regular expression pattern:
```
FindFiles.exe . "test.*\.log" -r
```

Find all .jpg files and sort them by filename:
```
FindFiles.exe . "*.jpg" --sort n
```

Find all .log files and sort by size (largest first), then by modification date (newest first):
```
FindFiles.exe . "*.log" --sort -s-m
```

Output tab-separated values with full paths for processing:
```
FindFiles.exe . "*.exe" -t
```

Display only file paths for use in scripts:
```
FindFiles.exe . "*.dll" -b
```

Execute a command on each found file:
```
FindFiles.exe . "*.jpg" -x "copy %f D:\backup\"
```

## License

This project is licensed under the MIT License - see the LICENSE file for details. 