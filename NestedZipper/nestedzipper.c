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
DECLSPEC_IMPORT VOID   WINAPI KERNEL32$RtlCopyMemory(PVOID Destination, CONST VOID *Source, SIZE_T Length);


// New ZipFile structure
typedef struct {
    char* filename;
    unsigned char* content;
    int size;
} ZipFile;

#pragma pack(push, 1)  // Disable padding

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
        size_t fileSize = zip_files[i].size;

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
        KERNEL32$WriteFile(hZipFile, zip_files[i].filename, zip_files[i].size, &writtenBytes, NULL);
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


ZipFile createZipFile(const char* fname, unsigned char* fcontent, int size) {
    ZipFile zip_file;

    // Get length of filename using MyStrLen
    int fname_len = MyStrLen((char*)fname);

    // Allocate memory for the filename and content using heap
    HANDLE hHeap = KERNEL32$GetProcessHeap();
    zip_file.filename = (char*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, fname_len + 1);  // Allocate memory for filename
    zip_file.content = (unsigned char*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, size);  // Allocate memory for binary content

    // Use MyStrcpy to copy the filename (as it's a string)
    MyStrcpy(zip_file.filename, fname, fname_len + 1);  // Ensure max_len includes null terminator

    // Copy binary content (unsigned char) manually, as we can't rely on null terminators
    for (int i = 0; i < size; i++) {
        zip_file.content[i] = fcontent[i];
    }
    zip_file.size = size;  // Set the size of the content
    return zip_file;
}


// Custom function to write data into the memory buffer
int memory_write(char* buffer, size_t* offset, size_t buffer_size, const void* data, size_t data_size) {
    if (*offset + data_size > buffer_size) {
        // Handle buffer overflow (you may resize or report an error here)
        BeaconPrintf(CALLBACK_OUTPUT, "[+] memory_write failed\n");
        return 0; // Return failure
    }
    KERNEL32$RtlCopyMemory(buffer + *offset, data, data_size);  // Copy data into the buffer
    *offset += data_size;
    return 1; // Return success
}


int create_zip_to_memory(char* buffer, size_t* offset, size_t buffer_size, ZipFile* zip_files, int file_count) {
    struct CentralDirectoryHeader* centralHeaders = NULL;
    HANDLE hHeap = KERNEL32$GetProcessHeap();

    // Allocate memory for central directory headers
    centralHeaders = (struct CentralDirectoryHeader*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(struct CentralDirectoryHeader) * file_count);
    if (centralHeaders == NULL) {
        return 0;  // Allocation failure
    }

    size_t centralDirSize = 0;
    size_t centralDirStart = *offset;  // Capture the start of the central directory

    // Process each file in the zip_files array
    for (int i = 0; i < file_count; i++) {
        const char* filename = zip_files[i].filename;
        const char* fileContent = (char*)zip_files[i].content;
        size_t fileSize = zip_files[i].size;

        // Initialize the local file header
        struct LocalFileHeader localHeader;
        localHeader.signature = 0x04034b50;
        localHeader.version = 20;  // Version needed to extract (2.0)
        localHeader.flag = 0;
        localHeader.compression = 0;  // No compression
        localHeader.modTime = getDosTime();
        localHeader.modDate = getDosDate();
        localHeader.crc32 = crc32(fileContent, fileSize);
        localHeader.compressedSize = fileSize;
        localHeader.uncompressedSize = fileSize;
        localHeader.filenameLength = MyStrLen(filename);
        localHeader.extraFieldLength = 0;

        // Write local file header to memory
        if (!memory_write(buffer, offset, buffer_size, &localHeader, sizeof(localHeader))) {
            KERNEL32$HeapFree(hHeap, 0, centralHeaders);
            return 0;  // Failure
        }

        // Write filename to memory
        if (!memory_write(buffer, offset, buffer_size, filename, localHeader.filenameLength)) {
            KERNEL32$HeapFree(hHeap, 0, centralHeaders);
            return 0;  // Failure
        }

        // Write file content to memory
        if (!memory_write(buffer, offset, buffer_size, fileContent, fileSize)) {
            KERNEL32$HeapFree(hHeap, 0, centralHeaders);
            return 0;  // Failure
        }

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
        centralHeader.relativeOffset = centralDirStart;

        // Store the central directory header
        centralHeaders[i] = centralHeader;
        centralDirSize += sizeof(centralHeader) + localHeader.filenameLength;
        centralDirStart = *offset;  // Update the offset for the next file
    }

    // Write the central directory headers for all files to memory
    for (int i = 0; i < file_count; i++) {
        if (!memory_write(buffer, offset, buffer_size, &centralHeaders[i], sizeof(centralHeaders[i]))) {
            KERNEL32$HeapFree(hHeap, 0, centralHeaders);
            return 0;  // Failure
        }

        // Write filename associated with the central directory header
        if (!memory_write(buffer, offset, buffer_size, zip_files[i].filename, centralHeaders[i].filenameLength)) {
            KERNEL32$HeapFree(hHeap, 0, centralHeaders);
            return 0;  // Failure
        }
    }

    // Write the End of Central Directory record
    struct EndOfCentralDirectory eocd;
    eocd.signature = 0x06054b50;
    eocd.diskNumber = 0;
    eocd.centralDirDisk = 0;
    eocd.numEntriesOnDisk = file_count;
    eocd.totalEntries = file_count;
    eocd.centralDirSize = centralDirSize;
    eocd.centralDirOffset = centralDirStart;
    eocd.commentLength = 0;

    if (!memory_write(buffer, offset, buffer_size, &eocd, sizeof(eocd))) {
        KERNEL32$HeapFree(hHeap, 0, centralHeaders);
        return 0;  // Failure
    }

    // Clean up
    KERNEL32$HeapFree(hHeap, 0, centralHeaders);

    return 1;  // Success
}


char* create_inner_zip_in_memory(ZipFile* inner_files, int inner_file_count, size_t* zip_size) {
    HANDLE hHeap = KERNEL32$GetProcessHeap();
    
    // Allocate a buffer for ZIP data (initial size is arbitrary, adjust as needed)
    size_t buffer_size = 1024 * 1024; // 1MB initial size
    char* zip_buffer = (char*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, buffer_size);
    if (zip_buffer == NULL) {
        return NULL;
    }

    // Track the current write position in the buffer
    size_t write_offset = 0;

    // Modify the create_zip function to use memory_write
    if (!create_zip_to_memory(zip_buffer, &write_offset, buffer_size, inner_files, inner_file_count)) {
        // Error handling if memory write fails
        BeaconPrintf(CALLBACK_OUTPUT, "[+] Fail 1\n");
        KERNEL32$HeapFree(hHeap, 0, zip_buffer);
        return NULL;
    }

    // Set the total size of the ZIP data written
    *zip_size = write_offset;

    return zip_buffer;
}


void go(char *args, int length) {
    // Inner ZIP files
    int inner_files_number = 2;
    HANDLE hHeap = KERNEL32$GetProcessHeap();
    ZipFile* inner_files = (ZipFile*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(ZipFile) * inner_files_number);
    char* inner_str1 = "Inner File 1";
    char* inner_str2 = "Inner File 2";
    inner_files[0] = createZipFile("inner1.txt", inner_str1, MyStrLen(inner_str1));
    inner_files[1] = createZipFile("inner2.txt", inner_str2, MyStrLen(inner_str2));
    
    // Get inner ZIP as bytes
    int outer_files_number = 4;
    size_t inner_zip_size;
    unsigned char* inner_zip_bytes = create_inner_zip_in_memory(inner_files, inner_files_number, &inner_zip_size);

    ZipFile* outer_files = (ZipFile*)KERNEL32$HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(ZipFile) * outer_files_number);
    char* zip_name = "output.zip";
    char* nested_zip_name = "nested.zip";
    char* outer_str1 = "Outer File 1";
    char* outer_str2 = "Outer File 2";
    char* outer_str3 = "Outer File 3";
    outer_files[0] = createZipFile("outer1.txt", outer_str1, MyStrLen(outer_str1));
    outer_files[1] = createZipFile("outer2.txt", outer_str2, MyStrLen(outer_str2));
    outer_files[2] = createZipFile("outer3.txt", outer_str3, MyStrLen(outer_str3));
    outer_files[3] = createZipFile(nested_zip_name, inner_zip_bytes, inner_zip_size);
    create_zip(zip_name, outer_files, outer_files_number);
}
