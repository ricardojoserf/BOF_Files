#include <windows.h>
#include "beacon.h"
#include <stdio.h>
#include <string.h>

DECLSPEC_IMPORT HANDLE WINAPI KERNEL32$GetProcessHeap();
DECLSPEC_IMPORT LPVOID WINAPI KERNEL32$HeapAlloc(HANDLE, DWORD, SIZE_T);
DECLSPEC_IMPORT BOOL   WINAPI KERNEL32$HeapFree(HANDLE, DWORD, LPVOID);
DECLSPEC_IMPORT HANDLE WINAPI KERNEL32$CreateFileA(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
DECLSPEC_IMPORT BOOL   WINAPI KERNEL32$WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
DECLSPEC_IMPORT BOOL   WINAPI KERNEL32$CloseHandle(HANDLE);
DECLSPEC_IMPORT BOOL   WINAPI KERNEL32$SetFilePointer(HANDLE, LONG, PLONG, DWORD);
DECLSPEC_IMPORT BOOL WINAPI KERNEL32$ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);



// New ZipFile structure
typedef struct {
    char* filename;
    char* content;
} ZipFile;

#pragma pack(push, 1)  // Disable padding

// uint32_t -> int
// uint16_t -> short
// uint8_t  -> byte

// Local File Header (without the filename)
struct LocalFileHeader {
    int signature;           // 0x04034b50
    short version;             // Version needed to extract
    short flag;                // General purpose bit flag
    short compression;         // Compression method (0 = no compression)
    short modTime;             // Last mod file time
    short modDate;             // Last mod file date
    int crc32;               // CRC-32
    int compressedSize;      // Compressed size
    int uncompressedSize;    // Uncompressed size
    short filenameLength;      // Filename length
    short extraFieldLength;    // Extra field length
};


// Central Directory Header (without the filename)
struct CentralDirectoryHeader {
    int signature;           // 0x02014b50
    short versionMadeBy;       // Version made by
    short versionNeeded;       // Version needed to extract
    short flag;                // General purpose bit flag
    short compression;         // Compression method
    short modTime;             // Last mod file time
    short modDate;             // Last mod file date
    int crc32;               // CRC-32
    int compressedSize;      // Compressed size
    int uncompressedSize;    // Uncompressed size
    short filenameLength;      // Filename length
    short extraFieldLength;    // Extra field length
    short commentLength;       // File comment length
    short diskNumberStart;     // Disk number where file starts
    short internalFileAttr;    // Internal file attributes
    int externalFileAttr;    // External file attributes
    int relativeOffset;      // Offset of local header
};


// End of Central Directory Record
struct EndOfCentralDirectory {
    int signature;           // 0x06054b50
    short diskNumber;          // Number of this disk
    short centralDirDisk;      // Number of the disk with the start of the central directory
    short numEntriesOnDisk;    // Number of entries in the central directory on this disk
    short totalEntries;        // Total number of entries in the central directory
    int centralDirSize;      // Size of the central directory
    int centralDirOffset;    // Offset of start of central directory, relative to start of archive
    short commentLength;       // ZIP file comment length
};


#pragma pack(pop)  // Enable padding


short getDosTime() {
    return (12 << 11) | (0 << 5) | (0 / 2);  // Example: 12:00:00 (noon)
}


short getDosDate() {
    return (2024 - 1980) << 9 | (9 << 5) | 28;  // Example: September 28, 2024
}


// CRC32 calculation (fixed)
int crc32(const char* data, size_t length) {
    unsigned int crc = 0xFFFFFFFF;  // Use unsigned for correct CRC-32 behavior

    for (size_t i = 0; i < length; i++) {
        crc ^= (unsigned char)data[i];  // Cast each byte to unsigned to avoid sign issues
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;  // Apply polynomial
            } else {
                crc >>= 1;
            }
        }
    }

    // Final bitwise inversion, but return as signed int
    int result = ~crc;
    return result;
}


// Function to create a ZIP file
void create_zip(const char* zip_fname, ZipFile* zip_files, int file_count) {
    HANDLE hZipFile;
    DWORD writtenBytes = 0;

    // Open or create the ZIP file using CreateFile
    hZipFile = KERNEL32$CreateFileA(zip_fname, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hZipFile == INVALID_HANDLE_VALUE) {
        // printf("Failed to open zip file.\n");
        return;
    }

    int centralDirSize = 0;
    int centralDirOffset = 0;
    long centralDirStart = 0;

    // Allocate memory for the central directory headers
    HANDLE hHeap = KERNEL32$GetProcessHeap();
    struct CentralDirectoryHeader* centralHeaders = (struct CentralDirectoryHeader*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, file_count * sizeof(struct CentralDirectoryHeader));
    if (centralHeaders == NULL) {
        KERNEL32$CloseHandle(hZipFile);
        return;
    }

    // Process each file in the zip_files array
    for (int i = 0; i < file_count; i++) {
        const char* filename = zip_files[i].filename;
        const char* fileContent = zip_files[i].content;
        size_t fileSize = MyStrLen(fileContent);

        // Initialize the local file header
        struct LocalFileHeader localHeader;
        localHeader.signature = 0x04034b50;
        localHeader.version = 20;  // Version needed to extract (2.0)
        localHeader.flag = 0;
        localHeader.compression = 0;  // No compression
        localHeader.modTime = getDosTime();  // Define getDosTime() and getDosDate() to return time values
        localHeader.modDate = getDosDate();
        localHeader.crc32 = crc32(fileContent, fileSize);  // Define crc32() function to calculate CRC
        localHeader.compressedSize = fileSize;
        localHeader.uncompressedSize = fileSize;
        localHeader.filenameLength = MyStrLen(filename);
        localHeader.extraFieldLength = 0;

        // Capture the local file header offset
        DWORD localFileOffset = KERNEL32$SetFilePointer(hZipFile, 0, NULL, FILE_CURRENT);

        // Write local file header
        KERNEL32$WriteFile(hZipFile, &localHeader, sizeof(localHeader), &writtenBytes, NULL);
        KERNEL32$WriteFile(hZipFile, filename, MyStrLen(filename), &writtenBytes, NULL);
        KERNEL32$WriteFile(hZipFile, fileContent, fileSize, &writtenBytes, NULL);

        // Prepare the central directory header for this file
        struct CentralDirectoryHeader centralHeader;
        centralHeader.signature = 0x02014b50;
        centralHeader.versionMadeBy = 20;
        centralHeader.versionNeeded = 20;
        centralHeader.flag = 0;
        centralHeader.compression = 0;
        centralHeader.modTime = localHeader.modTime;
        centralHeader.modDate = localHeader.modDate;
        centralHeader.crc32 = localHeader.crc32;
        centralHeader.compressedSize = localHeader.compressedSize;
        centralHeader.uncompressedSize = localHeader.uncompressedSize;
        centralHeader.filenameLength = localHeader.filenameLength;
        centralHeader.extraFieldLength = 0;
        centralHeader.commentLength = 0;
        centralHeader.diskNumberStart = 0;
        centralHeader.internalFileAttr = 0;
        centralHeader.externalFileAttr = 0;
        centralHeader.relativeOffset = localFileOffset;

        // Store the central directory header
        centralHeaders[i] = centralHeader;
        centralDirSize += sizeof(centralHeader) + MyStrLen(filename);
    }

    // Capture the central directory start offset
    centralDirStart = KERNEL32$SetFilePointer(hZipFile, 0, NULL, FILE_CURRENT);

    // Write the central directory headers for all files
    for (int i = 0; i < file_count; i++) {
        KERNEL32$WriteFile(hZipFile, &centralHeaders[i], sizeof(centralHeaders[i]), &writtenBytes, NULL);
        KERNEL32$WriteFile(hZipFile, zip_files[i].filename, MyStrLen(zip_files[i].filename), &writtenBytes, NULL);
    }

    // Initialize the end of central directory record
    struct EndOfCentralDirectory eocd;
    eocd.signature = 0x06054b50;
    eocd.diskNumber = 0;
    eocd.centralDirDisk = 0;
    eocd.numEntriesOnDisk = file_count;
    eocd.totalEntries = file_count;
    eocd.centralDirSize = centralDirSize;
    eocd.centralDirOffset = centralDirStart;
    eocd.commentLength = 0;

    // Write end of central directory record
    KERNEL32$WriteFile(hZipFile, &eocd, sizeof(eocd), &writtenBytes, NULL);

    // Clean up
    KERNEL32$HeapFree(hHeap, 0, centralHeaders);
    KERNEL32$CloseHandle(hZipFile);
    BeaconPrintf(CALLBACK_OUTPUT, "[+] ZIP file %s generated.\n", zip_fname);
}


// Custom string copy function
void MyStrcpy(char* dest, const char* src, size_t max_len) {
    size_t i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}


// Custom string length function
int MyStrLen(char *str) {
    int len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}


// Function to create a ZipFile object with dynamically allocated filename and content
ZipFile createZipFile(const char* fname, const char* fcontent) {
    ZipFile zip_file;

    // Get lengths using MyStrLen
    int fname_len = MyStrLen((char*)fname);
    int fcontent_len = MyStrLen((char*)fcontent);

    HANDLE hHeap = KERNEL32$GetProcessHeap();
    zip_file.filename = (char*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, fname_len + 1);
    zip_file.content = (char*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, fcontent_len + 1);

    // Use MyStrcpy to copy filename and content
    MyStrcpy(zip_file.filename, fname, fname_len + 1);  // Ensure max_len includes null terminator
    MyStrcpy(zip_file.content, fcontent, fcontent_len + 1);

    return zip_file;
}


/*
// Function to create a nested ZIP file containing a text file with any content
void create_nested_zip(const char* outer_zip_name, const char* inner_zip_name, const char* inner_file_name, const char* inner_file_content) {
    HANDLE hHeap = KERNEL32$GetProcessHeap();

    // Step 1: Create the inner ZIP file (in-memory)
    ZipFile* inner_zip_files = (ZipFile*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(ZipFile));
    if (!inner_zip_files) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to allocate memory for inner_zip_files.");
        return;
    }

    // Create a ZipFile structure for the inner ZIP
    inner_zip_files[0] = createZipFile(inner_file_name, inner_file_content);

    // Inner ZIP filename and buffer to hold ZIP data
    const char* inner_zip_memory_name = "inner.zip";

    // Call create_zip to generate the inner ZIP file
    create_zip(inner_zip_memory_name, inner_zip_files, 1);

    // Step 2: Read the inner ZIP file and treat it as the content for the outer ZIP
    HANDLE hInnerZipFile = KERNEL32$CreateFileA(inner_zip_memory_name, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hInnerZipFile == INVALID_HANDLE_VALUE) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to open inner ZIP file.");
        return;
    }

    DWORD inner_zip_size = KERNEL32$SetFilePointer(hInnerZipFile, 0, NULL, FILE_END);
    KERNEL32$SetFilePointer(hInnerZipFile, 0, NULL, FILE_BEGIN);

    char* inner_zip_content = (char*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, inner_zip_size);
    DWORD bytesRead = 0;
    KERNEL32$ReadFile(hInnerZipFile, inner_zip_content, inner_zip_size, &bytesRead, NULL);
    KERNEL32$CloseHandle(hInnerZipFile);

    // Step 3: Create the outer ZIP file and add the inner ZIP as a file
    ZipFile* outer_zip_files = (ZipFile*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(ZipFile));
    if (!outer_zip_files) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to allocate memory for outer_zip_files.");
        return;
    }

    // Create a ZipFile structure for the outer ZIP containing the inner ZIP
    outer_zip_files[0] = createZipFile(inner_zip_name, inner_zip_content);

    // Call create_zip to generate the outer ZIP file
    create_zip(outer_zip_name, outer_zip_files, 1);

    // Cleanup
    KERNEL32$HeapFree(hHeap, 0, inner_zip_content);
    KERNEL32$HeapFree(hHeap, 0, inner_zip_files);
    KERNEL32$HeapFree(hHeap, 0, outer_zip_files);
}
*/

/*
// Function to create an in-memory ZIP file and return its content and size
char* create_zip_in_memory(ZipFile* zip_files, int file_count, DWORD* out_zip_size) {
    HANDLE hHeap = KERNEL32$GetProcessHeap();
    char* zip_buffer = NULL;
    DWORD buffer_size = 0;
    DWORD written_bytes = 0;

    // Use a dynamic memory buffer to hold the ZIP data (instead of writing to disk)
    HANDLE hMemBuffer = KERNEL32$CreateFileA("CON", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);  // "CON" is a trick to get a memory buffer

    if (hMemBuffer == INVALID_HANDLE_VALUE) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to create in-memory file.");
        return NULL;
    }

    // Similar to the previous create_zip function, but writing to the memory buffer
    int centralDirSize = 0;
    int centralDirOffset = 0;
    long centralDirStart = 0;

    struct CentralDirectoryHeader* centralHeaders = (struct CentralDirectoryHeader*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, file_count * sizeof(struct CentralDirectoryHeader));
    if (centralHeaders == NULL) {
        KERNEL32$CloseHandle(hMemBuffer);
        return NULL;
    }

    for (int i = 0; i < file_count; i++) {
        const char* filename = zip_files[i].filename;
        const char* fileContent = zip_files[i].content;
        size_t fileSize = MyStrLen(fileContent);

        struct LocalFileHeader localHeader;
        localHeader.signature = 0x04034b50;
        localHeader.version = 20;
        localHeader.flag = 0;
        localHeader.compression = 0;
        localHeader.modTime = getDosTime();
        localHeader.modDate = getDosDate();
        localHeader.crc32 = crc32(fileContent, fileSize);
        localHeader.compressedSize = fileSize;
        localHeader.uncompressedSize = fileSize;
        localHeader.filenameLength = MyStrLen(filename);
        localHeader.extraFieldLength = 0;

        DWORD localFileOffset = KERNEL32$SetFilePointer(hMemBuffer, 0, NULL, FILE_CURRENT);

        // Write local file header
        KERNEL32$WriteFile(hMemBuffer, &localHeader, sizeof(localHeader), &written_bytes, NULL);
        KERNEL32$WriteFile(hMemBuffer, filename, MyStrLen(filename), &written_bytes, NULL);
        KERNEL32$WriteFile(hMemBuffer, fileContent, fileSize, &written_bytes, NULL);

        struct CentralDirectoryHeader centralHeader;
        centralHeader.signature = 0x02014b50;
        centralHeader.versionMadeBy = 20;
        centralHeader.versionNeeded = 20;
        centralHeader.flag = 0;
        centralHeader.compression = 0;
        centralHeader.modTime = localHeader.modTime;
        centralHeader.modDate = localHeader.modDate;
        centralHeader.crc32 = localHeader.crc32;
        centralHeader.compressedSize = localHeader.compressedSize;
        centralHeader.uncompressedSize = localHeader.uncompressedSize;
        centralHeader.filenameLength = localHeader.filenameLength;
        centralHeader.extraFieldLength = 0;
        centralHeader.commentLength = 0;
        centralHeader.diskNumberStart = 0;
        centralHeader.internalFileAttr = 0;
        centralHeader.externalFileAttr = 0;
        centralHeader.relativeOffset = localFileOffset;

        centralHeaders[i] = centralHeader;
        centralDirSize += sizeof(centralHeader) + MyStrLen(filename);
    }

    centralDirStart = KERNEL32$SetFilePointer(hMemBuffer, 0, NULL, FILE_CURRENT);

    for (int i = 0; i < file_count; i++) {
        KERNEL32$WriteFile(hMemBuffer, &centralHeaders[i], sizeof(centralHeaders[i]), &written_bytes, NULL);
        KERNEL32$WriteFile(hMemBuffer, zip_files[i].filename, MyStrLen(zip_files[i].filename), &written_bytes, NULL);
    }

    struct EndOfCentralDirectory eocd;
    eocd.signature = 0x06054b50;
    eocd.diskNumber = 0;
    eocd.centralDirDisk = 0;
    eocd.numEntriesOnDisk = file_count;
    eocd.totalEntries = file_count;
    eocd.centralDirSize = centralDirSize;
    eocd.centralDirOffset = centralDirStart;
    eocd.commentLength = 0;

    KERNEL32$WriteFile(hMemBuffer, &eocd, sizeof(eocd), &written_bytes, NULL);

    // Get the total ZIP size by querying the file pointer
    DWORD total_zip_size = KERNEL32$SetFilePointer(hMemBuffer, 0, NULL, FILE_CURRENT);

    // Allocate memory to hold the ZIP content
    zip_buffer = (char*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, total_zip_size);

    if (!zip_buffer) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to allocate memory for ZIP content.");
        KERNEL32$CloseHandle(hMemBuffer);
        return NULL;
    }

    // Read the ZIP content back into memory
    KERNEL32$SetFilePointer(hMemBuffer, 0, NULL, FILE_BEGIN);
    KERNEL32$ReadFile(hMemBuffer, zip_buffer, total_zip_size, &written_bytes, NULL);

    KERNEL32$CloseHandle(hMemBuffer);
    KERNEL32$HeapFree(hHeap, 0, centralHeaders);

    *out_zip_size = total_zip_size;
    return zip_buffer;
}
*/

/*
// Updated nested ZIP function that works entirely in memory
void create_nested_zip_in_memory(const char* outer_zip_name, const char* inner_zip_name, const char* inner_file_name, const char* inner_file_content) {
    HANDLE hHeap = KERNEL32$GetProcessHeap();

    // Step 1: Create the inner ZIP file in memory
    ZipFile* inner_zip_files = (ZipFile*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(ZipFile));
    if (!inner_zip_files) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to allocate memory for inner_zip_files.");
        return;
    }

    inner_zip_files[0] = createZipFile(inner_file_name, inner_file_content);

    // Create the inner ZIP file in memory
    DWORD inner_zip_size = 0;
    char* inner_zip_content = create_zip_in_memory(inner_zip_files, 1, &inner_zip_size);
    if (!inner_zip_content) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to create inner ZIP file in memory.");
        return;
    }

    // Step 2: Create the outer ZIP file and add the inner ZIP as a file (in memory)
    ZipFile* outer_zip_files = (ZipFile*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(ZipFile));
    if (!outer_zip_files) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to allocate memory for outer_zip_files.");
        return;
    }

    outer_zip_files[0] = createZipFile(inner_zip_name, inner_zip_content);

    // Create the outer ZIP file in memory
    DWORD outer_zip_size = 0;
    char* outer_zip_content = create_zip_in_memory(outer_zip_files, 1, &outer_zip_size);
    if (!outer_zip_content) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to create outer ZIP file in memory.");
        return;
    }

    // Write the outer ZIP file to disk (or handle as needed)
    HANDLE hZipFile = KERNEL32$CreateFileA(outer_zip_name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hZipFile != INVALID_HANDLE_VALUE) {
        DWORD written_bytes = 0;
        KERNEL32$WriteFile(hZipFile, outer_zip_content, outer_zip_size, &written_bytes, NULL);
        KERNEL32$CloseHandle(hZipFile);
    }

    // Cleanup
    KERNEL32$HeapFree(hHeap, 0, inner_zip_content);
    KERNEL32$HeapFree(hHeap, 0, outer_zip_content);
    KERNEL32$HeapFree(hHeap, 0, inner_zip_files);
    KERNEL32$HeapFree(hHeap, 0, outer_zip_files);
}


// Function to create an in-memory ZIP file and return its content and size
char* create_zip_in_memory(ZipFile* zip_files, int file_count, DWORD* out_zip_size) {
    HANDLE hHeap = KERNEL32$GetProcessHeap();
    char* zip_buffer = NULL;
    DWORD buffer_size = 0;
    DWORD written_bytes = 0;

    // Use a dynamic memory buffer to hold the ZIP data (instead of writing to disk)
    HANDLE hMemBuffer = KERNEL32$CreateFileA("CON", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);  // "CON" is a trick to get a memory buffer

    if (hMemBuffer == INVALID_HANDLE_VALUE) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to create in-memory file.");
        return NULL;
    }

    // Similar to the previous create_zip function, but writing to the memory buffer
    int centralDirSize = 0;
    int centralDirOffset = 0;
    long centralDirStart = 0;

    struct CentralDirectoryHeader* centralHeaders = (struct CentralDirectoryHeader*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, file_count * sizeof(struct CentralDirectoryHeader));
    if (centralHeaders == NULL) {
        KERNEL32$CloseHandle(hMemBuffer);
        return NULL;
    }

    for (int i = 0; i < file_count; i++) {
        const char* filename = zip_files[i].filename;
        const char* fileContent = zip_files[i].content;
        size_t fileSize = MyStrLen(fileContent);

        struct LocalFileHeader localHeader;
        localHeader.signature = 0x04034b50;
        localHeader.version = 20;
        localHeader.flag = 0;
        localHeader.compression = 0;
        localHeader.modTime = getDosTime();
        localHeader.modDate = getDosDate();
        localHeader.crc32 = crc32(fileContent, fileSize);
        localHeader.compressedSize = fileSize;
        localHeader.uncompressedSize = fileSize;
        localHeader.filenameLength = MyStrLen(filename);
        localHeader.extraFieldLength = 0;

        DWORD localFileOffset = KERNEL32$SetFilePointer(hMemBuffer, 0, NULL, FILE_CURRENT);

        // Write local file header
        KERNEL32$WriteFile(hMemBuffer, &localHeader, sizeof(localHeader), &written_bytes, NULL);
        KERNEL32$WriteFile(hMemBuffer, filename, MyStrLen(filename), &written_bytes, NULL);
        KERNEL32$WriteFile(hMemBuffer, fileContent, fileSize, &written_bytes, NULL);

        struct CentralDirectoryHeader centralHeader;
        centralHeader.signature = 0x02014b50;
        centralHeader.versionMadeBy = 20;
        centralHeader.versionNeeded = 20;
        centralHeader.flag = 0;
        centralHeader.compression = 0;
        centralHeader.modTime = localHeader.modTime;
        centralHeader.modDate = localHeader.modDate;
        centralHeader.crc32 = localHeader.crc32;
        centralHeader.compressedSize = localHeader.compressedSize;
        centralHeader.uncompressedSize = localHeader.uncompressedSize;
        centralHeader.filenameLength = localHeader.filenameLength;
        centralHeader.extraFieldLength = 0;
        centralHeader.commentLength = 0;
        centralHeader.diskNumberStart = 0;
        centralHeader.internalFileAttr = 0;
        centralHeader.externalFileAttr = 0;
        centralHeader.relativeOffset = localFileOffset;

        centralHeaders[i] = centralHeader;
        centralDirSize += sizeof(centralHeader) + MyStrLen(filename);
    }

    centralDirStart = KERNEL32$SetFilePointer(hMemBuffer, 0, NULL, FILE_CURRENT);

    for (int i = 0; i < file_count; i++) {
        KERNEL32$WriteFile(hMemBuffer, &centralHeaders[i], sizeof(centralHeaders[i]), &written_bytes, NULL);
        KERNEL32$WriteFile(hMemBuffer, zip_files[i].filename, MyStrLen(zip_files[i].filename), &written_bytes, NULL);
    }

    struct EndOfCentralDirectory eocd;
    eocd.signature = 0x06054b50;
    eocd.diskNumber = 0;
    eocd.centralDirDisk = 0;
    eocd.numEntriesOnDisk = file_count;
    eocd.totalEntries = file_count;
    eocd.centralDirSize = centralDirSize;
    eocd.centralDirOffset = centralDirStart;
    eocd.commentLength = 0;

    KERNEL32$WriteFile(hMemBuffer, &eocd, sizeof(eocd), &written_bytes, NULL);

    // Get the total ZIP size by querying the file pointer
    DWORD total_zip_size = KERNEL32$SetFilePointer(hMemBuffer, 0, NULL, FILE_CURRENT);

    // Allocate memory to hold the ZIP content
    zip_buffer = (char*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, total_zip_size);

    if (!zip_buffer) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to allocate memory for ZIP content.");
        KERNEL32$CloseHandle(hMemBuffer);
        return NULL;
    }

    // Read the ZIP content back into memory
    KERNEL32$SetFilePointer(hMemBuffer, 0, NULL, FILE_BEGIN);
    KERNEL32$ReadFile(hMemBuffer, zip_buffer, total_zip_size, &written_bytes, NULL);

    KERNEL32$CloseHandle(hMemBuffer);
    KERNEL32$HeapFree(hHeap, 0, centralHeaders);

    *out_zip_size = total_zip_size;
    return zip_buffer;
}
*/


/*
// Updated nested ZIP function that works entirely in memory
void create_nested_zip_in_memory(const char* outer_zip_name, const char* inner_zip_name, const char* inner_file_name, const char* inner_file_content) {
    HANDLE hHeap = KERNEL32$GetProcessHeap();

    // Step 1: Create the inner ZIP file in memory
    ZipFile* inner_zip_files = (ZipFile*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(ZipFile));
    if (!inner_zip_files) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to allocate memory for inner_zip_files.");
        return;
    }

    inner_zip_files[0] = createZipFile(inner_file_name, inner_file_content);

    // Create the inner ZIP file in memory
    DWORD inner_zip_size = 0;
    char* inner_zip_content = create_zip_in_memory(inner_zip_files, 1, &inner_zip_size);
    if (!inner_zip_content) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to create inner ZIP file in memory.");
        return;
    }

    // Step 2: Create the outer ZIP file and add the inner ZIP as a file (in memory)
    ZipFile* outer_zip_files = (ZipFile*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(ZipFile));
    if (!outer_zip_files) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to allocate memory for outer_zip_files.");
        return;
    }

    outer_zip_files[0] = createZipFile(inner_zip_name, inner_zip_content);

    // Create the outer ZIP file in memory
    DWORD outer_zip_size = 0;
    char* outer_zip_content = create_zip_in_memory(outer_zip_files, 1, &outer_zip_size);
    if (!outer_zip_content) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to create outer ZIP file in memory.");
        return;
    }

    // Write the outer ZIP file to disk (or handle as needed)
    HANDLE hZipFile = KERNEL32$CreateFileA(outer_zip_name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hZipFile != INVALID_HANDLE_VALUE) {
        DWORD written_bytes = 0;
        KERNEL32$WriteFile(hZipFile, outer_zip_content, outer_zip_size, &written_bytes, NULL);
        KERNEL32$CloseHandle(hZipFile);
    }

    // Cleanup
    KERNEL32$HeapFree(hHeap, 0, inner_zip_content);
    KERNEL32$HeapFree(hHeap, 0, outer_zip_content);
    KERNEL32$HeapFree(hHeap, 0, inner_zip_files);
    KERNEL32$HeapFree(hHeap, 0, outer_zip_files);
}
*/

void go(char *args, int length) {
    /*
    */
    char* zip_name = "output.zip";
    const int files_number = 3;
    HANDLE hHeap = KERNEL32$GetProcessHeap();
    ZipFile* zip_files = (ZipFile*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(ZipFile) * files_number);
    if (!zip_files) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to allocate memory for zip_files.");
        return;
    }

    zip_files[0] = createZipFile("test1.txt", "test 1");
    zip_files[1] = createZipFile("test2.txt", "test 2");
    zip_files[2] = createZipFile("test3.txt", "test 3");

    // Create a ZIP file with one element
    create_zip(zip_name, zip_files, files_number);

    /*
    char* outer_zip_name = "nested_output.zip";
    char* inner_zip_name = "inner.zip";
    char* inner_file_name = "nested_test.txt";
    char* inner_file_content = "This is the content of the nested text file.";

    // Create a nested ZIP file
    create_nested_zip(outer_zip_name, inner_zip_name, inner_file_name, inner_file_content);
    create_nested_zip_in_memory("outer.zip", "inner.zip", "file.txt", "This is the content of the file inside the inner ZIP.");
    */
    return 0;
}