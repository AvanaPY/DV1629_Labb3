#include <iostream>
#include "fs.h"
#include <string>
#include <cstring>

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
    int16_t *blk = (int16_t*)malloc(BLOCK_SIZE);
    disk.read(FAT_BLOCK, (uint8_t*)blk);

    for(int i = 0; i < BLOCK_SIZE/2; i++)
        fat[i] = blk[i];

    free(blk);
}

FS::~FS()
{

}

// formats the disk, i.e., creates an empty file system
int
FS::format()
{
    std::cout << "FS::format()\n";
    fat[0] = FAT_EOF;
    fat[1] = FAT_EOF;
    for(int i = 2; i < BLOCK_SIZE/2; i++){
        fat[i] = FAT_FREE;
    }

    dir_entry *blk = (dir_entry*)malloc(BLOCK_SIZE);
    disk.read(ROOT_BLOCK, (uint8_t*)blk);

    // Reset all dir_entries in root folder to start on block 0 so they do not show up
    for(int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){
        blk[i].first_blk = 0;
    }

    disk.write(ROOT_BLOCK, (uint8_t*)blk);
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    free(blk);
    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int
FS::create(std::string filepath)
{
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

    // Write data
    char *c_accum = (char*)accum.c_str();
    int bytes_to_write = accum.length() + 1;
    int first_block = -1, block = -1, previous_block = -1;
    while(bytes_to_write > 0){ // While there's data to write
        // Find an empty block
        for(int i = 2; i < 2048; i++){
            if(fat[i] == FAT_FREE){
                block = i;
                break;
            }
        }
        disk.write(block, (uint8_t*)c_accum);

        fat[block] = FAT_EOF;
        bytes_to_write -= BLOCK_SIZE;
        c_accum += BLOCK_SIZE;

        if(previous_block > 0)
            fat[previous_block] = block;

        if(first_block == -1)
            first_block = block;
        previous_block = block;

        std::cout << "Bytes left: " << bytes_to_write << "\n";
    }

    // Update directory data
    dir_entry *blk = (dir_entry*)malloc(BLOCK_SIZE);

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

    std::cout << "File length: " << e->size << "\n";
    std::cout << "From block " << first_block << " to block " << block << "\n";

    disk.write(ROOT_BLOCK, (uint8_t*)blk);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    free(blk);
    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int
FS::cat(std::string filepath)
{
    std::cout << "FS::cat(" << filepath << ")\n";

    dir_entry *blk = (dir_entry*)malloc(BLOCK_SIZE);
    disk.read(ROOT_BLOCK, (uint8_t*)blk);

    int i = file_exists(filepath);

    if(i >= BLOCK_SIZE / sizeof(dir_entry)) // File not found
        return 1;

    dir_entry e = blk[i];

    std::cout << e.file_name << " " << i << "\n";

    char *cblk = (char*)blk;
    int block = e.first_blk;
    while(block != FAT_EOF){

        disk.read(block, (uint8_t*)cblk);
        std::string s = std::string(cblk);
        block = fat[block];
        std::cout << s;
    }

    std::cout << "\n";

    free(blk);
    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int
FS::ls()
{
    std::cout << "FS::ls()\n";

    std::cout << "FAT status: ";
    for(int i = 0; i < 32; i++){
      std::cout << fat[i] << " ";
    }
    std::cout << "\n";

    dir_entry *blk = (dir_entry*)malloc(BLOCK_SIZE);
    disk.read(ROOT_BLOCK, (uint8_t*)blk);

    std::string str = "    Size   File name\n";
    std::cout << str;

    for(int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){
        if(blk[i].first_blk != 0){

            str = std::to_string(i);
            str.append(4 - str.length(), ' ');

            str.append(std::to_string(blk[i].size));
            str.append(5, ' ');

            str.append(blk[i].file_name);
            std::cout << str << "\n";
        }
    }

    free(blk);
    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int
FS::cp(std::string sourcepath, std::string destpath)
{
    if(file_exists(destpath) != -1){
      std::cout << "File \"" << destpath << "\" already exists.\n";
      return 2;
    }
    if(file_exists(sourcepath) == -1){
      std::cout << "File \"" << sourcepath << "\" does not exist.\n";
      return 2;
    }

    std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";

    // Find file in root
    dir_entry *blk = (dir_entry*)malloc(BLOCK_SIZE);
    disk.read(ROOT_BLOCK, (uint8_t*)blk);

    int i;
    for(i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++)
        if(std::string(blk[i].file_name) == sourcepath)
            break;

    if(i >= BLOCK_SIZE / sizeof(dir_entry)) // File not found
        return 1;

    // Make copy of dir_entry
    dir_entry file_entry = blk[i];
    dir_entry *copy_entry;

    for(copy_entry = blk; copy_entry->first_blk > 0 && copy_entry < blk + 64; copy_entry++){}

    for(int i = 0; i < 56; i++)
      copy_entry->file_name[i] = 0;
    strcpy(copy_entry->file_name, destpath.c_str());
    copy_entry->size = file_entry.size;
    copy_entry->type = file_entry.type;
    copy_entry->access_rights = file_entry.access_rights;

    // For each block, copy the data to another block

    uint8_t *blk_buf = (uint8_t*)malloc(BLOCK_SIZE);

    int c_blk = file_entry.first_blk;
    int to_block = -1;
    while(c_blk != FAT_EOF){

      // Find empty block
      for(int i = 2; i < 2048; i++){
          if(fat[i] == FAT_FREE){

              if(to_block == -1){
                copy_entry->first_blk = i;
                fat[i] = FAT_EOF;
              } else {
                fat[to_block] = i;
                fat[i] = FAT_EOF;
              }
              to_block = i;
              break;
          }
      }

      if(to_block <= 0)
        return 1;
      else if(to_block == 1)
        return 2;

      disk.read(c_blk, blk_buf);
      disk.write(to_block, blk_buf);

      // Next block
      c_blk = fat[c_blk];
    }
    // Update the copied dir_entry
    disk.write(ROOT_BLOCK, (uint8_t*)blk);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    free(blk);
    free(blk_buf);

    std::cout << copy_entry->first_blk << "\n";
   return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";
    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(std::string filepath1, std::string filepath2)
{
    std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";
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
  dir_entry *blk = (dir_entry*)malloc(BLOCK_SIZE);
  disk.read(ROOT_BLOCK, (uint8_t*)blk);

  int i;
  for(i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++){
      if(blk[i].first_blk > 0 && std::string(blk[i].file_name) == filename){
          break;
      }
  }
  if(i >= BLOCK_SIZE / sizeof(dir_entry))
    i = -1;

  free(blk);
  return i;
}
