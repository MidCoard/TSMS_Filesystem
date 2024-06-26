#include <errno.h>
#include "unistd.h"
#include "tsms_filesystem.h"

const uint32_t TSMS_FILE_MAGIC = 0x3def11c1;

pFilesystem defaultFilesystem = TSMS_NULL;

pString TSMS_STRING_ROOT;

pString TSMS_STRING_CURRENT;

pString TSMS_STRING_PARENT;

pString TSMS_STRING_SPACE;

uint8_t filenameBuffer[256];

uint8_t contentBuffer[TSMS_FILE_CONTENT_BLOCK];
uint8_t contentBuffer2[TSMS_FILE_CONTENT_BLOCK];

const uint8_t TSMS_FILE_EMPTY_CONTENT[0];

struct __tsms_internal_pair {
	TSMS_POS offset;
	TSMS_LSIZE count;
};

TSMS_INLINE struct __tsms_internal_pair *__internal_tsms_create_pair(TSMS_POS offset, TSMS_LSIZE count) {
	struct __tsms_internal_pair *pair = TSMS_malloc(sizeof(struct __tsms_internal_pair));
	if (pair == TSMS_NULL)
		return TSMS_NULL;
	pair->offset = offset;
	pair->count = count;
	return pair;
}

TSMS_INLINE TSMS_FILESYSTEM_PLATFORM_SENSITIVE void __internal_tsms_seek(void *handle, long offset) {
#ifdef STM32
#else
	if (fseek(handle, offset, SEEK_SET) != 0)
		printf("fseek failed");
#ifdef TSMS_DEBUG
	printf("Seeked to %x", offset);
	if (offset < TSMS_FILESYSTEM_HEADER_OFFSET)
		printf(" (internal)");
	else if (offset < TSMS_FILESYSTEM_CONTENT_OFFSET)
		printf(" (header)");
	else
		printf(" (content)");
	printf("\n");
#endif
#endif
}

TSMS_INLINE TSMS_FILESYSTEM_PLATFORM_SENSITIVE long __internal_tsms_tell(void *handle) {
#ifdef STM32
#else
	return ftell(handle);
#endif
}

TSMS_INLINE TSMS_FILESYSTEM_PLATFORM_SENSITIVE void __internal_tsms_write(void *handle, const void *ptr, size_t size) {
#ifdef STM32
#else
	size_t t = fwrite(ptr, size, 1, handle);
	if (t != 1) {
		printf("fwrite failed, %d\n", ferror(handle));
		printf("errno: %d\n", errno);
	}
#ifdef TSMS_DEBUG
	printf("Wrote %x bytes\n", size);
#endif
#endif
}

TSMS_INLINE TSMS_FILESYSTEM_PLATFORM_SENSITIVE void __internal_tsms_read(void *handle, void *ptr, size_t size) {
#ifdef STM32
#else
	size_t t = fread(ptr, size, 1, handle);
	if (t != 1) {
		printf("fread failed, %d\n", ferror(handle));
		printf("errno: %d\n", errno);
	}
#ifdef TSMS_DEBUG
	printf("Read %x bytes\n", size);
#endif
#endif
}

TSMS_INLINE TSMS_FILESYSTEM_PLATFORM_SENSITIVE void __internal_tsms_close_filesystem(pFilesystem fs) {
#ifdef TSMS_STM32
#else
	fclose(fs->native);
#endif
}

TSMS_INLINE void __internal_tsms_save_filesystem(pFilesystem fs) {
#ifdef TSMS_STM32
	printf("Saving filesystem...\n");
#endif
	__internal_tsms_seek(fs->native, TSMS_FILESYSTEM_INTERNAL_OFFSET);
	__internal_tsms_write(fs->native, &fs->headerEnd, sizeof(TSMS_POS));
	__internal_tsms_write(fs->native, &fs->contentEnd, sizeof(TSMS_POS));
	__internal_tsms_write(fs->native, &fs->headerDeque->list->length, sizeof(TSMS_SIZE));
	for (TSMS_LKNP node = fs->headerDeque->list->head; node != TSMS_NULL; node = node->next) {
		struct __tsms_internal_pair *pair = node->element;
		__internal_tsms_write(fs->native, &pair->offset, sizeof(long));
		__internal_tsms_write(fs->native, &pair->count, sizeof(long));
	}
	__internal_tsms_write(fs->native, &fs->contentDeque->list->length, sizeof(TSMS_SIZE));
	for (TSMS_LKNP node = fs->contentDeque->list->head; node != TSMS_NULL; node = node->next) {
		struct __tsms_internal_pair *pair = node->element;
		__internal_tsms_write(fs->native, &pair->offset, sizeof(long));
		__internal_tsms_write(fs->native, &pair->count, sizeof(long));
	}
#ifdef TSMS_STM32
	printf("Filesystem saved\n");
#endif
}

TSMS_INLINE void __internal_tsms_read_filesystem(pFilesystem fs) {
	fs->headerDeque = TSMS_DEQUE_create();
	fs->contentDeque = TSMS_DEQUE_create();
	__internal_tsms_seek(fs->native, TSMS_FILESYSTEM_INTERNAL_OFFSET);
	__internal_tsms_read(fs->native, &fs->headerEnd, sizeof(TSMS_POS));
	__internal_tsms_read(fs->native, &fs->contentEnd, sizeof(TSMS_POS));
	TSMS_SIZE size;
	__internal_tsms_read(fs->native, &size, sizeof(TSMS_SIZE));
	for (TSMS_POS i = 0; i < size; i++) {
		TSMS_POS offset;
		TSMS_LSIZE count;
		__internal_tsms_read(fs->native, &offset, sizeof(TSMS_POS));
		__internal_tsms_read(fs->native, &count, sizeof(TSMS_LSIZE));
		TSMS_DEQUE_addLast(fs->headerDeque, __internal_tsms_create_pair(offset, count));
	}
	__internal_tsms_read(fs->native, &size, sizeof(TSMS_SIZE));
	for (TSMS_POS i = 0; i < size; i++) {
		TSMS_POS offset;
		TSMS_LSIZE count;
		__internal_tsms_read(fs->native, &offset, sizeof(TSMS_POS));
		__internal_tsms_read(fs->native, &count, sizeof(TSMS_LSIZE));
		TSMS_DEQUE_addLast(fs->contentDeque, __internal_tsms_create_pair(offset, count));
	}
}

TSMS_INLINE void __internal_tsms_dealloc_header_block(pFilesystem fs, long offset) {
#ifdef TSMS_DEBUG
	printf("Deallocating header block at %x\n", offset);
#endif
	bool flag = false;
	for (TSMS_LKNP node = fs->headerDeque->list->head; node != TSMS_NULL; node = node->next) {
		struct __tsms_internal_pair *pair = node->element;
		if (offset == pair->offset - TSMS_FILE_HEADER_BLOCK) {
			pair->offset = offset;
			pair->count++;
		} else if (offset == pair->offset + TSMS_FILE_HEADER_BLOCK + pair->count * TSMS_FILE_HEADER_BLOCK)
			pair->count++;
		else
			continue;
		flag = true;
		break;
	}
	if (!flag) {
		if (TSMS_DEQUE_size(fs->headerDeque) >= TSMS_FILESYSTEM_AVAILABLE_BLOCK_THRESHOLD)
			free(TSMS_DEQUE_removeFirst(fs->headerDeque));
		TSMS_DEQUE_addLast(fs->headerDeque, __internal_tsms_create_pair(offset, 1));
	}
}

TSMS_INLINE void __internal_tsms_dealloc_content_block(pFilesystem fs, long offset) {
#ifdef TSMS_DEBUG
	printf("Deallocating content block at %x\n", offset);
#endif
	bool flag = false;
	for (TSMS_LKNP node = fs->headerDeque->list->head; node != TSMS_NULL; node = node->next) {
		struct __tsms_internal_pair *pair = node->element;
		if (offset == pair->offset - TSMS_FILE_CONTENT_BLOCK) {
			pair->offset = offset;
			pair->count++;
		} else if (offset == pair->offset + TSMS_FILE_CONTENT_BLOCK + pair->count * TSMS_FILE_CONTENT_BLOCK)
			pair->count++;
		else
			continue;
		flag = true;
		break;
	}
	if (!flag) {
		if (TSMS_DEQUE_size(fs->contentDeque) >= TSMS_FILESYSTEM_AVAILABLE_BLOCK_THRESHOLD)
			free(TSMS_DEQUE_removeFirst(fs->contentDeque));
		TSMS_DEQUE_addLast(fs->contentDeque, __internal_tsms_create_pair(offset, 1));
	}
}

TSMS_INLINE void __internal_tsms_mark_free_header_block(pFilesystem fs, long offset) {
	__internal_tsms_seek(fs->native, offset);
	uint8_t t = 0xff;
	for (TSMS_POS i = 0; i < 8; i++)
		__internal_tsms_write(fs->native, &t, 1);
	__internal_tsms_dealloc_header_block(fs, offset);
}

TSMS_INLINE void __internal_tsms_mark_free_content_block(pFilesystem fs, long offset) {
	__internal_tsms_seek(fs->native, offset);
	uint8_t t = 0xff;
	for (TSMS_POS i = 0; i < TSMS_FILE_CONTENT_BLOCK; i++) // todo may should only delete first 8 bytes or just ignore
		__internal_tsms_write(fs->native, &t, 1);
	__internal_tsms_dealloc_content_block(fs, offset);
}

TSMS_INLINE TSMS_SIZE __internal_tsms_calc_align_bytes(pFile file) {
	TSMS_SIZE align = 8 - (4 + file->name->length + 1 + 4 + 4) % 8;
	return align == 8 ? 0 : align;
}

TSMS_INLINE TSMS_SIZE __internal_tsms_calc_definition_block_size(pFile file) {
	return 4 + file->name->length + 1 + 4 + 4 + __internal_tsms_calc_align_bytes(file);
}

TSMS_INLINE TSMS_SIZE __internal_tsms_calc_header_size(pFile file) {
	return __internal_tsms_calc_definition_block_size(file) +
	       (TSMS_FILESYSTEM_isFolder(file) ? 8 * file->files->size : 8 * file->blocks->length + 8);
}

TSMS_INLINE long __internal_tsms_alloc_header_block(pFilesystem filesystem) {
	if (!TSMS_DEQUE_empty(filesystem->headerDeque)) {
		struct __tsms_internal_pair *pair = TSMS_DEQUE_peekLast(filesystem->headerDeque);
		pair->count--;
		if (pair->count == 0)
			TSMS_DEQUE_removeLast(filesystem->headerDeque);
		long offset = pair->offset + pair->count;
		free(pair);
#ifdef TSMS_DEBUG
		printf("Allocating new header block at %x\n", offset);
#endif
		long cur = __internal_tsms_tell(filesystem->native);
#ifdef TSMS_DEBUG
		printf("record native position: %x\n", cur);
#endif
		__internal_tsms_save_filesystem(filesystem);
		__internal_tsms_seek(filesystem->native, cur);
		return offset;
	}
	long offset = filesystem->headerEnd + TSMS_FILESYSTEM_HEADER_OFFSET;
	filesystem->headerEnd += TSMS_FILE_HEADER_BLOCK;
#ifdef TSMS_DEBUG
	printf("Allocating new header block at %x\n", offset);
#endif
	long cur = __internal_tsms_tell(filesystem->native);
#ifdef TSMS_DEBUG
	printf("record native position: %x\n", cur);
#endif
	__internal_tsms_save_filesystem(filesystem);
	__internal_tsms_seek(filesystem->native, cur);
	return offset;
}

// todo platform special, check the limitation of the disk size, in other words, this method may return -1 as it is not possible to allocate a new block
TSMS_INLINE long __internal_tsms_alloc_content_block(pFilesystem filesystem) {
	if (!TSMS_DEQUE_empty(filesystem->contentDeque)) {
		struct __tsms_internal_pair *pair = TSMS_DEQUE_peekLast(filesystem->contentDeque);
		pair->count--;
		if (pair->count == 0)
			TSMS_DEQUE_removeLast(filesystem->contentDeque);
		long offset = pair->offset + pair->count;
		free(pair);
#ifdef TSMS_DEBUG
		printf("Allocating new content block at %x\n", offset);
#endif
		long cur = __internal_tsms_tell(filesystem->native);
#ifdef TSMS_DEBUG
		printf("record native position: %x\n", cur);
#endif
		__internal_tsms_save_filesystem(filesystem);
		__internal_tsms_seek(filesystem->native, cur);
		return offset;
	}
	long offset = filesystem->contentEnd + TSMS_FILESYSTEM_CONTENT_OFFSET;
	filesystem->contentEnd += TSMS_FILE_CONTENT_BLOCK;
#ifdef TSMS_DEBUG
	printf("Allocating new content block at %x\n", offset);
#endif
	long cur = __internal_tsms_tell(filesystem->native);
#ifdef TSMS_DEBUG
	printf("record native position: %x\n", cur);
#endif
	__internal_tsms_save_filesystem(filesystem);
	__internal_tsms_seek(filesystem->native, cur);
	return offset;

}

TSMS_INLINE bool __internal_tsms_check_file_name(pFilesystem fs, pString name) {
	if (name->length > TSMS_FILE_NAME_MAX_LENGTH)
		return false;
	for (TSMS_SIZE i = 0; i < name->length; i++)
		if (name->cStr[i] == fs->split->cStr[0] || name->cStr[i] == ':' || name->cStr[i] == '*' ||
		    name->cStr[i] == '?' || name->cStr[i] == '"' || name->cStr[i] == '<' || name->cStr[i] == '>' ||
		    name->cStr[i] == '|')
			return false;
	return true;
}

TSMS_INLINE void __internal_tsms_save_header(pFile file) {
	__internal_tsms_seek(file->filesystem->native, file->offset);
	__internal_tsms_write(file->filesystem->native, &TSMS_FILE_MAGIC, sizeof(uint32_t));
	for (TSMS_POS i = 0; i < file->name->length; i++)
		__internal_tsms_write(file->filesystem->native, file->name->cStr + i, sizeof(char));
	uint8_t t = 0;
	__internal_tsms_write(file->filesystem->native, &t, sizeof(uint8_t));
	for (TSMS_POS i = 0; i < 4; i++)
		__internal_tsms_write(file->filesystem->native, file->options + i, sizeof(TSMS_FILE_OPTION));
	TSMS_SIZE align = __internal_tsms_calc_align_bytes(file);
	for (TSMS_POS i = 0; i < align; i++)
		__internal_tsms_write(file->filesystem->native, &t, sizeof(uint8_t));
	TSMS_SIZE currentSize = __internal_tsms_calc_definition_block_size(file);
	TSMS_SIZE totalSize = __internal_tsms_calc_header_size(file);
	TSMS_SIZE blockSize = totalSize % (TSMS_FILE_HEADER_BLOCK - TSMS_FILE_UNIT) == 0 ? totalSize /
	                                                                                   (TSMS_FILE_HEADER_BLOCK -
	                                                                                    TSMS_FILE_UNIT) :
	                      totalSize / (TSMS_FILE_HEADER_BLOCK - TSMS_FILE_UNIT) + 1;
	for (TSMS_POS i = file->anchors->length; i < blockSize - 1; i++)
		TSMS_LONG_LIST_add(file->anchors, __internal_tsms_alloc_header_block(file->filesystem));
	TSMS_POS anchorPos = 0;
	if (TSMS_FILESYSTEM_isFolder(file)) {
		__internal_tsms_write(file->filesystem->native, &file->files->size, sizeof(TSMS_SIZE));
		TSMS_MI iter = TSMS_MAP_iterator(file->files);
		while (TSMS_MAP_hasNext(&iter)) {
			if (((currentSize + TSMS_FILE_UNIT) % TSMS_FILE_HEADER_BLOCK) == 0) {
				long offset = file->anchors->list[anchorPos++];
				__internal_tsms_write(file->filesystem->native, &offset, sizeof(long));
				__internal_tsms_seek(file->filesystem->native, offset);
				currentSize += TSMS_FILE_UNIT;
			}
			TSMS_ME entry = TSMS_MAP_next(&iter);
			pFile f = entry.value;
			long offset = f->offset;
			__internal_tsms_write(file->filesystem->native, &offset, sizeof(long));
			currentSize += TSMS_FILE_UNIT;
		}
	} else {
		__internal_tsms_write(file->filesystem->native, &file->blocks->length, sizeof(TSMS_SIZE));
		__internal_tsms_write(file->filesystem->native, &file->size, sizeof(TSMS_LSIZE));
		currentSize += 8;
		for (TSMS_POS i = 0; i < file->blocks->length; i++) {
			if (((currentSize + TSMS_FILE_UNIT) % TSMS_FILE_HEADER_BLOCK) == 0) {
				long offset = file->anchors->list[anchorPos++];
				__internal_tsms_write(file->filesystem->native, &offset, sizeof(long));
				__internal_tsms_seek(file->filesystem->native, offset);
				currentSize += TSMS_FILE_UNIT;
			}
			long offset = file->blocks->list[i];
			__internal_tsms_write(file->filesystem->native, &offset, sizeof(long));
			currentSize += TSMS_FILE_UNIT;
		}
	}
	for (TSMS_POS i = anchorPos; i < file->anchors->length; i++)
		__internal_tsms_mark_free_header_block(file->filesystem, file->anchors->list[i]);
	TSMS_LONG_LIST_truncate(file->anchors, anchorPos);
	file->dirty = false;
}

TSMS_INLINE pFile __internal_tsms_read_file(pFilesystem filesystem, long offset, pFile parent, bool deep);

TSMS_INLINE void __internal_tsms_load_file(pFile file) {
	if (file->loaded)
		return;
	TSMS_SIZE currentSize = __internal_tsms_calc_definition_block_size(file);
	TSMS_SIZE size;
	__internal_tsms_seek(file->filesystem->native, file->offset + currentSize - 4);
	__internal_tsms_read(file->filesystem->native, &size, sizeof(TSMS_SIZE));
	if (TSMS_FILESYSTEM_isFolder(file)) {
		file->files = TSMS_MAP_create(255, (TSMS_HASH_FUNCTION) TSMS_STRING_hash,
		                              (TSMS_COMPARE_FUNCTION) TSMS_STRING_compare);
		file->blocks = TSMS_NULL;
		for (TSMS_POS i = 0; i < size; i++) {
			if (((currentSize + TSMS_FILE_UNIT) % TSMS_FILE_HEADER_BLOCK) == 0) {
				long offset;
				__internal_tsms_read(file->filesystem->native, &offset, sizeof(long));
				if (offset > TSMS_FILESYSTEM_CONTENT_OFFSET)
					printf("Content offset error: %ld\n", offset);
				TSMS_LONG_LIST_add(file->anchors, offset);
				__internal_tsms_seek(file->filesystem->native, offset);
				currentSize += TSMS_FILE_UNIT;
			}
			long offset;
			__internal_tsms_read(file->filesystem->native, &offset, sizeof(long));
			long cur = __internal_tsms_tell(file->filesystem->native);
			pFile child = __internal_tsms_read_file(file->filesystem, offset, file, false);
			__internal_tsms_seek(file->filesystem->native, cur);
			TSMS_MAP_put(file->files, child->name, child);
			currentSize += TSMS_FILE_UNIT;
		}
	} else {
		file->files = TSMS_NULL;
		file->blocks = TSMS_LONG_LIST_create(10);
		__internal_tsms_read(file->filesystem->native, &file->size, sizeof(TSMS_LSIZE));
		currentSize += 8;
		for (TSMS_POS i = 0; i < size; i++) {
			if (((currentSize + TSMS_FILE_UNIT) % TSMS_FILE_HEADER_BLOCK) == 0) {
				long offset;
				__internal_tsms_read(file->filesystem->native, &offset, sizeof(long));
				if (offset > TSMS_FILESYSTEM_CONTENT_OFFSET)
					printf("Content offset error: %ld\n", offset);
				TSMS_LONG_LIST_add(file->anchors, offset);
				__internal_tsms_seek(file->filesystem->native, offset);
				currentSize += TSMS_FILE_UNIT;
			}
			long offset;
			__internal_tsms_read(file->filesystem->native, &offset, sizeof(long));
			TSMS_LONG_LIST_add(file->blocks, offset);
			currentSize += TSMS_FILE_UNIT;
		}
	}

	file->loaded = true;
}

TSMS_INLINE pFile __internal_tsms_read_file(pFilesystem filesystem, long offset, pFile parent, bool deep) {
	pFile file = TSMS_malloc(sizeof(tFile));
	if (file == TSMS_NULL)
		return TSMS_NULL;

	__internal_tsms_seek(filesystem->native, offset);
	uint32_t magic;
	__internal_tsms_read(filesystem->native, &magic, sizeof(uint32_t));

	if (magic != TSMS_FILE_MAGIC) {
		free(file);
		return TSMS_NULL;
	}
	file->dirty = false;
	file->filesystem = filesystem;

	file->offset = offset;
	file->parent = parent;
	file->anchors = TSMS_LONG_LIST_create(10);

	file->level = parent == TSMS_NULL ? 0 : parent->level + 1;
	file->loaded = false;

	TSMS_SIZE length = 0;
	uint8_t c;
	while (length < TSMS_FILE_NAME_MAX_LENGTH) {
		__internal_tsms_read(filesystem->native, &c, sizeof(uint8_t));
		if (c == 0)
			break;
		filenameBuffer[length++] = c;
	}
	if (c != 0) {
		TSMS_LONG_LIST_release(file->anchors);
		free(file);
		return TSMS_NULL;
	}

	filenameBuffer[length] = 0;
	file->name = TSMS_STRING_createWithString(filenameBuffer);
	for (TSMS_POS i = 0; i < 4; i++)
		__internal_tsms_read(filesystem->native, file->options + i, sizeof(TSMS_FILE_OPTION));
	TSMS_SIZE align = __internal_tsms_calc_align_bytes(file);
	for (TSMS_POS i = 0; i < align; i++)
		__internal_tsms_read(filesystem->native, &c, sizeof(uint8_t));

	file->size = -1;

	if (deep)
		__internal_tsms_load_file(file);

	return file;
}

TSMS_INLINE pFile
__internal_tsms_create_file(pFilesystem filesystem, long offset, pString name, TSMS_FILE_TYPE type, pFile parent,
                            const uint8_t *options) {
	pFile file = TSMS_malloc(sizeof(tFile));
	if (file == TSMS_NULL)
		return TSMS_NULL;
	file->dirty = false;
	file->filesystem = filesystem;

	file->offset = offset;
	file->parent = parent;
	file->anchors = TSMS_LONG_LIST_create(10);

	file->level = parent == TSMS_NULL ? 0 : parent->level + 1;
	file->loaded = true;

	file->name = TSMS_STRING_createWithString(name->cStr);
	if (parent != TSMS_NULL)
		TSMS_MAP_put(parent->files, file->name, file);
	if (options != TSMS_NULL)
		for (int i = 0; i < 4; i++)
			file->options[i] = options[i];
	file->options[0] &= 0x7f;
	file->options[0] |= type << 7;
	for (int i = 1; i < 4; i++)
		file->options[i] = 0;
	if (type == TSMS_FILE_TYPE_FOLDER) {
		file->files = TSMS_MAP_create(255, (TSMS_HASH_FUNCTION) TSMS_STRING_hash,
		                              (TSMS_COMPARE_FUNCTION) TSMS_STRING_compare);
		file->blocks = TSMS_NULL;
	} else {
		file->files = TSMS_NULL;
		file->blocks = TSMS_LONG_LIST_create(10);
	}
	file->size = 0;

	__internal_tsms_save_header(file);
	if (parent != TSMS_NULL)
		__internal_tsms_save_header(parent);
	return file;
}

TSMS_RESULT TSMS_FILESYSTEM_init(TSMS_CLOCK_FREQUENCY frequency) {
	TSMS_STRING_ROOT = TSMS_STRING_static("root");
	TSMS_STRING_CURRENT = TSMS_STRING_static(".");
	TSMS_STRING_PARENT = TSMS_STRING_static("..");
	TSMS_STRING_SPACE = TSMS_STRING_static(" ");
	if (TSMS_STRING_ROOT == TSMS_NULL || TSMS_STRING_CURRENT == TSMS_NULL || TSMS_STRING_PARENT == TSMS_NULL)
		return TSMS_ERROR;
	TSMS_FILESYSTEM_setDefaultFilesystem(TSMS_FILESYSTEM_createFilesystem('/'));
	return TSMS_SUCCESS;
}

TSMS_RESULT TSMS_FILESYSTEM_setDefaultFilesystem(pFilesystem filesystem) {
	defaultFilesystem = filesystem;
	return TSMS_SUCCESS;
}

pFilesystem TSMS_FILESYSTEM_PLATFORM_SENSITIVE TSMS_FILESYSTEM_createFilesystem(char split) {
	pFilesystem filesystem = TSMS_malloc(sizeof(tFilesystem));
	if (filesystem == TSMS_NULL)
		return TSMS_NULL;
	filesystem->split = TSMS_STRING_createWithChar(split);
	if (filesystem->split == TSMS_NULL) {
		TSMS_FILESYSTEM_release(filesystem);
		return TSMS_NULL;
	}
#ifdef TSMS_STM32
#else
	if (access("filesystem", F_OK) == 0) {
		filesystem->native = fopen("filesystem", "r+");
		if (filesystem->native == TSMS_NULL) {
			TSMS_FILESYSTEM_release(filesystem);
			return TSMS_NULL;
		}
		__internal_tsms_read_filesystem(filesystem);
		filesystem->root = __internal_tsms_read_file(filesystem, TSMS_FILESYSTEM_HEADER_OFFSET, TSMS_NULL, true);
		if (filesystem->root == TSMS_NULL) {
			TSMS_FILESYSTEM_release(filesystem);
			return TSMS_NULL;
		}
	} else {
		filesystem->native = fopen("filesystem", "w+");
		if (filesystem->native == TSMS_NULL) {
			TSMS_FILESYSTEM_release(filesystem);
			return TSMS_NULL;
		}
		filesystem->root = __internal_tsms_create_file(filesystem, TSMS_FILESYSTEM_HEADER_OFFSET, TSMS_STRING_ROOT,
		                                               TSMS_FILE_TYPE_FOLDER, TSMS_NULL, 0);
		filesystem->headerEnd = TSMS_FILE_HEADER_BLOCK;
		filesystem->headerDeque = TSMS_DEQUE_create();
		filesystem->contentEnd = 0;
		filesystem->contentDeque = TSMS_DEQUE_create();
		if (filesystem->root == TSMS_NULL) {
			TSMS_FILESYSTEM_release(filesystem);
			return TSMS_NULL;
		}
	}
#endif
	return filesystem;
}

pFile TSMS_FILESYSTEM_createFolder(pFile parent, pString name, uint8_t *options) {
	pFile file = TSMS_FILESYSTEM_getFile(parent, name);
	if (file != TSMS_NULL)
		return TSMS_NULL;
	if (!__internal_tsms_check_file_name(parent->filesystem, name))
		return TSMS_NULL;
	long offset = __internal_tsms_alloc_header_block(parent->filesystem);
	return __internal_tsms_create_file(parent->filesystem, offset, name, TSMS_FILE_TYPE_FOLDER, parent, options);
}

pFile TSMS_FILESYSTEM_createFile(pFile parent, pString name, uint8_t *options) {
	pFile file = TSMS_FILESYSTEM_getFile(parent, name);
	if (file != TSMS_NULL)
		return TSMS_NULL;
	if (!__internal_tsms_check_file_name(parent->filesystem, name))
		return TSMS_NULL;
	long offset = __internal_tsms_alloc_header_block(parent->filesystem);
	return __internal_tsms_create_file(parent->filesystem, offset, name, TSMS_FILE_TYPE_FILE, parent, options);
}

TSMS_RESULT TSMS_FILESYSTEM_releaseFile(pFile file) {
	if (file == TSMS_NULL)
		return TSMS_ERROR;
	if (file->dirty)
		__internal_tsms_save_header(file);
	if (file->loaded) {
		if (TSMS_FILESYSTEM_isFolder(file)) {
			TSMS_MI it = TSMS_MAP_iterator(file->files);
			while (TSMS_MAP_hasNext(&it)) {
				TSMS_ME entry = TSMS_MAP_next(&it);
				pFile child = entry.value;
				TSMS_FILESYSTEM_releaseFile(child); // does not matter if it fails
			}
			TSMS_MAP_release(file->files);
		} else
			TSMS_LONG_LIST_release(file->blocks);
	}
	TSMS_LONG_LIST_release(file->anchors);
	TSMS_STRING_release(file->name);
	free(file);
	return TSMS_SUCCESS;
}

TSMS_RESULT TSMS_FILESYSTEM_release(pFilesystem filesystem) {
	if (filesystem == TSMS_NULL)
		return TSMS_ERROR;
	TSMS_STRING_release(filesystem->split);
	TSMS_FILESYSTEM_releaseFile(filesystem->root);
	__internal_tsms_save_filesystem(filesystem);
	for (TSMS_LKNP node = filesystem->headerDeque->list->head; node != TSMS_NULL; node = node->next)
		free(node->element);
	__internal_tsms_close_filesystem(filesystem);
	TSMS_DEQUE_release(filesystem->headerDeque);
	TSMS_DEQUE_release(filesystem->contentDeque);
	free(filesystem);
	return TSMS_SUCCESS;
}

bool TSMS_FILESYSTEM_isFolder(pFile file) {
	if (file == TSMS_NULL)
		return false;
	return (file->options[0] >> 7) == TSMS_FILE_TYPE_FOLDER;
}

pFile TSMS_FILESYSTEM_getFile(pFile parent, pString name) {
	if (parent == TSMS_NULL)
		return TSMS_NULL;
	pFile child = TSMS_MAP_get(parent->files, name);
	if (child == TSMS_NULL)
		return TSMS_NULL;
	__internal_tsms_load_file(child);
	return child;
}


// will return TSMS_FILE_EMPTY_CONTENT if file is empty
uint8_t *TSMS_FILESYSTEM_readFile(pFile file) {
	if (file == TSMS_NULL)
		return TSMS_NULL;
	if (TSMS_FILESYSTEM_isFolder(file))
		return TSMS_NULL;
	TSMS_LSIZE size = file->size;
	if (size == 0)
		return TSMS_FILE_EMPTY_CONTENT;
	uint8_t *buffer = TSMS_malloc(sizeof(uint8_t) * size);
	if (buffer == TSMS_NULL)
		return TSMS_NULL;
	TSMS_LSIZE bufferLength = 0;
	for (TSMS_POS i = 0; i < file->blocks->length; i++) {
		TSMS_SIZE block = min(size, TSMS_FILE_CONTENT_BLOCK);
		__internal_tsms_seek(file->filesystem->native, file->blocks->list[i]);
		__internal_tsms_read(file->filesystem->native, buffer + bufferLength, block);
		size -= block;
		bufferLength += block;
	}
	// size should be 0
	return buffer;
}

// will return TSMS_FILE_EMPTY_CONTENT if the file is empty
uint8_t *TSMS_FILESYSTEM_readPartialFile(pFile file, TSMS_POS start, TSMS_POS end) {
	if (file == TSMS_NULL)
		return TSMS_NULL;
	if (TSMS_FILESYSTEM_isFolder(file))
		return TSMS_NULL;
	if (start > end || end > file->size || start < 0)
		return TSMS_NULL;
	TSMS_LSIZE size = end - start;
	if (size == 0)
		return TSMS_FILE_EMPTY_CONTENT;
	uint8_t *buffer = TSMS_malloc(sizeof(uint8_t) * size);
	if (buffer == TSMS_NULL)
		return TSMS_NULL;
	TSMS_POS startBlockPos = start / TSMS_FILE_CONTENT_BLOCK;
	TSMS_POS endBlockPos = end / TSMS_FILE_CONTENT_BLOCK;
	if (startBlockPos == endBlockPos) {
		__internal_tsms_seek(file->filesystem->native,
		                     file->blocks->list[startBlockPos] + (start % TSMS_FILE_CONTENT_BLOCK));
		__internal_tsms_read(file->filesystem->native, buffer, size);
		return buffer;
	}
	TSMS_SIZE startBlockSize = TSMS_FILE_CONTENT_BLOCK - (start % TSMS_FILE_CONTENT_BLOCK);
	__internal_tsms_seek(file->filesystem->native,
	                     file->blocks->list[startBlockPos] + (start % TSMS_FILE_CONTENT_BLOCK));
	__internal_tsms_read(file->filesystem->native, buffer, startBlockSize);
	for (TSMS_POS i = startBlockPos + 1; i < endBlockPos; i++) {
		__internal_tsms_seek(file->filesystem->native, file->blocks->list[i]);
		__internal_tsms_read(file->filesystem->native, buffer + startBlockSize + (i - startBlockPos - 1) * TSMS_FILE_CONTENT_BLOCK, TSMS_FILE_CONTENT_BLOCK);
	}
	TSMS_SIZE endBlockSize = end % TSMS_FILE_CONTENT_BLOCK;
	__internal_tsms_seek(file->filesystem->native, file->blocks->list[endBlockPos]);
	__internal_tsms_read(file->filesystem->native, buffer + startBlockSize + (endBlockPos - startBlockPos - 1) * TSMS_FILE_CONTENT_BLOCK, endBlockSize);
	return buffer;
}

// used to safely free the buffer returned by TSMS_FILESYSTEM_readFile and TSMS_FILESYSTEM_readPartialFile
TSMS_RESULT TSMS_FILESYSTEM_freeContentBuffer(uint8_t *buffer) {
	if (buffer == TSMS_NULL)
		return TSMS_ERROR;
	if (buffer == TSMS_FILE_EMPTY_CONTENT)
		return TSMS_SUCCESS;
	free(buffer);
	return TSMS_SUCCESS;
}

TSMS_RESULT TSMS_FILESYSTEM_writeFile(pFile file, uint8_t *content, TSMS_LSIZE size) {
	if (file == TSMS_NULL || content == TSMS_NULL)
		return TSMS_ERROR;
	if (TSMS_FILESYSTEM_emptyFile(file) != TSMS_SUCCESS)
		return TSMS_ERROR;
	return TSMS_FILESYSTEM_insertFile(file, content, 0, size);
}

// todo we do not check __internal_tsms_alloc_content_block may return -1, which means the disk is full
// todo there is no rollback if some error occurs in the middle of the process, which may cause the file system to be corrupted
// todo File Cotent-Block One Byte Freed Information
TSMS_RESULT TSMS_FILESYSTEM_insertFile(pFile file, const uint8_t *content, TSMS_POS pos, TSMS_LSIZE size) {
	if (file == TSMS_NULL || content == TSMS_NULL)
		return TSMS_ERROR;
	if (TSMS_FILESYSTEM_isFolder(file))
		return TSMS_ERROR;
	if (pos > file->size || pos < 0)
		return TSMS_ERROR;
	if (size == 0)
		return TSMS_SUCCESS;

	TSMS_POS startBlockPos = pos / TSMS_FILE_CONTENT_BLOCK;
	TSMS_SIZE tempSize = TSMS_FILE_CONTENT_BLOCK - (pos % TSMS_FILE_CONTENT_BLOCK);
	TSMS_SIZE startBlockRestSize = min (tempSize, file->size - pos);
	// if the start block is not the end block of file, the rest size depends on the tempSize and surely less than file->size - pos
	// if the start block is the end block of file or the start block does not exist, the rest size depends on the file->size - pos and surely less or equal to tempSize
	TSMS_SIZE startBlockWritableSize = min((startBlockPos == file->blocks->length) ? 0 : tempSize, size);
	// the startBlockWritableSize depends on whether the start block exists and the content size
	// if the start block exists, min (tempSize, size) will be the startBlockWritableSize
	// if the start block does not exist, the startBlockWritableSize will be 0
	// only if the pos is at multiple of TSMS_FILE_CONTENT_BLOCK and the pos is at the end of file, the startBlockWritableSize will be 0, which means the start block does not exist
	// so (startBlockWritableSize != 0) will indicate whether the start block exists
	TSMS_SIZE startBlockExisted = startBlockWritableSize != 0;
	// copy the start block rest size to contentBuffer2
	if (startBlockRestSize != 0) {
		__internal_tsms_seek(file->filesystem->native,
		                     file->blocks->list[startBlockPos] + (pos % TSMS_FILE_CONTENT_BLOCK));
		__internal_tsms_read(file->filesystem->native, contentBuffer2, startBlockRestSize);
	}
	if (startBlockWritableSize != 0) {
		__internal_tsms_seek(file->filesystem->native,
		                     file->blocks->list[startBlockPos] + (pos % TSMS_FILE_CONTENT_BLOCK));
		__internal_tsms_write(file->filesystem->native, content, startBlockWritableSize);
	}
	// middleBlockSize > 0 only if the rest size is larger than TSMS_FILE_CONTENT_BLOCK
	TSMS_SIZE middleBlockSize = (size - startBlockWritableSize) / TSMS_FILE_CONTENT_BLOCK;
	// write the middle full blocks (the size must be TSMS_FILE_CONTENT_BLOCK)
	for (TSMS_POS i = 0; i < middleBlockSize; i++) {
		long offset = __internal_tsms_alloc_content_block(file->filesystem);
		TSMS_LONG_LIST_insert(file->blocks, offset, startBlockPos + startBlockExisted + i);
		__internal_tsms_seek(file->filesystem->native, offset);
		__internal_tsms_write(file->filesystem->native, content + startBlockWritableSize + i * TSMS_FILE_CONTENT_BLOCK, TSMS_FILE_CONTENT_BLOCK);
	}
	// the rest: three parts:
	// 1. the rest of the content < TSMS_FILE_CONTENT_BLOCK
	// 2. the rest of the start block <= TSMS_FILE_CONTENT_BLOCK
	// 3. the rest of the following blocks after the start block
	// if the 1 and 2's size is TSMS_FILE_CONTENT_BLOCK or 0, we don't need to do anything to the following blocks after the start block, so special case

	TSMS_LSIZE restSize = size - startBlockWritableSize - middleBlockSize * TSMS_FILE_CONTENT_BLOCK;
	TSMS_LSIZE size1And2 = restSize + startBlockRestSize;
	// if start block exists and size < tempSize, there should be blank space in the start block, which size is tempSize - size
	// only if the startBlockRestSize is tempSize, the following blocks after the start block should exist
	// use startBlockRestSize to fill the blank space and there should be a space with size tempSize - (tempSize - size) = size, which should never be 0
	// so when there is a blank space, the following blocks after the start block should be moved. This is a general case
	if ((size1And2) % TSMS_FILE_CONTENT_BLOCK != 0) {
		// general case
		// align the rest of the start block if possible
		TSMS_LSIZE total = restSize + file->size - pos;
		uint8_t * buffer = TSMS_malloc(sizeof(uint8_t) * total);
		if (buffer == TSMS_NULL)
			return TSMS_ERROR;
		memcpy(buffer, content + startBlockWritableSize + middleBlockSize * TSMS_FILE_CONTENT_BLOCK, restSize);
		memcpy(buffer + restSize, contentBuffer2, startBlockRestSize);
		TSMS_SIZE blockPos = startBlockPos + middleBlockSize + startBlockExisted;
		for (TSMS_POS i = blockPos; i < file->blocks->length;i++) {
			__internal_tsms_seek(file->filesystem->native, file->blocks->list[i]);
			__internal_tsms_read(file->filesystem->native, buffer + size1And2 + (i - blockPos) * TSMS_FILE_CONTENT_BLOCK, min(TSMS_FILE_CONTENT_BLOCK, total - size1And2 - (i - blockPos) * TSMS_FILE_CONTENT_BLOCK));
		}
		TSMS_LSIZE extraSize = min(total, (startBlockExisted && size < tempSize) ? tempSize - size : 0);
		// if start block does not exist, the extraSize will be 0
		// if start block exists, the extraSize will be the blank space size
		// only if size < tempSize and start block exist, the extraSize will not be 0
		if (extraSize != 0) {
			__internal_tsms_seek(file->filesystem->native,
			                     file->blocks->list[startBlockPos] + (pos % TSMS_FILE_CONTENT_BLOCK) +
			                     startBlockWritableSize);
			__internal_tsms_write(file->filesystem->native, buffer, extraSize);
			total -= extraSize;
		}
		TSMS_SIZE endBlockSize = total % TSMS_FILE_CONTENT_BLOCK == 0 ? total / TSMS_FILE_CONTENT_BLOCK : total / TSMS_FILE_CONTENT_BLOCK + 1;
		for (TSMS_POS i = 0; i < endBlockSize; i++) {
			if (blockPos + i == file->blocks->length) {
				long offset = __internal_tsms_alloc_content_block(file->filesystem);
				TSMS_LONG_LIST_add(file->blocks, offset);
			}
			__internal_tsms_seek(file->filesystem->native, file->blocks->list[blockPos + i]);
			__internal_tsms_write(file->filesystem->native, buffer + extraSize + i * TSMS_FILE_CONTENT_BLOCK, min(TSMS_FILE_CONTENT_BLOCK, total - i * TSMS_FILE_CONTENT_BLOCK));
		}
		free(buffer);
	} else if (size1And2 == TSMS_FILE_CONTENT_BLOCK) {
		// in this case, the rest of the start block should not be 0, so the start block exists
		memcpy(contentBuffer, content + startBlockWritableSize + middleBlockSize * TSMS_FILE_CONTENT_BLOCK, restSize);
		memcpy(contentBuffer + restSize, contentBuffer2, startBlockRestSize);
		long offset = __internal_tsms_alloc_content_block(file->filesystem);
		TSMS_LONG_LIST_insert(file->blocks, offset, startBlockPos + middleBlockSize + startBlockExisted); // startBlockExisted is 1
		__internal_tsms_seek(file->filesystem->native, offset);
		__internal_tsms_write(file->filesystem->native, contentBuffer, TSMS_FILE_CONTENT_BLOCK);
	}


	file->size += size;
	__internal_tsms_save_header(file);
	return TSMS_SUCCESS;
}

TSMS_RESULT TSMS_FILESYSTEM_emptyFile(pFile file) {
	if (file == TSMS_NULL)
		return TSMS_ERROR;
	if (TSMS_FILESYSTEM_isFolder(file))
		return TSMS_ERROR;
	file->size = 0;
	for (TSMS_POS i = 0; i < file->blocks->length; i++)
		__internal_tsms_mark_free_content_block(file->filesystem, file->blocks->list[i]);
	TSMS_LONG_LIST_clear(file->blocks);
	__internal_tsms_save_header(file);
	return TSMS_SUCCESS;
}

pString TSMS_FILESYSTEM_getPath(pFile file) {
	if (file == TSMS_NULL)
		return TSMS_NULL;
	if (file->parent == TSMS_NULL) {
		pString path = TSMS_STRING_empty();
		if (TSMS_STRING_copy(file->name, path) != TSMS_SUCCESS) {
			TSMS_STRING_release(path);
			return TSMS_NULL;
		}
		return path;
	}
	pString path = TSMS_FILESYSTEM_getPath(file->parent);
	if (path == TSMS_NULL)
		return TSMS_NULL;
	if (TSMS_STRING_append(path, file->filesystem->split) != TSMS_SUCCESS) {
		TSMS_STRING_release(path);
		return TSMS_NULL;
	}
	if (TSMS_STRING_append(path, file->name) != TSMS_SUCCESS) {
		TSMS_STRING_release(path);
		return TSMS_NULL;
	}
	return path;
}

TSMS_LSIZE TSMS_FILESYSTEM_size(pFile file) {
	if (TSMS_FILESYSTEM_isFolder(file)) {
		TSMS_LSIZE size = 0;
		TSMS_MI it = TSMS_MAP_iterator(file->files);
		while (TSMS_MAP_hasNext(&it)) {
			TSMS_ME entry = TSMS_MAP_next(&it);
			pFile child = entry.value;
			__internal_tsms_load_file(child);
			size += TSMS_FILESYSTEM_size(child);
		}
		return file->size = size;
	}
	__internal_tsms_load_file(file);
	return file->size;
}

TSMS_LP TSMS_FILESYSTEM_list(pFile file) {
	if (file == TSMS_NULL)
		return TSMS_NULL;
	if (!TSMS_FILESYSTEM_isFolder(file))
		return TSMS_EMPTY_LIST;
	TSMS_LP list = TSMS_LIST_create(file->files->size);
	if (list == TSMS_NULL)
		return TSMS_EMPTY_LIST;
	TSMS_MI it = TSMS_MAP_iterator(file->files);
	while (TSMS_MAP_hasNext(&it)) {
		TSMS_ME entry = TSMS_MAP_next(&it);
		pFile child = entry.value;
		__internal_tsms_load_file(child);
		TSMS_LIST_add(list, child);
	}
	return list;
}

TSMS_RESULT TSMS_FILESYSTEM_deleteFile(pFile file) {
	if (file == TSMS_NULL)
		return TSMS_ERROR;
	if (TSMS_FILESYSTEM_isFolder(file))
		return TSMS_ERROR;
	if (file->parent == TSMS_NULL)
		return TSMS_ERROR;
	if (TSMS_MAP_remove(file->parent->files, file->name) != TSMS_SUCCESS)
		return TSMS_FAIL;
	__internal_tsms_save_header(file->parent);
	__internal_tsms_mark_free_header_block(file->filesystem, file->offset);
	for (TSMS_POS i = 0; i < file->anchors->length; i++)
		__internal_tsms_mark_free_header_block(file->filesystem, file->anchors->list[i]);
	TSMS_FILESYSTEM_releaseFile(file);
	return TSMS_SUCCESS;
}

// if the folder is not empty, it will not be deleted
TSMS_RESULT TSMS_FILESYSTEM_deleteFolder(pFile file) {
	if (file == TSMS_NULL)
		return TSMS_ERROR;
	if (!TSMS_FILESYSTEM_isFolder(file))
		return TSMS_ERROR;
	if (file->parent == TSMS_NULL)
		return TSMS_ERROR;
	if (file->files->size > 0)
		return TSMS_ERROR;
	if (TSMS_MAP_remove(file->parent->files, file->name) != TSMS_SUCCESS)
		return TSMS_FAIL;
	__internal_tsms_save_header(file->parent);
	__internal_tsms_mark_free_header_block(file->filesystem, file->offset);
	for (TSMS_POS i = 0; i < file->anchors->length; i++)
		__internal_tsms_mark_free_header_block(file->filesystem, file->anchors->list[i]);
	TSMS_FILESYSTEM_releaseFile(file);
	return TSMS_SUCCESS;
}

pFile TSMS_FILESYSTEM_resolve(pFile current, pString path) {
	if (current == TSMS_NULL || path == TSMS_NULL)
		return TSMS_NULL;
	TSMS_LP list = TSMS_STRING_split(path, current->filesystem->split->cStr[0]);
	pFile file = current;
	TSMS_POS i = 0;
	while (i < list->length) {
		pString arg = list->list[i];
		if (arg->length == 0 && i == 0)
			file = current->filesystem->root;
		else if (arg->length == 0 && i == list->length - 1) {
			if (!TSMS_FILESYSTEM_isFolder(file))
				file = TSMS_NULL;
		} else if (TSMS_STRING_equals(arg, TSMS_STRING_PARENT))
			file = file->parent;
		else if (TSMS_STRING_equals(arg, TSMS_STRING_CURRENT)) {
			// ignore
		} else
			file = TSMS_FILESYSTEM_getFile(file, arg);
		TSMS_STRING_release(arg);
		i++;
		if (file == TSMS_NULL)
			break;
	}
	for (; i < list->length; i++)
		TSMS_STRING_release(list->list[i]);
	TSMS_LIST_release(list);
	return file;
}

TSMS_RESULT TSMS_FILESYSTEM_rename(pFile file, pString name) {
	if (file == TSMS_NULL || name == TSMS_NULL)
		return TSMS_ERROR;
	if (file->parent == TSMS_NULL)
		return TSMS_ERROR;
	if (TSMS_STRING_copy(name, file->name) != TSMS_SUCCESS)
		return TSMS_FAIL;
	__internal_tsms_save_header(file);
	return TSMS_SUCCESS;
}

bool TSMS_FILESYSTEM_isParent(pFile parent, pFile child) {
	if (parent == TSMS_NULL || child == TSMS_NULL)
		return false;
	if (!TSMS_FILESYSTEM_isFolder(parent))
		return false;
	if (parent->level >= child->level)
		return false;
	if (parent == child->parent)
		return true;
	return TSMS_FILESYSTEM_isParent(parent, child->parent);
}

TSMS_RESULT TSMS_FILESYSTEM_move(pFile file, pFile dir) {
	if (file == TSMS_NULL || dir == TSMS_NULL)
		return TSMS_ERROR;
	if (file->parent == TSMS_NULL)
		return TSMS_ERROR;
	if (!TSMS_FILESYSTEM_isFolder(dir))
		return TSMS_ERROR;
	if (TSMS_FILESYSTEM_isParent(file, dir))
		return TSMS_ERROR;
	if (file == dir)
		return TSMS_ERROR;
	if (TSMS_MAP_remove(file->parent->files, file->name) != TSMS_SUCCESS)
		return TSMS_FAIL;
	if (TSMS_MAP_put(dir->files, file->name, file) != TSMS_SUCCESS) {
		TSMS_MAP_put(file->parent->files, file->name, file);
		return TSMS_FAIL;
	}
	file->parent = dir;
	__internal_tsms_save_header(file->parent);
	__internal_tsms_save_header(dir);
	return TSMS_SUCCESS;
}

TSMS_RESULT TSMS_FILESYSTEM_copy(pFile file, pFile dir) {
	if (file == TSMS_NULL || dir == TSMS_NULL)
		return TSMS_ERROR;
	if (file->parent == TSMS_NULL)
		return TSMS_ERROR;
	if (!TSMS_FILESYSTEM_isFolder(dir))
		return TSMS_ERROR;
	if (TSMS_FILESYSTEM_isParent(file, dir))
		return TSMS_ERROR;
	if (file == dir)
		return TSMS_ERROR;
	if (TSMS_FILESYSTEM_isFolder(file)) {
		pFile newFile = TSMS_FILESYSTEM_getFile(dir, file->name);
		int cur = 1;
		pString temp = TSMS_STRING_createWithString(file->name->cStr);
		while (newFile != TSMS_NULL) {
			TSMS_STRING_release(temp);
			if (cur > 1000)
				return TSMS_FAIL;
			temp = TSMS_STRING_createWithString(file->name->cStr);
			TSMS_STRING_append(temp, TSMS_STRING_SPACE);
			pString num = TSMS_STRING_createWithInt(cur);
			TSMS_STRING_append(temp, num);
			TSMS_STRING_release(num);
			newFile = TSMS_FILESYSTEM_getFile(dir, temp);
			cur++;
		}
		newFile = TSMS_FILESYSTEM_createFolder(dir, temp, file->options);
		TSMS_STRING_release(temp);
		if (newFile == TSMS_NULL)
			return TSMS_FAIL;
		TSMS_MI iter = TSMS_MAP_iterator(file->files);
		while (TSMS_MAP_hasNext(&iter)) {
			TSMS_ME entry = TSMS_MAP_next(&iter);
			if (TSMS_FILESYSTEM_copy(entry.value, newFile) != TSMS_SUCCESS)
				return TSMS_FAIL;
		}
	} else {
		pFile newFile = TSMS_FILESYSTEM_getFile(dir, file->name);
		int cur = 1;
		pString temp = TSMS_STRING_createWithString(file->name->cStr);
		while (newFile != TSMS_NULL) {
			TSMS_STRING_release(temp);
			if (cur > 1000)
				return TSMS_FAIL;
			temp = TSMS_STRING_createWithString(file->name->cStr);
			TSMS_STRING_append(temp, TSMS_STRING_SPACE);
			pString num = TSMS_STRING_createWithInt(cur);
			TSMS_STRING_append(temp, num);
			TSMS_STRING_release(num);
			newFile = TSMS_FILESYSTEM_getFile(dir, temp);
			cur++;
		}
		newFile = TSMS_FILESYSTEM_createFile(dir, temp, file->options);
		TSMS_STRING_release(temp);
		if (newFile == TSMS_NULL)
			return TSMS_FAIL;
		uint8_t *content = TSMS_FILESYSTEM_readFile(file);
		TSMS_RESULT result = TSMS_FILESYSTEM_writeFile(newFile, content, file->size);
		free(content);
		if (result != TSMS_SUCCESS)
			return TSMS_FAIL;
	}
	return TSMS_SUCCESS;
}

bool TSMS_FILESYSTEM_contentEquals(pFile file1, pFile file2) {
	if (file1 == TSMS_NULL && file2 == TSMS_NULL)
		return true;
	if (file1 == TSMS_NULL || file2 == TSMS_NULL)
		return false;
	if (TSMS_FILESYSTEM_isFolder(file1) || TSMS_FILESYSTEM_isFolder(file2))
		return false;
	if (file1->size != file2->size)
		return false;
	uint8_t *content1 = TSMS_FILESYSTEM_readFile(file1);
	uint8_t *content2 = TSMS_FILESYSTEM_readFile(file2);
	bool result = memcmp(content1, content2, file1->size) == 0;
	free(content1);
	free(content2);
	return result;
}

pFilestream TSMS_FILE_open(pFile file) {
	return TSMS_FILE_openWithMode(file, TSMS_FILE_MODE_READ);
}

pFilestream TSMS_FILE_openWithMode(pFile file, TSMS_FILE_MODE mode) {
	pFilestream stream = (pFilestream) TSMS_malloc(sizeof(tFilestream));
	if (stream == TSMS_NULL)
		return TSMS_NULL;
	stream->file = file;
	stream->mode = mode;
	stream->pos = 0;
	return stream;
}

TSMS_RESULT TSMS_FILE_seek(pFilestream stream, TSMS_POS pos) {
	if (stream == TSMS_NULL)
		return TSMS_ERROR;
	if (pos < 0)
		return TSMS_ERROR;
	// pos > file->size is allowed
	stream->pos = pos;
	return TSMS_SUCCESS;
}

TSMS_RESULT TSMS_FILE_read(pFilestream stream, uint8_t *buffer, TSMS_LSIZE size) {
	if (stream == TSMS_NULL)
		return TSMS_ERROR;
	if (stream->pos > stream->file->size)
		for (TSMS_LSIZE i = 0; i < size; i++)
			buffer[i] = 0;
	else if (stream->pos + size > stream->file->size) {
		uint8_t * b = TSMS_FILESYSTEM_readPartialFile(stream->file, stream->pos, stream->file->size);
		memcpy(buffer, b, stream->file->size - stream->pos);
		TSMS_FILESYSTEM_freeContentBuffer(b);
		for (TSMS_LSIZE i = stream->file->size - stream->pos; i < size; i++)
			buffer[i] = 0;
	} else {
		uint8_t *b = TSMS_FILESYSTEM_readPartialFile(stream->file, stream->pos, stream->pos + size);
		memcpy(buffer, b, size);
		free(b);
	}
	stream->pos += size;
	return TSMS_SUCCESS;
}

TSMS_RESULT TSMS_FILE_write(pFilestream stream, uint8_t *buffer, TSMS_LSIZE size) {
	if (stream == TSMS_NULL)
		return TSMS_ERROR;
	if (stream->mode == TSMS_FILE_MODE_READ)
		return TSMS_ERROR;
	if (stream->pos > stream->file->size) {
		uint8_t *b = TSMS_malloc(sizeof(uint8_t) * (stream->pos - stream->file->size));
		if (b == TSMS_NULL)
			return TSMS_ERROR;
		for (TSMS_LSIZE i = 0; i < stream->pos - stream->file->size; i++)
			b[i] = 0;
		TSMS_FILESYSTEM_insertFile(stream->file, b, stream->file->size, stream->pos - stream->file->size);
		free(b);
	}
	TSMS_RESULT result = TSMS_FILESYSTEM_insertFile(stream->file, buffer, stream->pos, size);
	stream->pos += size;
	return result;
}

TSMS_POS TSMS_FILE_tell(pFilestream stream) {
	if (stream == TSMS_NULL)
		return TSMS_ERROR;
	return stream->pos;
}

TSMS_RESULT TSMS_FILE_close(pFilestream stream) {
	if (stream == TSMS_NULL)
		return TSMS_ERROR;
	free(stream);
	return TSMS_SUCCESS;
}