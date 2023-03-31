# TSMS Filesystem
TSMS filesystem is used to create a filesystem on embedded devices.

## Implementation

### Basic Structure

We have separated the filesystem into two parts: the file and the folder. The folder is used to contain files.
Both of them have a header to store specific information.

The file/folder header contains the following information(we consider this part of the header as the definition-block):

- The magic value: 0x3def11c1 (4 bytes)
- The file/folder name (max 255 bytes)
- One byte 0 to indicate the end of the name
- Four options (4 bytes)
- Zero to eight bytes 0 to align the header to 8 bytes (0 to 7 bytes)
- The file/folder blocks size (4 bytes)

The first bit of the options is used to indicate whether the file/folder is a file or a folder.
The rest of the options are reserved.

Note: the alignment calculation contains the file/folder blocks size.
So the alignment size is 8 - (4 + the file/folder name length + 1 + 4 + 4) % 8. (There should not be 8 bytes alignment)

For the file, the following information is stored(we consider this part of the header as the content-definition-block):

- the file size (8 bytes)
- the file blocks (8 bytes each)

For the folder, the following information is stored(we consider this part of the header as the content-definition-block):

- the folder blocks (8 bytes each)

The max size of the name makes sure that the definition-block size is no more than 272 bytes.
We define the size of the single header block as 512 bytes to make sure we can store the whole definition-block in the first header block.

The content-definition-block is used to store the block position of the file/folder.
The whole header size can exceed 512 bytes as there is no limitation on the file/folder blocks size.
When the size of the header exceeds 512 bytes, the last 8 bytes of the header block is used to store the position of the next header block.

For the file, the content-definition-block is used to store the position of the content blocks.
Each content block contains no more than 4096 bytes of data.
They are contiguous and the last block may not be full and the file size is used to determine the last block size.

For the folder, the content-definition-block is used to store the position of the file/folder first header block.

### Memory Space

We store the first 60 allocated freeing memory space information for both header block and content block. If the count of the stored information exceeds 60, we discard the previous information.

### File/Folder Allocation

If there are some free memory space information stored, we use the first one to allocate the header/content block.

If not, we simply align the last block of the file/folder to 512/4096 bytes and allocate the header/content block after it.

### File/Folder Freeing

Write the first 8 bytes of each block to 0xff to mark it freed as we may discard the information.(So the offset of 0xffffffffffffffff in block position is reserved)

Add the freed block to the stored information.

### File/Folder Defragmentation

As we delete the information of the freed block if the count of the stored information exceeds 60, we need to find out leading 0xffffffffffffffff header/content blocks and fragment them if necessary.

Currently, this work has not been done yet.

### Details

#### Create File

1. Allocate the header block as documented in the File/Folder Allocation section.
2. Fill the definition-block with the magic value, the file name with end 0, the options with first bit 0, the alignment size, and the file blocks size.
3. Fill the content-definition-block with the file size 0 and the empty file blocks.
4. Save the header block.
5. Save the parent folder header block.(add the file header block position to the parent folder content-definition-block)
5. Return the file handle.

#### Create Folder

1. Allocate the header block as documented in the File/Folder Allocation section.
2. Fill the definition-block with the magic value, the folder name with end 0, the options with first bit 1, the alignment size, and the folder blocks size.
3. Fill the content-definition-block with the empty folder blocks.
4. Save the header block.
5. Save the parent folder header block.(add the folder header block position to the parent folder content-definition-block)
5. Return the folder handle.

#### Read File

1. Check this is a file not a folder.
2. Allocate a buffer with the file size.
3. Read the file blocks and copy the data to the buffer.
4. Return the buffer.

#### Read Part of File

1. Check this is a file not a folder.
2. Allocate a buffer with the part size.
3. Calculate the start block position, the end block position and the middle blocks size.
4. Read the start block to the end position to the buffer.
5. Read the middle blocks to the buffer.
6. Read the end block start position to the end to the buffer.
7. Return the buffer.

#### Insert File

1. Check this is a file not a folder.
2. Calculate the start block position, the rest size of the start block and the middle blocks size.
3. Calculate the end block size.
4. Allocate a dynamic buffer.
5. Read the start block to the end position to the buffer.
6. Write rest size of the data to the rest of the start block.
7. Allocate the middle blocks as documented in the File/Folder Allocation section.(as we do not free the file blocks, we can simply reuse them if the new file blocks size is no more than the old one)
8. Write the middle blocks.
9. combine the end block and the buffer.
10. Write the buffer.(may bigger than single content block)

#### Write File

1. Check this is a file not a folder.
2. Empty the file.(mark the file size to 0)
3. Insert the data to the file.

#### Move File/Folder to another Folder

1. Remove the file/folder from the parent folder.
2. Add the file/folder to the new folder.
3. Save the new folder header block.
4. Save the parent folder header block.

#### Copy File/Folder to another Folder

1. Create a new file/folder in the new folder with same name and options.
2. If this is a folder, iteratively copy the files/folders in this folder to the new folder and copy the content-definition-block.
3. If this is a file, write the file to the new file.
4. Save the new file/folder header block.
5. Save the new folder header block.

## Current Status

You can use this filesystem on an existed OS.

## Current Plan

- [ ] Run on bare metal.
- [ ] Rewrite the filesystem in proper way.
- [ ] Add defragmentation.
- [ ] Add more efficient allocation.
- [ ] Add more efficient freeing.

