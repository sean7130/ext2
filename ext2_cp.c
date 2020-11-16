#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"
#include "helper.h"


int main (int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <image file name> <path to source file> <path to dest>\n", argv[0]);
        exit(1);
    }
    // access disk image
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    gd = (struct ext2_group_desc *)(disk + EXT2_BLOCK_SIZE*2);

    // ------------------- handle dest path -----------------------
    int dest_parent_num;
    char dest_child_name[strlen(argv[3]) + 1];
    int dest_child_num;

    // remove trailing slashes from path
    remove_trailing_slashes(argv[3]);
    // verify that the path starts with '/' indicating absolute path
    if (verify_absolute_path_structure(argv[3]) == 0) {
        return ENOENT;
    }
    // edge case: when the entire path is just the root
    if (strlen(argv[2]) == 1 && argv[3][0] == '/') {
        return EEXIST;
    }
    // find the position of the string at which basename starts
    int basename_offset = get_basename_offset(argv[3]);
    if (basename_offset <= 0) {
        return ENOENT;
    }
    // construct the basename string
    strncpy(dest_child_name, argv[3] + basename_offset, strlen(argv[3]) - basename_offset);
    dest_child_name[strlen(argv[3]) - basename_offset] = '\0';
    // find the inode number of the parent directory
    dest_parent_num = get_parent_inode_num_from_path(argv[3], basename_offset - 1);
    if (dest_parent_num == -1) {
        return ENOENT;
    }

    int dest_child_name_len = strlen(dest_child_name);
    int src_child_offset = get_basename_offset(argv[2]);
    int src_child_name_len = strlen(argv[2]) - src_child_offset;
    // allocate cp_filename to be able to store either child_name
    int max_path_len = dest_child_name_len;
    if (max_path_len < src_child_name_len) {
        max_path_len = src_child_name_len;
    }
    char cp_filename[max_path_len + 1];

    /* 
        cases: 
            1. if dest_child_name exists and is a dir, 
                place copied file into the basename dir (with same source filename).
            2. if dest_child_name exists and is not a dir, return error.   
            3. if dest_child_name does not exist, 
                use the dest_child_name as the name of the file copy. 
    */
    dest_child_num = find_token_in_dir(dest_parent_num, dest_child_name);
    if (dest_child_num != -1) {
        struct ext2_inode *base_inode = get_inode_pointer(dest_child_num);
        if (find_filetype(base_inode->i_mode) == 'd') {
            // dest_child_num becomes the destination directory
            dest_parent_num = dest_child_num;
            // copied file takes src_file_name
            strncpy(cp_filename, argv[2] + src_child_offset, src_child_name_len - src_child_offset);
            cp_filename[src_child_name_len - src_child_offset] = '\0';
        } else {
            return EEXIST;
        }
    } else {
        strncpy(cp_filename, dest_child_name, dest_child_name_len);
        cp_filename[dest_child_name_len] = '\0';
    }

    // ------------------- handle source path -----------------------
    FILE *src_fp = fopen(argv[2], "r");
    if (src_fp == NULL) {
        return ENOENT;
    }
    // jump file to end to find the size of file
    fseek(src_fp, 0, SEEK_END);
    int src_file_size = ftell(src_fp);
    
    // get the dest inode
    struct ext2_inode *dir_inode = get_inode_pointer(dest_parent_num);
    // find the i_block to place the inode of the new file
    int i_block_idx = first_available_i_block(dest_parent_num, strlen(cp_filename));
    int num_of_blocks_for_dir = 0;

    // calculate how many free blocks do we need to find for this file
    int num_of_blocks = (int)(src_file_size / EXT2_BLOCK_SIZE);
    if (src_file_size % EXT2_BLOCK_SIZE > 0) {
        num_of_blocks += 1;
    }
    
    // check if dest directory requires a new block to store dir_entry of the new file
    if (dir_inode->i_block[i_block_idx] == 0) {
        // one more block is needed to make a new block for dir_entry
        num_of_blocks_for_dir += 1;
    }
    // error check: not enough blocks
    if (gd->bg_free_blocks_count < (num_of_blocks + num_of_blocks_for_dir) || gd->bg_free_inodes_count < 1) {
        return ENOMEM;
    }

    // jump back to start of file for reading the data
    fseek(src_fp, 0, SEEK_SET);

    // ------- put data into the blocks and set up file inode ------------
    // create the inode of a new file
    int free_inode_num = find_first_available_inode();
    struct ext2_inode *file_inode = make_inode(free_inode_num, 'f');
    file_inode->i_size = src_file_size;
    int block_count = 0;
    // array representing the single indirect block
    int *sib;
    int sib_idx = 0;
    // put file data into blocks for the file's inode
    while (block_count < num_of_blocks) {
        int data_block_num;
        // direct mapping to data block
        if (block_count < 12) {
            if (file_inode->i_block[block_count] == 0) {
                file_inode->i_block[block_count] = find_first_available_block();
                file_inode->i_blocks += 2;
            }
            data_block_num = file_inode->i_block[block_count];
        } else if (block_count == 12) {
            // single indirection mapping to data block
            if (file_inode->i_block[12] == 0) {
                file_inode->i_block[12] = find_first_available_block();
                file_inode->i_blocks += 2;
            }
        }
        if (block_count >= 12) {
            // get block number from sib
            sib = (int *) (disk + EXT2_BLOCK_SIZE * file_inode->i_block[12]);
            // find the data_block
            sib[sib_idx] = find_first_available_block();
            file_inode->i_blocks += 2;
            data_block_num = sib[sib_idx];
            sib_idx += 1;
        }
        // read the contents of the file into the data block in the disk
        char *data_block = (char *)(disk + (data_block_num * EXT2_BLOCK_SIZE));
        fread(data_block, sizeof(char), EXT2_BLOCK_SIZE, src_fp);
        block_count += 1;
    }

    // ----------------- put file inode into destination directory --------
    // allocate block if no block has been assigned to this i_block
    if (dir_inode->i_block[i_block_idx] == 0) {
        dir_inode->i_block[i_block_idx] = find_first_available_block();
    }
    // make a dir_entry for file_inode and place it in directory
    make_dir_entry_in_inode(dest_parent_num, cp_filename, free_inode_num, 'f');

    return 0;
}