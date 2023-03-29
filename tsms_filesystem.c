#include "tsms_filesystem.h"

const uint32_t TSMS_FILE_MAGIC = 0x3def11c1;

pFilesystem defaultFilesystem = TSMS_NULL;

pString TSMS_STRING_ROOT;

pString TSMS_STRING_CURRENT;

pString TSMS_STRING_PARENT;

uint8_t filenameBuffer[256];

uint8_t contentBuffer[TSMS_FILE_CONTENT_BLOCK];

const uint8_t TSMS_FILE_EMPTY_CONTENT[0];

TSMS_INLINE void __internal_tsms_seek(void* handle, long offset) {
#ifdef STM32
#else
	fseek(handle, offset, SEEK_SET);
#endif
}

TSMS_INLINE void __internal_tsms_write(void* handle, const void * ptr, size_t size) {
#ifdef STM32
#else
	fwrite(ptr, size, 1, handle);
#endif
}

TSMS_INLINE void __internal_tsms_read(void* handle, void * ptr, size_t size) {
#ifdef STM32
#else
	fread(ptr, size, 1, handle);
#endif
}

TSMS_INLINE void __internal_tsms_dealloc(pFilesystem fs, long offset) {
	// todo
}

TSMS_INLINE void __internal_tsms_mark_free(pFilesystem fs, long offset) {
	__internal_tsms_seek(fs->native, offset);
	uint8_t t = 0xff;
	for (TSMS_POS i = 0; i < 8;i++)
		__internal_tsms_write(fs->native, &t, 1);
	__internal_tsms_dealloc(fs, offset);
}

TSMS_INLINE TSMS_SIZE __internal_tsms_calc_align_bytes(pFile file) {
	TSMS_SIZE align = 8 - (4 + file->name->length + 1 + 4 + 8) % 8;
	return align == 8 ? 0 : align;
}

TSMS_INLINE TSMS_SIZE __internal_tsms_calc_definition_block_size(pFile file) {
	return 4 + file->name->length + 1 + 4 + 8 + __internal_tsms_calc_align_bytes(file);
}

TSMS_INLINE TSMS_SIZE __internal_tsms_calc_header_size(pFile file) {
	return __internal_tsms_calc_definition_block_size(file) + (TSMS_FILESYSTEM_isFolder(file) ? 8 * file->files->size : 8 * file->blocks->length * 8 + 8);
}



TSMS_INLINE long __internal_tsms_alloc_content_block(pFilesystem filesystem) { // todo
#ifdef STM32
	return 0;
#else
	long cur = ftell(filesystem->native);
	fseek(filesystem->native, 0 , SEEK_END);
	long end = ftell(filesystem->native);
	fseek(filesystem->native, cur, SEEK_SET);
	if (end % TSMS_FILE_CONTENT_BLOCK == 0) {
		printf("Allocating new block at %ld\n", end);
		return end;
	}
	printf("Allocating new block at %ld\n", end + (TSMS_FILE_CONTENT_BLOCK - (end % TSMS_FILE_CONTENT_BLOCK)));
	return (TSMS_FILE_CONTENT_BLOCK - (end % TSMS_FILE_CONTENT_BLOCK)) + end;
#endif
}

TSMS_INLINE long __internal_tsms_alloc_header_block(pFilesystem filesystem) { // todo
#ifdef STM32
	return 0;
#else
	long cur = ftell(filesystem->native);
	fseek(filesystem->native, 0 , SEEK_END);
	long end = ftell(filesystem->native);
	fseek(filesystem->native, cur, SEEK_SET);
	if (end % TSMS_FILE_HEADER_BLOCK == 0) {
		printf("Allocating new block at %ld\n", end);
		return end;
	}
	printf("Allocating new block at %ld\n", end + (TSMS_FILE_HEADER_BLOCK - (end % TSMS_FILE_HEADER_BLOCK)));
	return (TSMS_FILE_HEADER_BLOCK - (end % TSMS_FILE_HEADER_BLOCK)) + end;
#endif
}





TSMS_INLINE void __internal_tsms_check_and_free(uint8_t * content) {
	if (content != TSMS_FILE_EMPTY_CONTENT)
		free(content);
}

TSMS_INLINE bool __internal_tsms_check_file_name(pFilesystem fs, pString name) {
	if (name->length > TSMS_FILE_NAME_MAX_LENGTH)
		return false;
	for (TSMS_SIZE i = 0; i < name->length; i++)
		if (name->cStr[i] == fs->split->cStr[0] || name->cStr[i] == ':' || name->cStr[i] == '*' || name->cStr[i] == '?' || name->cStr[i] == '"' || name->cStr[i] == '<' || name->cStr[i] == '>' || name->cStr[i] == '|')
			return false;
	return true;
}

TSMS_INLINE void __internal_tsms_load_file(long offset, pFile file);
TSMS_INLINE TSMS_SIZE __internal_tsms_calc_header(pFile file) {
	TSMS_SIZE length = 5 + file->name->length + 8; // 5 = 4 (magic) + 1 (string ends with 0)
	TSMS_SIZE extra = length % TSMS_FILE_UNIT;
	if (extra != 0)
		length += TSMS_FILE_UNIT - extra;
	return length;
}

TSMS_INLINE void __internal_tsms_save_header(pFile file) {
	__internal_tsms_seek(file->filesystem->native, file->offset);
	__internal_tsms_write(file->filesystem->native, &TSMS_FILE_MAGIC, sizeof (uint32_t));
	for (TSMS_POS i = 0;i < file->name->length; i++)
		__internal_tsms_write(file->filesystem->native, file->name->cStr + i, sizeof(char));
	uint8_t t = 0;
	__internal_tsms_write(file->filesystem->native, &t, sizeof(uint8_t));
	for (TSMS_POS i = 0;i < 4;i++)
		__internal_tsms_write(file->filesystem->native, file->options + i, sizeof (TSMS_FILE_OPTION));
	TSMS_SIZE align = __internal_tsms_calc_align_bytes(file);
	for (TSMS_POS i = 0;i<align;i++)
		__internal_tsms_write(file->filesystem->native, &t, sizeof(uint8_t));
	TSMS_SIZE currentSize = __internal_tsms_calc_definition_block_size(file);
	TSMS_SIZE totalSize = __internal_tsms_calc_header_size(file);
	TSMS_SIZE blockSize = totalSize % (TSMS_FILE_HEADER_BLOCK - 8) == 0 ? totalSize / (TSMS_FILE_HEADER_BLOCK - 8) : totalSize / (TSMS_FILE_HEADER_BLOCK - 8) + 1;
	for (TSMS_POS i = file->anchors->length; i < blockSize - 1;i++)
		TSMS_LONG_LIST_add(file->anchors, __internal_tsms_alloc_content_block(file->filesystem));
	TSMS_POS anchorPos = 0;
	if (TSMS_FILESYSTEM_isFolder(file)) {
		TSMS_LSIZE size = file->files->size;
		__internal_tsms_write(file->filesystem->native, &size, sizeof(TSMS_LSIZE));
		TSMS_MI iter = TSMS_MAP_iterator(file->files);
		while(TSMS_MAP_hasNext(&iter)) {
			if (((currentSize + 8) % TSMS_FILE_HEADER_BLOCK) == 0) {
				long offset = file->anchors->list[anchorPos++];
				__internal_tsms_write(file->filesystem->native, &offset, sizeof(long));
				currentSize += 8;
			}
			TSMS_ME entry = TSMS_MAP_next(&iter);
			pFile f = entry.value;
			long offset = f->offset;
			__internal_tsms_write(file->filesystem->native, &offset, sizeof(long));
			currentSize += 8;
		}
	} else {
		TSMS_LSIZE size = file->blocks->length;
		__internal_tsms_write(file->filesystem->native, &size, sizeof(TSMS_LSIZE));
		size = file->size;
		__internal_tsms_write(file->filesystem->native, &size, sizeof(TSMS_LSIZE));
		for (TSMS_POS i = 0; i < file->blocks->length;i++) {
			if (((currentSize + 8) % TSMS_FILE_HEADER_BLOCK) == 0) {
				long offset = file->anchors->list[anchorPos++];
				__internal_tsms_write(file->filesystem->native, &offset, sizeof(long));
				currentSize += 8;
			}
			long offset = file->blocks->list[i];
			__internal_tsms_write(file->filesystem->native, &offset, sizeof(long));
			currentSize += 8;
		}
	}
	for (TSMS_POS i = anchorPos; i < file->anchors->length;i++)
		__internal_tsms_mark_free(file->filesystem, file->anchors->list[i]);
}

TSMS_INLINE pFile __internal_tsms_read_file(pFilesystem filesystem, long offset, pFile parent, bool deep);

TSMS_INLINE void __internal_tsms_load_file(long offset, pFile file) {
#ifdef TSMS_STM32
#else
	TSMS_SIZE headerLength = __internal_tsms_calc_header(file);
	fseek(file->filesystem->native, offset + headerLength - 4, SEEK_SET);
	TSMS_SIZE childLength;
	fread(&childLength, sizeof(TSMS_SIZE), 1, file->filesystem->native);
	file->anchors = TSMS_LONG_LIST_create(10);
	if (TSMS_FILESYSTEM_isFolder(file)) {
		file->files = TSMS_LONG_MAP_create(255);
		file->blocks = TSMS_NULL;
	} else {
		file->files = TSMS_NULL;
		file->blocks = TSMS_LONG_LIST_create(10);
	}
	long childPosition;
	for (TSMS_POS i = 0;i < childLength;i++) {
		fread(&childPosition, sizeof(long), 1, file->filesystem->native);
		if ((headerLength + TSMS_FILE_UNIT) % TSMS_FILE_HEADER_BLOCK == 0) {
			TSMS_LONG_LIST_add(file->anchors, childPosition);
			fseek(file->filesystem->native, childPosition, SEEK_SET);
			headerLength += TSMS_FILE_UNIT;
			fread(&childPosition, sizeof(long), 1, file->filesystem->native);
		}
		if (TSMS_FILESYSTEM_isFolder(file)) {
			long cur = ftell(file->filesystem->native);
			pFile child = __internal_tsms_read_file(file->filesystem, childPosition, false);
			child->level = file->level + 1;
			fseek(file->filesystem->native, cur, SEEK_SET);
			child->parent = file;
			TSMS_LONG_MAP_put(file->files, childPosition, child);
		} else {
			if (file->size == -1)
				file->size = childPosition;
			else TSMS_LONG_LIST_add(file->blocks, childPosition);
		}
		headerLength += TSMS_FILE_UNIT;
	}
	file->loaded = true;
#endif
}

TSMS_INLINE void __internal_tsms_deep_load_file(pFile file) {
	if (file->loaded)
		return;
	TSMS_SIZE currentSize = __internal_tsms_calc_definition_block_size(file);
	TSMS_SIZE totalSize = __internal_tsms_calc_header(file);
	TSMS_LSIZE size;
	__internal_tsms_seek(file->filesystem->native, file->offset + currentSize - 8);
	__internal_tsms_read(file->filesystem->native, &size, sizeof(TSMS_LSIZE));
	if (TSMS_FILESYSTEM_isFolder(file)) {
		file->files = TSMS_MAP_create(255, (TSMS_MAP_HASH_FUNCTION) TSMS_STRING_hash,
		                              (TSMS_MAP_COMPARE_FUNCTION) TSMS_STRING_compare);
		file->blocks = TSMS_NULL;


	} else {
		file->files = TSMS_NULL;
		file->blocks = TSMS_LONG_LIST_create(10);
	}

	file->loaded = true;
}

TSMS_INLINE pFile __internal_tsms_read_file(pFilesystem filesystem, long offset, pFile parent, bool deep) {
	pFile file = malloc(sizeof(tFile));
	if (file == TSMS_NULL)
		return TSMS_NULL;

	__internal_tsms_seek(filesystem->native, offset);
	uint32_t magic;
	__internal_tsms_read(filesystem->native, &magic, sizeof (uint32_t));

	if (magic != TSMS_FILE_MAGIC) {
		free(file);
		return TSMS_NULL;
	}

	file->filesystem = filesystem;

	file->offset = offset;
	file->parent = parent;
	file->anchors = TSMS_LONG_LIST_create(10);

	file->level = parent == TSMS_NULL ? 0 : parent->level + 1;
	file->loaded = false;

	TSMS_SIZE length = 0;
	uint8_t c;
	while(length < TSMS_FILE_NAME_MAX_LENGTH) {
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
	file->name = TSMS_STRING_createAndInit(filenameBuffer);
	for (TSMS_POS i = 0;i < 4;i++)
		__internal_tsms_read(filesystem->native, file->options + i, sizeof (TSMS_FILE_OPTION));
	TSMS_SIZE align = __internal_tsms_calc_align_bytes(file);
	for (TSMS_POS i = 0;i<align;i++)
		__internal_tsms_read(filesystem->native, &c, sizeof(uint8_t));

	file->size = -1;

	if (deep)
		__internal_tsms_deep_load_file(file);

	return file;
}

TSMS_INLINE pFile __internal_tsms_create_file(pFilesystem filesystem, long offset, pString name, TSMS_FILE_TYPE type, pFile parent, const uint8_t * options) {
	pFile file = malloc(sizeof(tFile));
	if (file == TSMS_NULL)
		return TSMS_NULL;
	file->filesystem = filesystem;

	file->offset = offset;
	file->parent = parent;
	file->anchors = TSMS_LONG_LIST_create(10);

	file->level = parent == TSMS_NULL ? 0 : parent->level + 1;
	file->loaded = true;

	file->name = TSMS_STRING_static(name->cStr);
	if (parent != TSMS_NULL)
		TSMS_MAP_put(parent->files, file->name , file);
	if (options != TSMS_NULL)
		for (int i = 0;i<4;i++)
			file->options[i] = options[i];
	file->options[0] &= 0x7f;
	file->options[0] |= type << 7;
	for (int i = 1;i<4;i++)
		file->options[i] = 0;
	if (type == TSMS_FILE_TYPE_FOLDER) {
		file->files = TSMS_MAP_create(255, (TSMS_MAP_HASH_FUNCTION) TSMS_STRING_hash,
		                              (TSMS_MAP_COMPARE_FUNCTION) TSMS_STRING_compare);
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
	if (TSMS_STRING_ROOT == TSMS_NULL || TSMS_STRING_CURRENT == TSMS_NULL || TSMS_STRING_PARENT == TSMS_NULL)
		return TSMS_ERROR;
	TSMS_FILESYSTEM_setDefaultFilesystem(TSMS_FILESYSTEM_createFilesystem('/'));
	return TSMS_SUCCESS;
}

TSMS_RESULT TSMS_FILESYSTEM_setDefaultFilesystem(pFilesystem filesystem) {
	defaultFilesystem = filesystem;
	return TSMS_SUCCESS;
}

pFilesystem TSMS_FILESYSTEM_createFilesystem(char split) {
	pFilesystem filesystem = malloc(sizeof(tFilesystem));
	filesystem->split = TSMS_STRING_createAndInitChar(split);
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
		filesystem->root = __internal_tsms_read_file(filesystem, 0, TSMS_NULL, true);
		if (filesystem->root == TSMS_NULL) {
			TSMS_FILESYSTEM_release(filesystem);
			return TSMS_NULL;
		}
		filesystem->root->level = 0;
	} else {
		filesystem->native = fopen("filesystem", "wr");
		if (filesystem->native == TSMS_NULL) {
			TSMS_FILESYSTEM_release(filesystem);
			return TSMS_NULL;
		}
		filesystem->root = __internal_tsms_create_file(filesystem, 0, TSMS_STRING_ROOT, TSMS_FILE_TYPE_FOLDER, TSMS_NULL, 0);
		if (filesystem->root == TSMS_NULL) {
			TSMS_FILESYSTEM_release(filesystem);
			return TSMS_NULL;
		}
	}
#endif
	return filesystem;
}

pFile TSMS_FILESYSTEM_createFolder(pFile parent, pString name, uint8_t * options) {
	pFile file = TSMS_FILESYSTEM_getFile(parent, name);
	if (file != TSMS_NULL && TSMS_FILESYSTEM_isFolder(file))
		return file;
	if (file != TSMS_NULL)
		return TSMS_NULL;
	if (!__internal_tsms_check_file_name(parent->filesystem, name))
		return TSMS_NULL;
	long offset = __internal_tsms_alloc_header_block(parent->filesystem);
	return __internal_tsms_create_file(parent->filesystem, offset, name, TSMS_FILE_TYPE_FOLDER, parent, options);
}

pFile TSMS_FILESYSTEM_createFile(pFile parent, pString name, uint8_t * options) {
	pFile file = TSMS_FILESYSTEM_getFile(parent, name);
	if (file != TSMS_NULL && !TSMS_FILESYSTEM_isFolder(file))
		return file;
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
#ifdef TSMS_STM32
#else
	if (file->loaded) {
		if (TSMS_FILESYSTEM_isFolder(file)) {
			TSMS_LMI it = TSMS_LONG_MAP_iterator(file->files);
			while (TSMS_LONG_MAP_hasNext(&it)) {
				TSMS_LME entry = TSMS_LONG_MAP_next(&it);
				pFile child = entry.value;
				TSMS_RESULT result = TSMS_FILESYSTEM_releaseFile(child);
				if (result != TSMS_SUCCESS)
					return result;
			}
			TSMS_LONG_MAP_release(file->files);
		} else
			TSMS_LONG_LIST_release(file->blocks);
		TSMS_LONG_LIST_release(file->anchors);
	}
	TSMS_STRING_release(file->name); // does not matter if it is root
	free(file);
	return TSMS_SUCCESS;
#endif
}

TSMS_RESULT TSMS_FILESYSTEM_release(pFilesystem filesystem) {
	if (filesystem == TSMS_NULL)
		return TSMS_ERROR;
	TSMS_STRING_release(filesystem->split);
#ifdef TSMS_STM32
#else
	fclose(filesystem->native);
#endif
	TSMS_FILESYSTEM_releaseFile(filesystem->root);
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
	__internal_tsms_deep_load_file(child);
	return child;
}

uint8_t *TSMS_FILESYSTEM_readFile(pFile file) {
	if (file == TSMS_NULL)
		return TSMS_NULL;
	if (TSMS_FILESYSTEM_isFolder(file))
		return TSMS_NULL;
#ifdef TSMS_STM32
	return TSMS_NULL;
#else
	TSMS_LSIZE size = file->size;
	uint8_t * buffer = TSMS_NULL;
	TSMS_LSIZE bufferLength = 0;
	for (TSMS_POS i = 0; i< file->blocks->length; i++) {
		TSMS_SIZE block = min(size, TSMS_FILE_CONTENT_BLOCK);
		fseek(file->filesystem->native, file->blocks->list[i], SEEK_SET);
		fread(contentBuffer, block, 1, file->filesystem->native);
		buffer = TSMS_UTIL_streamAppend(buffer, bufferLength, contentBuffer, block);
		size -= block;
		bufferLength += block;
	}
	return buffer;
#endif
}

uint8_t *TSMS_FILESYSTEM_readPartialFile(pFile file, TSMS_POS start, TSMS_POS end) {
	if (file == TSMS_NULL)
		return TSMS_NULL;
	if (TSMS_FILESYSTEM_isFolder(file))
		return TSMS_NULL;
	if (start > end)
		return TSMS_NULL;
	if (end > file->size)
		return TSMS_NULL;
	if (start < 0)
		return TSMS_NULL;
#ifdef TSMS_STM32
	return TSMS_NULL;
#else
	TSMS_LSIZE size = end - start;
	if (size == 0)
		return TSMS_FILE_EMPTY_CONTENT;
	TSMS_POS pos = start / TSMS_FILE_CONTENT_BLOCK;
	if (size <= (TSMS_FILE_CONTENT_BLOCK - (start % TSMS_FILE_CONTENT_BLOCK))) {
		fseek(file->filesystem->native, file->blocks->list[pos] + (start % TSMS_FILE_CONTENT_BLOCK), SEEK_SET);
		fread(contentBuffer, size, 1, file->filesystem->native);
		uint8_t* buffer = TSMS_NULL;
		buffer = TSMS_UTIL_streamAppend(buffer, 0, contentBuffer, size);
		return buffer;
	}
	fseek(file->filesystem->native, file->blocks->list[pos] + (start % TSMS_FILE_CONTENT_BLOCK), SEEK_SET);
	fread(contentBuffer, TSMS_FILE_CONTENT_BLOCK - (start % TSMS_FILE_CONTENT_BLOCK), 1, file->filesystem->native);
	uint8_t * buffer = TSMS_NULL;
	TSMS_LSIZE bufferLength = TSMS_FILE_CONTENT_BLOCK - (start % TSMS_FILE_CONTENT_BLOCK);
	TSMS_UTIL_streamAppend(buffer, 0, contentBuffer, TSMS_FILE_CONTENT_BLOCK - (start % TSMS_FILE_CONTENT_BLOCK));
	size -= TSMS_FILE_CONTENT_BLOCK - (start % TSMS_FILE_CONTENT_BLOCK);
	for (TSMS_POS i = pos + 1; i < file->blocks->length; i++) {
		TSMS_SIZE block = min(size, TSMS_FILE_CONTENT_BLOCK);
		fseek(file->filesystem->native, file->blocks->list[i], SEEK_SET);
		fread(contentBuffer, block, 1, file->filesystem->native);
		buffer = TSMS_UTIL_streamAppend(buffer, bufferLength, contentBuffer, block);
		size -= block;
		bufferLength += block;
		if (size == 0)
			break;
	}
	return buffer;
#endif
}

TSMS_RESULT TSMS_FILESYSTEM_writeFile(pFile file, uint8_t *content, TSMS_LSIZE size) {
	if (file == TSMS_NULL || content == TSMS_NULL)
		return TSMS_ERROR;
	if (TSMS_FILESYSTEM_emptyFile(file) != TSMS_SUCCESS)
		return TSMS_ERROR;
	return TSMS_FILESYSTEM_insertFile(file, content,  size,0);
}


TSMS_RESULT TSMS_FILESYSTEM_insertFile(pFile file, uint8_t *content, TSMS_LSIZE ssize, TSMS_POS pos) {
	if (file == TSMS_NULL || content == TSMS_NULL)
		return TSMS_ERROR;
	if (TSMS_FILESYSTEM_isFolder(file))
		return TSMS_ERROR;
	if (pos > file->size)
		return TSMS_ERROR;
#ifdef TSMS_STM32
	return TSMS_FAIL;
#else
	if (pos % TSMS_FILE_CONTENT_BLOCK == 0) {
		TSMS_SIZE length = ssize;
		uint8_t *rest;
		if (length % TSMS_FILE_CONTENT_BLOCK == 0)
			rest = TSMS_NULL;
		else {
			rest = TSMS_FILESYSTEM_readPartialFile(file, pos, file->size);
			if (rest == TSMS_NULL)
				return TSMS_FAIL;
		}
		TSMS_POS blockPos = pos / TSMS_FILE_CONTENT_BLOCK;
		TSMS_SIZE size = length / TSMS_FILE_CONTENT_BLOCK;
		for (TSMS_POS i = 0; i < size; i++) {
			long offset = __internal_tsms_alloc_content_block(file->filesystem);
			if (TSMS_LONG_LIST_insert(file->blocks, offset , blockPos + i) != TSMS_SUCCESS) {
				file->size += ssize - length;
				__internal_tsms_save_header(file->offset, file);
				return TSMS_FAIL;
			}
			memcpy(contentBuffer, content + i * TSMS_FILE_CONTENT_BLOCK, TSMS_FILE_CONTENT_BLOCK);
			fseek(file->filesystem->native, offset, SEEK_SET);
			fwrite(contentBuffer, TSMS_FILE_CONTENT_BLOCK, 1, file->filesystem->native);
			length -= TSMS_FILE_CONTENT_BLOCK;
		}
		if (length > 0) {
			// align the rest
			uint8_t *sub = TSMS_NULL;
			sub = TSMS_UTIL_streamAppend(sub, 0, content + size * TSMS_FILE_CONTENT_BLOCK, length);
			if (sub == TSMS_NULL) {
				__internal_tsms_check_and_free(rest);
				file->size += ssize - length;
				__internal_tsms_save_header(file->offset, file);
				return TSMS_FAIL;
			}
			sub = TSMS_UTIL_streamAppend(sub, length, rest, file->size - pos);
			if (sub == TSMS_NULL) {
				__internal_tsms_check_and_free(rest);
				file->size += ssize - length;
				__internal_tsms_save_header(file->offset, file);
				return TSMS_FAIL;
			}
			__internal_tsms_check_and_free(rest);
			TSMS_SIZE restSize = (length + file->size - pos) / TSMS_FILE_CONTENT_BLOCK;
			length += file->size - pos;
			TSMS_POS i;
			for (i = blockPos + size; i < file->blocks->length; i++) {
				long offset = file->blocks->list[i];
				TSMS_SIZE block = min(length, TSMS_FILE_CONTENT_BLOCK);
				memcpy(contentBuffer, sub + (i - blockPos - size) * TSMS_FILE_CONTENT_BLOCK, block);
				fseek(file->filesystem->native, offset, SEEK_SET);
				fwrite(contentBuffer, block, 1, file->filesystem->native);
				length -= block;
				restSize--;
				if (length == 0)
					break;
			}
			if (length == 0)
				TSMS_LONG_LIST_truncate(file->blocks, i + 1);
			else {
				TSMS_SIZE previousSize = file->blocks->length;
				for (i = 0; i < restSize; i++) {
					long offset = __internal_tsms_alloc_content_block(file->filesystem);
					if (TSMS_LONG_LIST_add(file->blocks, offset) != TSMS_SUCCESS) {
						free(sub);
						file->size += ssize - length;
						__internal_tsms_save_header(file->offset, file);
						return TSMS_FAIL;
					}
					memcpy(contentBuffer, sub + (i + previousSize - blockPos - size) * TSMS_FILE_CONTENT_BLOCK,
					        TSMS_FILE_CONTENT_BLOCK);
					fseek(file->filesystem->native, offset, SEEK_SET);
					fwrite(contentBuffer, TSMS_FILE_CONTENT_BLOCK, 1, file->filesystem->native);
					length -= TSMS_FILE_CONTENT_BLOCK;
				}
				if (length > 0) {
					long offset = __internal_tsms_alloc_content_block(file->filesystem);
					if (TSMS_LONG_LIST_add(file->blocks, offset) != TSMS_SUCCESS) {
						free(sub);
						file->size += ssize - length;
						__internal_tsms_save_header(file->offset, file);
						return TSMS_FAIL;
					}
					memcpy(contentBuffer,
					        sub + (restSize + previousSize - blockPos - size) * TSMS_FILE_CONTENT_BLOCK, length);
					fseek(file->filesystem->native, offset, SEEK_SET);
					fwrite(contentBuffer, length, 1, file->filesystem->native);
				}
			}
			free(sub);
		}
		file->size += ssize;
	} else {
		// can directly go here, but the first if check is for optimization
		uint8_t *rest1 = TSMS_FILESYSTEM_readPartialFile(file, pos / TSMS_FILE_CONTENT_BLOCK * TSMS_FILE_CONTENT_BLOCK, pos);
		uint8_t *rest2 = TSMS_FILESYSTEM_readPartialFile(file, pos, file->size);
		if (rest1 == TSMS_NULL || rest2 == TSMS_NULL) {
			__internal_tsms_check_and_free(rest1);
			__internal_tsms_check_and_free(rest2);
			return TSMS_FAIL;
		}
		uint8_t *tmp = TSMS_UTIL_streamAppend(rest1, pos % TSMS_FILE_CONTENT_BLOCK, content, ssize);
		if (tmp == TSMS_NULL ) {
			__internal_tsms_check_and_free(rest1);
			__internal_tsms_check_and_free(rest2);
			return TSMS_FAIL;
		}
		rest1 = tmp;
		tmp = TSMS_UTIL_streamAppend(rest1, pos % TSMS_FILE_CONTENT_BLOCK + ssize, rest2, file->size - pos);
		if (tmp == TSMS_NULL) {
			__internal_tsms_check_and_free(rest1);
			__internal_tsms_check_and_free(rest2);
			return TSMS_FAIL;
		}
		rest1 = tmp;
		__internal_tsms_check_and_free(rest2);
		TSMS_POS blockPos = pos / TSMS_FILE_CONTENT_BLOCK;
		TSMS_SIZE rawLength = pos % TSMS_FILE_CONTENT_BLOCK + ssize + file->size - pos;
		TSMS_SIZE length = pos % TSMS_FILE_CONTENT_BLOCK + ssize + file->size - pos;
		TSMS_SIZE size = length / TSMS_FILE_CONTENT_BLOCK;
		TSMS_POS i;
		for (i = blockPos; i < file->blocks->length; i++) {
			long offset = file->blocks->list[i];
			TSMS_SIZE block = min(length, TSMS_FILE_CONTENT_BLOCK);
			memcpy(contentBuffer, rest1 + (i - blockPos) * TSMS_FILE_CONTENT_BLOCK, block);
			fseek(file->filesystem->native, offset, SEEK_SET);
			fwrite(contentBuffer, block, 1, file->filesystem->native);
			length -= block;
			size--;
			if (length == 0)
				break;
		}
		if (length == 0)
			TSMS_LONG_LIST_truncate(file->blocks, i + 1);
		else {
			TSMS_SIZE previousSize = file->blocks->length;
			for (i = 0; i < size; i++) {
				long offset = __internal_tsms_alloc_content_block(file->filesystem);
				if (TSMS_LONG_LIST_add(file->blocks, offset) != TSMS_SUCCESS) {
					__internal_tsms_check_and_free(rest1);
					file->size = pos / TSMS_FILE_CONTENT_BLOCK * TSMS_FILE_CONTENT_BLOCK + rawLength - length;
					__internal_tsms_save_header(file->offset, file);
					return TSMS_FAIL;
				}
				memcpy(contentBuffer, rest1 + (i + previousSize - blockPos) * TSMS_FILE_CONTENT_BLOCK,
				        TSMS_FILE_CONTENT_BLOCK);
				fseek(file->filesystem->native, offset, SEEK_SET);
				fwrite(contentBuffer, TSMS_FILE_CONTENT_BLOCK, 1, file->filesystem->native);
				length -= TSMS_FILE_CONTENT_BLOCK;
			}
			if (length > 0) {
				long offset = __internal_tsms_alloc_content_block(file->filesystem);
				if (TSMS_LONG_LIST_add(file->blocks, offset) != TSMS_SUCCESS) {
					__internal_tsms_check_and_free(rest1);
					file->size = pos / TSMS_FILE_CONTENT_BLOCK * TSMS_FILE_CONTENT_BLOCK + rawLength - length;
					__internal_tsms_save_header(file->offset, file);
					return TSMS_FAIL;
				}
				memcpy(contentBuffer, rest1 + (size + previousSize - blockPos) * TSMS_FILE_CONTENT_BLOCK,
				        length);
				fseek(file->filesystem->native, offset, SEEK_SET);
				fwrite(contentBuffer, length, 1, file->filesystem->native);
			}
		}
		__internal_tsms_check_and_free(rest1);
		file->size += ssize;
	}
	__internal_tsms_save_header(file->offset, file);
	return TSMS_SUCCESS;
#endif
}

TSMS_RESULT TSMS_FILESYSTEM_emptyFile(pFile file) {
	if (file == TSMS_NULL)
		return TSMS_ERROR;
	if (TSMS_FILESYSTEM_isFolder(file))
		return TSMS_ERROR;
	file->size = 0;
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
		TSMS_LMI it = TSMS_LONG_MAP_iterator(file->files);
		while (TSMS_LONG_MAP_hasNext(&it)) {
			TSMS_LME entry = TSMS_LONG_MAP_next(&it);
			pFile child = entry.value;
			if (!child->loaded)
				__internal_tsms_load_file(entry.key, child);
			size += TSMS_FILESYSTEM_size(child);
		}
		return file->size = size;
	}
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
	TSMS_MI it = TSMS_STRING_MAP_iterator(file->files);
	while (TSMS_STRING_MAP_hasNext(&it)) {
		TSMS_SME entry = TSMS_STRING_MAP_next(&it);
		pFile child = entry.value;
		__internal_tsms_deep_load_file(child);
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
	TSMS_STRING_MAP_remove(file->parent->files, file->name);
	__internal_tsms_save_header(file->parent);
	__internal_tsms_mark_free(file->offset);
	for (TSMS_POS i = 0; i < file->anchors->length;i++)
		__internal_tsms_mark_free(file->anchors->list[i]);
	TSMS_FILESYSTEM_releaseFile(file);
	return TSMS_SUCCESS;
}

TSMS_RESULT TSMS_FILESYSTEM_deleteFolder(pFile file) {
	if (file == TSMS_NULL)
		return TSMS_ERROR;
	if (!TSMS_FILESYSTEM_isFolder(file))
		return TSMS_ERROR;
	if (file->parent == TSMS_NULL)
		return TSMS_ERROR;
	if (TSMS_LONG_MAP_remove(file->parent->files, file->offset) != TSMS_SUCCESS)
		return TSMS_FAIL;
	__internal_tsms_save_header(file->parent->offset, file->parent);
	fseek(file->filesystem->native, file->offset, SEEK_SET);
	for (TSMS_POS i = 0; i < TSMS_FILE_HEADER_BLOCK; i++)
		fputc(0, file->filesystem->native);
	// write 0 is not necessary
	TSMS_FILESYSTEM_releaseFile(file);
	return TSMS_SUCCESS;
}

pFile TSMS_FILESYSTEM_resolve(pFile current, pString path) {
	if (current == TSMS_NULL || path == TSMS_NULL)
		return TSMS_NULL;
	TSMS_LP list = TSMS_STRING_split(path, current->filesystem->split->cStr[0]);
	pFile file = current;
	TSMS_POS i;
	for (i = 0; i < list->length;i++) {
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
	__internal_tsms_save_header(file->offset, file);
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
	if (TSMS_LONG_MAP_remove(file->parent->files, file->offset) != TSMS_SUCCESS)
		return TSMS_FAIL;
	if (TSMS_LONG_MAP_put(dir->files, file->offset, file) != TSMS_SUCCESS) {
		TSMS_LONG_MAP_put(file->parent->files, file->offset, file);
		return TSMS_FAIL;
	}
	file->parent = dir;
	__internal_tsms_save_header(file->parent->offset, file->parent);
	__internal_tsms_save_header(dir->offset, dir);
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
		if (newFile != TSMS_NULL)
			return TSMS_FAIL;
		newFile = TSMS_FILESYSTEM_createFolder(dir, file->name, file->options);
		if (newFile == TSMS_NULL)
			return TSMS_FAIL;
		TSMS_LMI iter = TSMS_LONG_MAP_iterator(file->files);
		while (TSMS_LONG_MAP_hasNext(&iter)) {
			TSMS_LME entry = TSMS_LONG_MAP_next(&iter);
			if (TSMS_FILESYSTEM_copy(entry.value, newFile) != TSMS_SUCCESS)
				return TSMS_FAIL;
		}
	} else {
		pFile newFile = TSMS_FILESYSTEM_getFile(dir, file->name);
		if (newFile != TSMS_NULL)
			return TSMS_FAIL;
		newFile = TSMS_FILESYSTEM_createFile(dir, file->name, file->options);
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