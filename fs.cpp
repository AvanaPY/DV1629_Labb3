#include <iostream>
#include "fs.h"
#include <string>
#include <cstring>

int blk_curr_dir = ROOT_BLOCK;

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
    // Reset FAT
    fat[0] = FAT_EOF;
    fat[1] = FAT_EOF;
    for(int i = 2; i < BLOCK_SIZE/2; i++){
        fat[i] = FAT_FREE;
    }

    // Reset all the data in the root block to completely empty
    dir_entry blk[BLOCK_SIZE];
    blk_curr_dir = ROOT_BLOCK;
    for(int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){

        blk[i].size = 0;
        blk[i].first_blk = 0;
        blk[i].type = TYPE_FILE;
        blk[i].access_rights = READ | WRITE | EXECUTE;
    }

    // Write data to disk
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
    if(file_exists(current_directory_block(), filepath) != -1){
      std::cout << "File \"" << filepath << "\" already exists.\n";
      return 1;
    }

    // GATHER USER INPUT

    std::string accum;
    std::string line;
    // A loop that accumulates user input text into one single line of text.
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
    int first_block = -1,       // Keep track of which block the file starts on
        block = -1,             // Which block we're writing to next
        previous_block = -1;    // Which block we wrote to last iteration

    while(bytes_to_write > 0){ // While there's data to write

        // Find an empty block to write data to
        block = find_empty_block_id();
        disk.write(block, (uint8_t*)c_accum);

        // Mark the fat table and mark the now-full block as EOF
        fat[block] = FAT_EOF;

        bytes_to_write -= BLOCK_SIZE;
        c_accum += BLOCK_SIZE;

        // If this is not our first loop
        if(previous_block > 0)
            fat[previous_block] = block;

        // If this is our first loop, set first_block equal to block
        if(first_block == -1)
            first_block = block;
        previous_block = block;
    }

    // UPDATE DIRECTORY DATA

    // Load current directory entries
    dir_entry blk[BLOCK_SIZE];
    disk.read(current_directory_block(), (uint8_t*)blk);

    // Find an empty entry to populate
    int empty_entry_id = find_empty_dir_entry_id(blk);
    dir_entry *empty_entry = blk + empty_entry_id;

    strcpy(empty_entry->file_name, filepath.c_str());
    empty_entry->size           = (uint32_t)(accum.length() + 1);
    empty_entry->first_blk      = first_block;
    empty_entry->type           = TYPE_FILE;
    empty_entry->access_rights  = READ | WRITE | EXECUTE;

    disk.write(current_directory_block(), (uint8_t*)blk);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    std::cout << "File: \"" <<  empty_entry->file_name << "\"\n" << "Size: " << empty_entry->size << "\n";
    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int
FS::cat(std::string filepath)
{
    std::string filename;                           
    get_file_name_from_path(filepath, &filename);   // The file name
    chop_file_name(&filepath);                      // The path to the file excluding the file's name

    std::cout << "New file path post-chop: " << filepath << "\n";

    // Find the directory block id where the file resides
    int file_block = find_final_block(current_directory_block(), filepath);

    // If we cannot calculate which block the filepath leads to, something's wrong
    if(file_block == -1){
        std::cout << "File does not exist.\n";
        return -1;
    }

    // Make sure the file exists
    int file_idx = file_exists(file_block, filename);
    if(file_idx == -1){
        std::cout << "File \"" << filename << "\" does not exist.\n";
        return 1;
    }

    // Load the directory block
    dir_entry blk[BLOCK_SIZE];
    disk.read(file_block, (uint8_t*)blk);
    dir_entry file_entry = blk[file_idx];

    // Make sure the file is not a directory
    if(file_entry.type == TYPE_DIR){
        std::cout << "Cannot cat a directory\n";
        return 1;
    }

    // Cat out the file contents
    char *cblk = (char*)blk;
    int block = file_entry.first_blk;
    while(block != FAT_EOF){

        disk.read(block, (uint8_t*)cblk);
        std::string s = std::string(cblk);
        block = fat[block];
        std::cout << s;
    }
    std::cout << "\n"; // New line for good luck

    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int
FS::ls()
{
    // Load the current directory
    dir_entry blk[BLOCK_SIZE];
    disk.read(current_directory_block(), (uint8_t*)blk);

    std::string str;                            // String object of what to print out
    std::cout << "    Type    Size    accessrights    Name\n";  // Layout
    for(int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){
        if(!file_is_visible(blk + i))           // Ignore any files that are not used
            continue;

        if(blk[i].type == TYPE_DIR){                // If dir... 

            str = std::to_string(i);

            str.append(4 - str.length(), ' ');
            str.append("Dir");                      // type is dir

            str.append(12 - str.length(), ' '); 
            str.append("-");                        // size is -

            str.append(20 - str.length(), ' ');
            str.append((blk[i].access_rights & READ)    ? "r" : "-"); 
            str.append((blk[i].access_rights & WRITE)   ? "w" : "-"); 
            str.append((blk[i].access_rights & EXECUTE) ? "x" : "-"); 

            str.append(36 - str.length(), ' ');
            str.append(blk[i].file_name);

            std::cout << str << "\n";
        } else if(blk[i].type == TYPE_FILE) {       // If file...

            str = std::to_string(i);

            str.append(4 - str.length(), ' ');
            str.append("File");                     // type is File

            str.append(12 - str.length(), ' ');
            str.append(std::to_string(blk[i].size));// size is the size of the file

            str.append(20 - str.length(), ' ');
            str.append((blk[i].access_rights & READ)    ? "r" : "-"); 
            str.append((blk[i].access_rights & WRITE)   ? "w" : "-"); 
            str.append((blk[i].access_rights & EXECUTE) ? "x" : "-"); 
            
            str.append(36 - str.length(), ' ');
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
    // Make sure the source file exists
    int source_file_id = file_exists(current_directory_block(), sourcepath);
    if(source_file_id == -1){
      std::cout << "File \"" << sourcepath << "\" does not exist.\n";
      return 1;
    }
    
    // Read current directory
    dir_entry blk[BLOCK_SIZE];
    disk.read(current_directory_block(), (uint8_t*)blk);

    // Make sure destination file does not exist
    int dest_file_id = file_exists(current_directory_block(), destpath);

    std::string copied_file_name;   // Final name of the new copied file
    int dest_blk_id = -1;           // Block number of the directory we're copying to
   
    // The following if-else case determines the file name of the new file
    // as well as which directory to copy it to
    if (destpath.find("/") != -1){                  
        // If the destpath includes any "/" then we're copying a file to another directory                           

        // Check if the path exists is valid
        dest_blk_id = find_final_block(current_directory_block(), destpath);       
        if(dest_blk_id == -1){
            std::cout << "Invalid path for destination file\n";
            return 1;
        }

        if(dest_blk_id == current_directory_block()){                              
            std::cout << "Destination sub-directory is the same directory as the current one.\n";
            return 1;
        }
        copied_file_name = sourcepath; //... so we put the name of the new file to the same name as source file
    } else {                                                                    
        // If the destpath does not include any "/" then we know we're just copying to the current directory

        if(dest_file_id != -1){
            dir_entry* dest_file_entry = blk + dest_file_id;

            // If our destination file exists and is a file, abort
            if(dest_file_entry->type == TYPE_FILE){
                std::cout << "File \"" << destpath << "\" already exists.\n";
                return 1;
            }

            // Else destination file is a directory 
            // Copy the file to the new directory with name sourcepath
            get_file_name_from_path(sourcepath, &copied_file_name);
            dest_blk_id = dest_file_entry->first_blk;

        } else {

            // If our destination file does not exist
            //  - Copy the file to current directory with name destpath
            get_file_name_from_path(destpath, &copied_file_name);
            dest_blk_id = current_directory_block();
        }

    }
    
    if(file_exists(dest_blk_id, copied_file_name) != -1){
        std::cout << "File with name " << copied_file_name << " already exists in destination sub-directory, aborting\n";
        return 1;
    }

    std::cout << "New file name: " << copied_file_name << "\n";

    // Load destination directory
    dir_entry dest_blk[BLOCK_SIZE];
    disk.read(dest_blk_id, (uint8_t*)dest_blk);

    int free_entry_id = find_empty_dir_entry_id(dest_blk);
    if(free_entry_id == -1){
        std::cout << "No free space in directory to copy file." << "\n";
        return 1;
    }

    // Update the free entry in destination directory with the new information
    dir_entry *dest_entry = dest_blk + free_entry_id;
    strcpy(dest_entry->file_name, copied_file_name.c_str());
    dest_entry->size = blk[source_file_id].size;
    dest_entry->type = blk[source_file_id].type;
    dest_entry->access_rights = blk[source_file_id].access_rights;

    uint8_t blk_buf[BLOCK_SIZE];                 // A buffer for file contents
    int blk_src = blk[source_file_id].first_blk; // The block we're reading from
    int blk_dest = -1;                           // The block we're reading to

    // This while loops copies the entire contents of a file to new blocks
    // as well as updates the FAT with accurate information 
    while(blk_src != FAT_EOF) { // While we have not reached EOF
        // Find empty block
        int blk_empty = find_empty_block_id();

        fat[blk_empty] = FAT_EOF;
        if(blk_dest == -1)
            dest_entry->first_blk = blk_empty;
        else 
            fat[blk_dest] = blk_empty;
        blk_dest = blk_empty;

        if(blk_dest <= 1) { // This should never happen but who knows
            std::cout << "Unaccounted-for error: blk_dest <= 1 (" << blk_dest << "). This may indicate that there's no more space on the disk.\n";
            return 1;
        }

        // Copy the file contents by reading a block into our buffer and then writing our buffer to another block
        disk.read(blk_src, blk_buf);
        disk.write(blk_dest, blk_buf);

        // Next block
        blk_src = fat[blk_src];
    }
                                  
    // WRITE TO DISK
    disk.write(dest_blk_id, (uint8_t*)dest_blk);
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    std::cout << "Successfully copied " << sourcepath << " into " << destpath << "\n";
   return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    // Make sure the source file exists
    int file_index = file_exists(current_directory_block(), sourcepath);
    if(file_index == -1){
        std::cout << "File \"" << sourcepath << "\" does not exist.\n";
        return 1;
    }

    // Load the current directory
    dir_entry blk[BLOCK_SIZE];
    disk.read(current_directory_block(), (uint8_t*)blk);

    // Check if the source file is a directory, we don't want to move directories around
    dir_entry* source_file = blk + file_index;
    if(source_file->type == TYPE_DIR){
        std::cout << "Cannot mv file of type directory\n";
        return 1;
    }

    int dest_idx = file_exists(current_directory_block(), destpath);

    // If there is not a "/" in the name and destpath does not exist, then we are renaming sourcefile
    if(destpath.find('/') == -1 && dest_idx == -1) { 

        // If a file with name destpath exists, abort
        if(dest_idx != -1){
            std::cout << "File \"" << destpath.c_str() << "\" already exists.\n";
            return 1;
        }

        // Check if destination file name is not too long
        if(destpath.length() >= 55){ // 56 - 1
            std::cout << "Filename too long. The name of a file can be at most be 56 characters long\n";
            return 1;
        }
        dir_entry *file_entry = blk + file_index;
        // Copy the new name into the file's dir_entry
        std::strcpy(file_entry->file_name, destpath.c_str());

        disk.write(current_directory_block(), (uint8_t*)blk);
        std::cout << "Successfully renamed " << sourcepath << " to " << file_entry->file_name << "\n";
    }
    else { // Else we're moving the file to a different directory 

        // Check if it's a valid path
        int new_blk_id = find_final_block(current_directory_block(), destpath);
        if(new_blk_id == -1){
            std::cout << "Invalid path for destination file\n";
            return 1;
        }

        // Load the block of the destination directory
        dir_entry new_blk[BLOCK_SIZE];
        disk.read(new_blk_id, (uint8_t*)new_blk);

        // If the destination sub-directory block is the same as current directory, we don't have to do anything
        if(new_blk_id == current_directory_block()){
            std::cout << "Destination sub-directory is the same directory as the current one.\n";
            return 0;
        }

        // If a file with the same name as source already exists in the destination sub-directory, abort
        std::cout << "Source path: " << sourcepath << "\n";
        if(file_exists(new_blk_id, sourcepath) != -1){
            std::cout << "File with name " << sourcepath << " already exists in destination sub-directory, aborting\n";
            return 1;
        }
        
        // Find an empty dir_entry in destination sub-directory
        int empty_dir_entry = find_empty_dir_entry_id(new_blk);
        if(empty_dir_entry == -1){
            std::cout << "No free space for file in destination sub-directory\n";
            return 1;
        }

        // Get pointers to the source and destination file entries
        dir_entry *source_entry = blk + file_index;
        dir_entry *dest_entry = new_blk + empty_dir_entry;

        // Update the desination entry with the source entry data
        strcpy(dest_entry->file_name,  source_entry->file_name);
        dest_entry->size             = source_entry->size;
        dest_entry->first_blk        = source_entry->first_blk;
        dest_entry->type             = source_entry->type;
        dest_entry->access_rights    = source_entry->access_rights;

        // Set source entry as empty
        source_entry->size = 0;
        source_entry->first_blk = 0;

        // Write new data to disk
        disk.write(current_directory_block(), (uint8_t*)blk);
        disk.write(new_blk_id, (uint8_t*)new_blk);
        std::cout << "Successfully moved " << sourcepath << " to " << destpath << "\n";
    } 
    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    // Make sure the file exists
    int file_index = file_exists(current_directory_block(), filepath);
    if(file_index == -1){
        std::cout << "File does not exist.\n";
        return 1;
    }
    
    // Load the root directory
    dir_entry blk[BLOCK_SIZE];
    disk.read(current_directory_block(), (uint8_t*)blk);
    
    // Grab a pointer to the file's dir_entry
    dir_entry *file_entry = blk + file_index;

    if(file_entry->type == TYPE_FILE){
        // Iterate through all the blocks taken up by the file and mark them as free
        int blk_rm = file_entry->first_blk, tmp = 0;
        while(blk_rm != FAT_EOF){
            tmp = blk_rm;
            blk_rm = fat[blk_rm];       // Next block
            fat[tmp] = FAT_FREE;    
        }

        // Set first_blk and size to 0 to indicate this dir_entry is not used
        file_entry->first_blk = 0;
        file_entry->size = 0;


        std::cout << "Successfully removed file " << filepath << "\n";
    } 
    else { // If we're working with a directory

        // Check if it is empty
        dir_entry directory_entries[BLOCK_SIZE];
        disk.read(file_entry->first_blk, (uint8_t*)directory_entries);
        for(int i = 1; i < BLOCK_SIZE / sizeof(dir_entry); i++){ // start on 1 since file 0 is always ".." unless 
                                                                 // we're root and we never want to remove root anyways
            if(directory_entries[i].size != 0 || directory_entries[i].first_blk != 0){
                std::cout << "Cannot remove directory: Directory " << file_entry->file_name << " is not empty.\n";
                return 1;
            }
        }

        // Mark the block the directory leads to as free
        fat[file_entry->first_blk] = FAT_FREE;

        // Set first_blk and size to 0 to indicate this dir_entry is not used
        file_entry->first_blk = 0;
        file_entry->size = 0;

        std::cout << "Successfully removed directory " << filepath << "\n";
    }
    disk.write(current_directory_block(), (uint8_t*)blk);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(std::string filepath1, std::string filepath2)
{
    // Make sure both files exists
    int file_1_id = file_exists(current_directory_block(), filepath1);
    if(file_1_id == -1){
        std::cout << "File " << filepath1 << " does not exist\n";
        return 1;
    }
    int file_2_id = file_exists(current_directory_block(), filepath2);
    if(file_2_id == -1){
        std::cout << "File " << filepath2 << " does not exist\n";
        return 1;
    }

    // Load the root directory 
    dir_entry blk[BLOCK_SIZE];
    disk.read(current_directory_block(), (uint8_t*)blk);

    dir_entry *entry_from = blk + file_1_id;
    dir_entry *entry_to = blk + file_2_id;

    int blk_from = entry_from->first_blk;       // The block we're reading from
    int blk_to = entry_to->first_blk;           // The block we're writing to
    while(fat[blk_to] != FAT_EOF){ blk_to = fat[blk_to]; } // Set the writing-to-block block to the last block of the file we're writing to

    // Prepare some variables
    uint8_t buf[BLOCK_SIZE * 2];                                                    // Buffer for our files
    int bytes_to_append = entry_from->size + (entry_to->size % BLOCK_SIZE);         // Keeps track of how much data is left to write
    int buf_end_pos = 0;                                                            // End position in our data

    // Prepare buffer with the data in the last block of the file we're appending to
    disk.read(blk_to, buf);
    buf_end_pos = (entry_to->size % BLOCK_SIZE);
    buf[buf_end_pos-1] = '\n';

    // ... as well as the data in the first block of the file we're appending
    disk.read(blk_from, buf + buf_end_pos);

    // Increment the end of the buffer position by the size of the first block in f2
    if(bytes_to_append > BLOCK_SIZE){
        buf_end_pos += BLOCK_SIZE;
    } else{
        buf_end_pos += (entry_to->size % BLOCK_SIZE);
    }

    while(bytes_to_append > 0){             // While there's data to write
        std::cout << "Start of loop..." << bytes_to_append << " | " << buf_end_pos << "\n";
        if(buf_end_pos >= BLOCK_SIZE){
            disk.write(blk_to, buf);        // Write the data to the block
            fat[blk_to] = FAT_EOF;          // ... and mark the FAT entry as EOF

            // Shift data in buffer to the start
            // It would be more efficient to not do this but this works, might update in the future
            // TODO: Improve 
            for(int i = 0; i < BLOCK_SIZE; i++)
                buf[i] = buf[BLOCK_SIZE + i];
            buf_end_pos -= BLOCK_SIZE;
            bytes_to_append -= BLOCK_SIZE;

            blk_from = fat[blk_from];
            if(blk_from != FAT_EOF){  // Read in new data from the next block unless we reached EOF
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
            bytes_to_append = 0;
        }
    }
    fat[blk_to] = FAT_EOF;  // Finally mark the end of file

    entry_to->size += entry_from->size; 

    disk.write(current_directory_block(), (uint8_t*)blk);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    std::cout << "Successfully appended " << entry_from->file_name << " to the end of " << entry_to->file_name << "\n";
    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int
FS::mkdir(std::string dirpath)
{
    if(file_exists(current_directory_block(), dirpath) != -1){
        std::cout << "wtf\n";
        std::cout << "A directory with name " << dirpath << " already exists.\n";
        return 1;
    }
    // Load the current directory directory
    dir_entry blk[BLOCK_SIZE];
    disk.read(current_directory_block(), (uint8_t*)blk);

    int entry_id = find_empty_dir_entry_id(blk);
    if(entry_id == -1){
        std::cout << "Could not create directory: Not enough space in directory.\n";
        return 1;
    }

    int free_block = find_empty_block_id();
    if(free_block == -1){
        std::cout << "Could not create directory: No free blocks available.\n";
        return 1;
    }

    // Create directory entry
    dir_entry *entry = blk + entry_id;

    strcpy(entry->file_name, dirpath.c_str());
    entry->size = 1;
    entry->first_blk = free_block;
    entry->type = TYPE_DIR;
    entry->access_rights = WRITE | READ | EXECUTE;

    // Update FAT 
    fat[free_block] = FAT_EOF;

    // Write our block
    disk.write(current_directory_block(), (uint8_t*)blk);
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    std::cout << "Successfully created directory " << entry->file_name << "\n";

    // Update the new directory's own block free_block
    // with our file ".." that points to the current block

    dir_entry dir_blk[BLOCK_SIZE];
    disk.read(free_block, (uint8_t*)dir_blk);

    for(int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){
        dir_blk[i].first_blk = 0;
        dir_blk[i].size = 0;
    }

    // Find a free entry in the current directory
    int free_entry = find_empty_dir_entry_id(dir_blk);
    if(free_entry == -1){
        std::cout << "Could not create sub-folder \"..\": Not enough space on block.\n"; // This should never happen
        return 1;
    }

    // Create a ".." in the new directory that points to the current directory
    dir_entry *parent_entry = dir_blk + free_entry;
    strcpy(parent_entry->file_name, "..");
    parent_entry->size = 1;
    parent_entry->first_blk = blk_curr_dir;
    parent_entry->type = TYPE_DIR;
    parent_entry->access_rights = READ | WRITE | EXECUTE;

    disk.write(free_block, (uint8_t*)dir_blk);
    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    dir_entry blk[BLOCK_SIZE];
    disk.read(current_directory_block(), (uint8_t*)blk);

    int final_block = find_final_block(current_directory_block(), dirpath);
    if(final_block == -1){
        std::cout << "Path " << dirpath << " is not a valid path.\n";
        return 1;
    }

    blk_curr_dir = final_block;
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    std::string path;
    int blk_id = current_directory_block();

    if(blk_id == ROOT_BLOCK){
        path = "/";
    } else {
        dir_entry blk[BLOCK_SIZE];
        do {
            disk.read(blk_id, (uint8_t*)blk);
            // First entry in a non-root directory should always be the .. directory
            dir_entry entry = blk[0];

            // Read the parent directory block
            disk.read(entry.first_blk, (uint8_t*)blk);

            // Iterate over all dir entries in parent directory 
            // and find which dir_entry points to the current block
            // and insert the name of that dir_entry
            for(int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){
                if(!file_is_visible(blk + i))
                    continue;
                
                if(blk[i].first_blk == blk_id)
                    path.insert(0, blk[i].file_name);
            }

            // Insert a "/" and update the current block to the parent's block
            path.insert(0, "/");
            blk_id = entry.first_blk;
        } while(blk_id != ROOT_BLOCK);
    }

    std::cout << path.c_str() << "\n";
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int
FS::chmod(std::string accessrights, std::string filepath)
{
    std::string file_name;
    get_file_name_from_path(filepath, &file_name);

    chop_file_name(&filepath);
    int file_directory_block = find_final_block(current_directory_block(), filepath);

    // Make sure target destination file exists
    int file_index = file_exists(file_directory_block, file_name);
    if(file_index == -1){
      std::cout << "File \"" << file_name << "\" does not exists.\n";
      return 1;
    }

    // Make sure access rights are valid
    int new_access_rights = std::stoid(accessrights);
    if(new_access_rights < 0 || new_access_rights > 7){
        std::cout << "Access rights are not valid\n";
        return 1;
    }

    // Load the root directory
    dir_entry blk[BLOCK_SIZE];
    disk.read(file_directory_block, (uint8_t*)blk);

    // Copy the new name into the file's dir_entry
    dir_entry *file_entry = blk + file_index;
    file_entry->access_rights = new_access_rights;

    disk.write(file_directory_block, (uint8_t*)blk);
    std::cout << "Changed permissions of " << file_name << " to " << std::to_string(file_entry->access_rights) << "\n";
    return 0;
}

int
FS::file_exists(uint16_t directory_block, std::string filename)
{       
    if(filename.empty())
        return -1;

    // If it is an absolute path
    if(filename.at(0) == '/'){
        filename = filename.erase(0, 1);
        return file_exists(ROOT_BLOCK, filename);
    }

    // We put this here at some point but don't really remember what it does
    // but I have learned the hard way to not delete code, so it is just commented out
    // Sitting here, waiting for the inevitable time where it is removed, or breaks our code.

    // int slash = filename.find("/");
    // if (slash != -1){
    //     filename = filename.substr(0, slash); // If there is a slash indicating a full path, 
    //                                           // only pick the first one
    //                                           // i.e dir/dir2 -> dir
    // }

    dir_entry blk[BLOCK_SIZE];
    disk.read(directory_block, (uint8_t*)blk);

    for(int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){
        if(file_is_visible(blk + i) && std::string(blk[i].file_name) == filename){
            return i;
        }
    }
    return -1;
}

int 
FS::find_empty_dir_entry_id(dir_entry* entries)
{
    for(int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){
        if(!file_is_visible(entries + i))
            return i;
    }
    return -1;
}

int 
FS::find_empty_block_id()
{
    for(int i = 2; i < 2048; i++){
        if(fat[i] == FAT_FREE)
            return i;
    }
    return -1;
}

int
FS::current_directory_block()
{
    return blk_curr_dir;
}

bool
FS::file_is_visible(dir_entry* file)
{
    return  (file->type == TYPE_FILE && file->size >  0) ||
            (file->type == TYPE_DIR  && file->size != 0);
}

// find_final_block returns the final block in a path that only contains directories
int
FS::find_final_block(int c_blk, std::string path)
{
    // If the path is empty, just return the current block
    if(path.empty())
        return c_blk;
    
    // If path is just root, return root, easy.
    if(path == "/"){
        return ROOT_BLOCK;
    }

    // If path starts at root, set current block to root and pretend that's what the user started with
    if(path.at(0) == '/'){
        c_blk = ROOT_BLOCK;
        path = path.erase(0, 1);
    }

    std::string buf;        // Buffer for the current directory name to look for
                            // e.g given a path "abc/dir/123", this would first have value abc, then dir, then 123
    while(!path.empty()){

        int slash_id = path.find("/");

        // If there's a slash, buf will be everything up until slash, excluding the slash
        if(slash_id != -1){
            buf = path.substr(0, slash_id);
            path.erase(0, slash_id + 1);
        } 
        // Else buf is just what's left
        else {
            buf = path;
            path.erase(0, path.length());
        }

        // Read the current block to look at
        dir_entry curr_dir_entries[BLOCK_SIZE];
        disk.read(c_blk, (uint8_t*)curr_dir_entries);

        // Look at each entry and find the directory with the same name as in buf and update current block
        bool found_next_file = false;
        for(int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){
            if(buf == curr_dir_entries[i].file_name){
                if(curr_dir_entries[i].type == TYPE_DIR)
                    c_blk = curr_dir_entries[i].first_blk;
                found_next_file = true;
                break;
            }
        }
        if(!found_next_file){
            return -1;
        }
    }
    return c_blk;
}

int
FS::chop_file_name(std::string* filepath)
{
    int last_slash_id = filepath->rfind('/');

    if(last_slash_id != -1) {
        filepath->erase(last_slash_id, filepath->length()); // Remove everything from slash and onwards
        
        // If the filepath is empty after chop, it means our path was an absolute path to root, e.g /dir, /file, etc
        if(filepath->empty())
            filepath->insert(0, "/");
    } else {
        filepath->erase(0, filepath->length());
    }

    return 0;
}

int
FS::get_file_name_from_path(std::string filepath, std::string *filename)
{
    int last_slash_id = filepath.rfind('/');

    if(last_slash_id != -1)
        filepath.erase(0, last_slash_id+1);
    
    filename->append(filepath);
    return 0;
}