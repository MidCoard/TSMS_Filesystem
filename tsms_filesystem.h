#ifndef TSMS_FILESYSTEM_H
#define TSMS_FILESYSTEM_H

#define TSMS_FILE_NAME_MAX_LENGTH 255
#define TSMS_FILE_UNIT 8
#define TSMS_FILE_HEADER_BLOCK 512
#define TSMS_FILE_CONTENT_BLOCK 4096

#define TSMS_FILESYSTEM_PLATFORM_SENSITIVE

#define TSMS_FILESYSTEM_INTERNAL_OFFSET 0
#define TSMS_FILESYSTEM_HEADER_OFFSET 1048576
#define TSMS_FILESYSTEM_CONTENT_OFFSET 10485760
#define TSMS_FILESYSTEM_AVAILABLE_BLOCK_THRESHOLD 60

#include "tsms_def.h"

extern const uint32_t TSMS_FILE_MAGIC;
extern const uint8_t TSMS_FILE_EMPTY_CONTENT[0];

typedef enum {
	TSMS_FILE_TYPE_FILE = 0,
	TSMS_FILE_TYPE_FOLDER
} TSMS_FILE_TYPE;

typedef enum {
	TSMS_FILE_MODE_READ = 0,
	TSMS_FILE_MODE_WRITE
} TSMS_FILE_MODE;

typedef uint8_t TSMS_FILE_OPTION;

typedef struct TSMS_FILE_HANDLER tFile;
typedef tFile* pFile;

typedef struct TSMS_FILESYSTEM tFilesystem;
typedef tFilesystem* pFilesystem;

typedef struct TSMS_FILESTREAM tFilestream;
typedef tFilestream* pFilestream;

#include "tsms.h"
#include "tsms_math.h"
#include "tsms_string.h"
#include "tsms_long_list.h"
#include "tsms_util.h"
#include "tsms_long_set.h"
#include "tsms_deque.h"
#include "tsms_map.h"

struct TSMS_FILE_HANDLER {
	pFilesystem filesystem;

	TSMS_POS offset;
	pFile parent;
	TSMS_LLP anchors;

	TSMS_SIZE level;
	bool loaded;


	// documented in README.md
	pString name; // max 255 characters
	TSMS_FILE_OPTION options[4];
	TSMS_MP files; // make sure the string is static
	TSMS_LLP blocks; // content blocks
	TSMS_LSIZE size;

	bool dirty;
};

struct TSMS_FILESYSTEM {
	pString split;
	pFile root;
#ifdef TSMS_STM32
#else
	FILE* native;
#endif
	TSMS_POS headerEnd;
	TSMS_POS contentEnd;
	TSMS_DP headerDeque;
	TSMS_DP contentDeque;
};

struct TSMS_FILESTREAM {
	pFile file;
	TSMS_POS pos;
	TSMS_FILE_MODE mode;
};

extern pFilesystem defaultFilesystem;

TSMS_RESULT TSMS_FILESYSTEM_init(TSMS_CLOCK_FREQUENCY frequency);

TSMS_RESULT TSMS_FILESYSTEM_setDefaultFilesystem(pFilesystem filesystem);

pFilesystem TSMS_FILESYSTEM_createFilesystem(char split);

pFile TSMS_FILESYSTEM_createFolder(pFile parent, pString name, uint8_t * options);

pFile TSMS_FILESYSTEM_createFile(pFile parent, pString name, uint8_t * options);

TSMS_RESULT TSMS_FILESYSTEM_releaseFile(pFile file);

TSMS_RESULT TSMS_FILESYSTEM_release(pFilesystem filesystem);

bool TSMS_FILESYSTEM_isFolder(pFile file);

pFile TSMS_FILESYSTEM_getFile(pFile parent, pString name);

uint8_t *TSMS_FILESYSTEM_readFile(pFile file);

uint8_t *TSMS_FILESYSTEM_readPartialFile(pFile file, TSMS_POS start, TSMS_POS end);

TSMS_RESULT TSMS_FILESYSTEM_freeContentBuffer(uint8_t *buffer);

TSMS_RESULT TSMS_FILESYSTEM_writeFile(pFile file, uint8_t *content, TSMS_LSIZE size);

TSMS_RESULT TSMS_FILESYSTEM_insertFile(pFile file, const uint8_t *content, TSMS_POS pos, TSMS_LSIZE size);

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

bool TSMS_FILESYSTEM_contentEquals(pFile file1, pFile file2);

pFilestream TSMS_FILE_open(pFile file);

pFilestream TSMS_FILE_openWithMode(pFile file, TSMS_FILE_MODE mode);

TSMS_RESULT TSMS_FILE_seek(pFilestream stream, TSMS_POS pos);

TSMS_RESULT TSMS_FILE_read(pFilestream stream, uint8_t *buffer, TSMS_LSIZE size);

TSMS_RESULT TSMS_FILE_write(pFilestream stream, uint8_t *buffer, TSMS_LSIZE size);

TSMS_POS TSMS_FILE_tell(pFilestream stream);

TSMS_RESULT TSMS_FILE_close(pFilestream stream);


#endif //TSMS_FILESYSTEM_H
