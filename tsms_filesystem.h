#ifndef TSMS_FILESYSTEM_H
#define TSMS_FILESYSTEM_H

#include "tsms.h"
#include "tsms_math.h"
#include "tsms_string.h"
#include "tsms_long_list.h"
#include "tsms_long_map.h"
#include <unistd.h>

#define TSMS_FILE_NAME_MAX_LENGTH 255
#define TSMS_FILE_UNIT 8
#define TSMS_FILE_HEADER_BLOCK 512
#define TSMS_FILE_CONTENT_BLOCK 4096
extern const uint32_t TSMS_FILE_MAGIC;

typedef enum {
	TSMS_FILE_TYPE_FILE = 0,
	TSMS_FILE_TYPE_FOLDER
} TSMS_FILE_TYPE;

typedef uint8_t TSMS_FILE_OPTION;

struct TSMS_FILE_HANDLER;

typedef struct TSMS_FILE_HANDLER tFile;
typedef tFile* pFile;

struct TSMS_FILESYSTEM;

typedef struct TSMS_FILESYSTEM tFilesystem;
typedef tFilesystem* pFilesystem;

struct TSMS_FILE_HANDLER {
	pString name; // max 255 characters
	pFile parent;
	pFilesystem filesystem;
	TSMS_FILE_OPTION options[4];
	TSMS_LLLP anchors;
	TSMS_LMP files; // for folder
	TSMS_LLLP blocks; // for file
	bool loaded;
	TSMS_LSIZE size;
	TSMS_SIZE level;
#ifdef TSMS_STM32
#else
	TSMS_POS __native_offset;
	// can be access by parent file
#endif
};

struct TSMS_FILESYSTEM {
	pString split;
	pFile root;
#ifdef TSMS_STM32
#else
	FILE* __native_file;
#endif
};

extern pFilesystem defaultFilesystem;

TSMS_RESULT TSMS_FILESYSTEM_init(TSMS_CLOCK_FREQUENCY frequency);

TSMS_RESULT TSMS_FILESYSTEM_setDefaultFilesystem(pFilesystem filesystem);

pFilesystem TSMS_FILESYSTEM_createFilesystem(char split);

pFile TSMS_FILESYSTEM_createFolder(pFile parent, pString name, uint8_t * options);

pFile TSMS_FILESYSTEM_createFile(pFile parent, pString name, uint8_t * options);

TSMS_RESULT TSMS_FILESYSTEM_release(pFilesystem filesystem);

bool TSMS_FILESYSTEM_isFolder(pFile file);

pFile TSMS_FILESYSTEM_getFile(pFile file, pString name);

pString TSMS_FILESYSTEM_readFile(pFile file);

pString TSMS_FILESYSTEM_readPartialFile(pFile file, TSMS_POS start, TSMS_POS end);

TSMS_RESULT TSMS_FILESYSTEM_writeFile(pFile file, pString content);

TSMS_RESULT TSMS_FILESYSTEM_insertFile(pFile file, pString content, TSMS_POS pos);

TSMS_RESULT TSMS_FILESYSTEM_emptyFile(pFile file);

pString TSMS_FILESYSTEM_getPath(pFile file);

TSMS_LSIZE TSMS_FILESYSTEM_size(pFile file);

TSMS_LP TSMS_FILESYSTEM_list(pFile file);

TSMS_RESULT TSMS_FILESYSTEM_deleteFile(pFile file);

TSMS_RESULT TSMS_FILESYSTEM_deleteFolder(pFile file);

pFile TSMS_FILESYSTEM_resolve(pFile current, pString path);

TSMS_RESULT TSMS_FILESYSTEM_rename(pFile file, pString name);

bool TSMS_FILESYSTEM_isParent(pFile parent, pFile child);

TSMS_RESULT TSMS_FILESYSTEM_move(pFile file, pFile dir);

TSMS_RESULT TSMS_FILESYSTEM_copy(pFile file, pFile dir);


#endif //TSMS_FILESYSTEM_H
