#include <iostream>
#include "fs.h"
#include <string>
#include <cstring>

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
    int16_t blk[BLOCK_SIZE];
    disk.read(FAT_BLOCK, (uint8_t*)blk);

    for(int i = 0; i < BLOCK_SIZE/2; i++)
        fat[i] = blk[i];
}

FS::~FS()
{

}

// formats the disk, i.e., creates an empty file system
int
FS::format()
{
    fat[0] = FAT_EOF;
    fat[1] = FAT_EOF;
    for(int i = 2; i < BLOCK_SIZE/2; i++){
        fat[i] = FAT_FREE;
    }

    dir_entry blk[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, (uint8_t*)blk);

    // Reset all dir_entries in root folder to start on block 0 so they do not show up
    for(int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){
        blk[i].first_blk = 0;
    }

    disk.write(ROOT_BLOCK, (uint8_t*)blk);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    std::cout << "Formatted the disk successfully\n";
    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int
FS::create(std::string filepath)
{
    if(filepath.length() > 55){ // 56 - 1, -1 because string::length() does not take the \0 into account
        std::cout << "Filename too long. The name of a file can be at most be 56 characters long\n";
        return 1;
    }
    if(file_exists(filepath) != -1){
      std::cout << "File \"" << filepath << "\" already exists.\n";
      return 1;
    }

    std::string accum;
    std::string line;
    for(;;){
        std::getline(std::cin, line);

        if(line.empty()){
            break;
        } else {
            if(!accum.empty())
                accum += "\n";
            accum += line;
        }
    }

    // Write a line accross the screen to indicate the end of the file for the user
    std::cout << "------------------------------------------------\n";

    // Write data
    char *c_accum = (char*)accum.c_str();
    int bytes_to_write = accum.length() + 1;
    int first_block = -1, block = -1, previous_block = -1;
    while(bytes_to_write > 0){ // While there's data to write

        // Find an empty block
        block = find_empty_block_id();

        disk.write(block, (uint8_t*)c_accum);

        fat[block] = FAT_EOF;
        bytes_to_write -= BLOCK_SIZE;
        c_accum += BLOCK_SIZE;

        if(previous_block > 0)
            fat[previous_block] = block;

        if(first_block == -1)
            first_block = block;
        previous_block = block;
    }

    // Update directory data
    dir_entry blk[BLOCK_SIZE];

    disk.read(ROOT_BLOCK, (uint8_t*)blk);

    dir_entry *e;
    for(e = blk; e->first_blk > 0 && e < blk + 64; e++){}

    for(int i = 0; i < 55; i++){
        if(i < filepath.size())
          e->file_name[i] = filepath[i];
        else
          e->file_name[i] = 0;
    }
    e->file_name[55] = 0;

    e->size = (uint32_t)(accum.length() + 1);
    e->first_blk = first_block;
    e->type = TYPE_FILE;
    e->access_rights = READ | WRITE | EXECUTE;

    disk.write(ROOT_BLOCK, (uint8_t*)blk);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    std::cout << "File: \"" <<  e->file_name << "\"\n" << "Size: " << e->size << "\n";
    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int
FS::cat(std::string filepath)
{
    if(file_exists(filepath) == -1){
        std::cout << "File \"" << filepath << "\" does not exist.\n";
        return 1;
    }

    dir_entry blk[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, (uint8_t*)blk);

    int i = file_exists(filepath);

    if(i >= BLOCK_SIZE / sizeof(dir_entry)) // File not found
        return 1;

    dir_entry e = blk[i];

    char *cblk = (char*)blk;
    int block = e.first_blk;
    while(block != FAT_EOF){

        disk.read(block, (uint8_t*)cblk);
        std::string s = std::string(cblk);
        block = fat[block];
        std::cout << s;
    }
    std::cout << "\n";

    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int
FS::ls()
{

    int block_to_write = 6;
    std::cout << "Writing contents in block " << block_to_write << "...\n";
    uint8_t blk6[BLOCK_SIZE];
    disk.read(block_to_write, blk6);
    std::cout << blk6 << "\n\n\n";

    // TEMPORARY
    for(int i = 0; i < 16; i++){
        std::string s = std::to_string(i);

        s.append(4 - s.length(), ' ');

        std::cout << s.c_str();

    }
    std::cout << "\n";
    for(int i = 0; i < 16; i++){
        std::string s = std::to_string(fat[i]);

        s.append(4 - s.length(), ' ');

        std::cout << s.c_str();

    }
    std::cout << "\n";
    //

    dir_entry blk[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, (uint8_t*)blk);

    std::string str = "    Size    File name\n";
    std::cout << str;

    for(int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){
        if(blk[i].first_blk != 0){

            str = std::to_string(i);
            str.append(4 - str.length(), ' ');

            str.append(std::to_string(blk[i].size));
            str.append(12 - str.length(), ' ');

            str.append(blk[i].file_name);
            std::cout << str << "\n";
        }
    }

    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int
FS::cp(std::string sourcepath, std::string destpath)
{
    if(file_exists(destpath) != -1){
      std::cout << "File \"" << destpath << "\" already exists.\n";
      return 1;
    }
    int source_file_id = file_exists(sourcepath);
    if(source_file_id == -1){
      std::cout << "File \"" << sourcepath << "\" does not exist.\n";
      return 1;
    }

    std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";

    // Find file in root
    dir_entry blk[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, (uint8_t*)blk);

    // Make copy of dir_entry
    dir_entry file_entry = blk[source_file_id];

    int free_entry_id = find_empty_dir_entry_id(blk);

    if(free_entry_id == -1){
        std::cout << "No free space in directory to copy file." << "\n";
        return 1;
    }

    dir_entry *copy_entry = blk + free_entry_id;

    for(int i = 0; i < 56; i++)
      copy_entry->file_name[i] = 0;
    strcpy(copy_entry->file_name, destpath.c_str());
    copy_entry->size = file_entry.size;
    copy_entry->type = file_entry.type;
    copy_entry->access_rights = file_entry.access_rights;

    // For each block, copy the data to another block

    uint8_t blk_buf[BLOCK_SIZE];
    int blk_src = file_entry.first_blk;
    int blk_dest = -1;

    // TODO: Perhaps count the number of blocks required and make sure that there's enough space on the disk before we start copying the file

    int blk_empty;
    while(blk_src != FAT_EOF) { // While we have not reached EOF
        // Find empty block
        blk_empty = find_empty_block_id();

        fat[blk_empty] = FAT_EOF;
        if(blk_dest == -1)
            copy_entry->first_blk = blk_empty;
        else 
            fat[blk_dest] = blk_empty;
        blk_dest = blk_empty;

        if(blk_dest <= 1) { // This should never happen but who know
            std::cout << "Unaccounted-for error: blk_dest <= 1 (" << blk_dest << ")\n";
            return 1;
        }

        // Copy the file contents by reading a block into our buffer and then writing our buffer to another block
        disk.read(blk_src, blk_buf);
        disk.write(blk_dest, blk_buf);

        // Next block
        blk_src = fat[blk_src];
    }
                                  
    // Update the copied dir_entry
    disk.write(ROOT_BLOCK, (uint8_t*)blk);
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    std::cout << "Successfully copied " << sourcepath << " into " << destpath << "\n";
   return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    if(destpath.length() >= 55){ // 56 - 1
        std::cout << "Filename too long. The name of a file can be at most be 56 characters long\n";
        return 1;
    }
    if(file_exists(destpath) != -1){
      std::cout << "File \"" << destpath << "\" already exists.\n";
      return 1;
    }
    // Make sure the file exists
    int file_index = file_exists(sourcepath);
    if(file_index == -1){
        std::cout << "File does not exist.\n";
        return 1;
    }

    // Load the root directory
    dir_entry blk[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, (uint8_t*)blk);

    // Copy the new name into the file's dir_entry
    dir_entry *file_entry = blk + file_index;
    std::strcpy(file_entry->file_name, destpath.c_str());

    disk.write(ROOT_BLOCK, (uint8_t*)blk);
    std::cout << "Successfully renamed " << sourcepath << " to " << file_entry->file_name << "\n";
    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    // Make sure the file exists
    int file_index = file_exists(filepath);
    if(file_index == -1){
        std::cout << "File does not exist.\n";
        return 1;
    }
    
    // Load the root directory
    dir_entry blk[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, (uint8_t*)blk);
    
    // Grab a pointer to the file's dir_entry
    dir_entry *file_entry = blk + file_index;

    // Iterate through all the blocks taken up by the file and mark them as free
    int blk_rm = file_entry->first_blk, tmp = 0;
    while(blk_rm != FAT_EOF){
        tmp = blk_rm;
        blk_rm = fat[blk_rm];       // Next block
        fat[tmp] = FAT_FREE;    
    }

    // Set first block to 0 to indicate this dir_entry is not used
    file_entry->first_blk = 0;

    disk.write(ROOT_BLOCK, (uint8_t*)blk);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    std::cout << "Successfully removed file " << filepath << "\n";
    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(std::string filepath1, std::string filepath2)
{
    // Make sure both files exists
    int file_1_id = file_exists(filepath1);
    int file_2_id = file_exists(filepath2);

    if(file_1_id == -1){
        std::cout << "File " << filepath1 << " does not exist\n";
        return 1;
    }
    if(file_2_id == -1){
        std::cout << "File " << filepath2 << " does not exist\n";
        return 1;
    }

    // Load the root directory 
    dir_entry blk[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, (uint8_t*)blk);

    dir_entry *entry_from = blk + file_1_id;
    dir_entry *entry_to = blk + file_2_id;

    int blk_from = entry_from->first_blk;       // The block we're reading from
    int blk_to = entry_to->first_blk;           // The block we're writing to
    while(fat[blk_to] != FAT_EOF){ blk_to = fat[blk_to]; } // Set the writing-to-block block to the last block of the file we're writing to

    // Prepare some variables
    uint8_t buf[BLOCK_SIZE * 2];                                             // Buffer for our files
    int bytes_to_append = entry_from->size + entry_to->size % BLOCK_SIZE;    // Keeps track of how much data is left to write
    int buf_end_pos = 0;                                                     // End position in our data

    // Prepare buffer with the data in the last block of the file we're appending to
    disk.read(blk_to, buf);
    buf_end_pos = (entry_to->size % BLOCK_SIZE) - 1; 

    // ... as well as the data in the first block of the file we're appending
    disk.read(blk_from, buf + buf_end_pos);

    std::cout << "Initial blk_to: " << blk_to << " with table entry " << fat[blk_to] << "\n";

    // Increment the end of the buffer position by the size of the first block in f2
    if(bytes_to_append > BLOCK_SIZE){
        buf_end_pos += BLOCK_SIZE;
    } else{
        buf_end_pos += bytes_to_append;
    }

    while(bytes_to_append > 0){             // While there's data to write
        if(buf_end_pos >= BLOCK_SIZE){
            disk.write(blk_to, buf);        // Write the data to the block
            fat[blk_to] = FAT_EOF;          // ... and mark the FAT entry as EOF

            // Shift data in buffer to the start
            for(int i = 0; i < BLOCK_SIZE; i++){
                buf[i] = buf[BLOCK_SIZE + i];
            }
            buf_end_pos -= BLOCK_SIZE;
            bytes_to_append -= BLOCK_SIZE;

            // Read in new data from the next block unless we reached EOF
            blk_from = fat[blk_from];
            if(blk_from != FAT_EOF){
                disk.read(blk_from, buf + buf_end_pos);

                // Increment buf_end_pos with size of block data
                if(fat[blk_from] == FAT_EOF)                             
                    buf_end_pos += (entry_from->size % BLOCK_SIZE);
                else
                    buf_end_pos += BLOCK_SIZE;
                
            }
            // Find an empty block and mark the FAT table to point correctly
            int blk_new = find_empty_block_id();
            fat[blk_to] = blk_new;
            blk_to = blk_new;
        } else {
            disk.write(blk_to, buf);
            buf_end_pos = 0;
            bytes_to_append = 0;
        }
        
    }
    fat[blk_to] = FAT_EOF;  // Finally mark the end of file

    entry_to->size += entry_from->size - 1;

    disk.write(ROOT_BLOCK, (uint8_t*)blk);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    std::cout << "Successfully appended " << entry_from->file_name << " to the end of " << entry_to->file_name << "\n";
    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int
FS::mkdir(std::string dirpath)
{
    std::cout << "FS::mkdir(" << dirpath << ")\n";
    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    std::cout << "FS::pwd()\n";
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int
FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}

int
FS::file_exists(std::string filename)
{
  dir_entry blk[BLOCK_SIZE];
  disk.read(ROOT_BLOCK, (uint8_t*)blk);

  int i;
  for(i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){
      if(blk[i].first_blk > 0 && std::string(blk[i].file_name) == filename){
          break;
      }
  }
  if(i >= BLOCK_SIZE / sizeof(dir_entry))
    i = -1;
  return i;
}


int 
FS::find_empty_dir_entry_id(dir_entry* entries){
    for(int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){
        if(entries[i].first_blk == 0)
            return i;
    }
    return -1;
}

int 
FS::find_empty_block_id(){
    for(int i = 2; i < 2048; i++){
        if(fat[i] == FAT_FREE)
            return i;
    }
    return -1;
}