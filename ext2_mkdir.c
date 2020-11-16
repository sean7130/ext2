#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include "ext2.h"
#include "helper.h"

/*
    Make directory in the inode specified by the given inode number.
 */ 
void make_directory(int parent_inode_num, char *new_name, int new_inode_num) {
    // create an inode for the new directory
    make_inode(new_inode_num, 'd');

    // make "." dir_entry into new_inode
    make_dir_entry_in_inode(new_inode_num, ".", new_inode_num, 'd');
    // make ".." dir_entry into new_inode
    make_dir_entry_in_inode(new_inode_num, "..", parent_inode_num, 'd');
    
    // find the block of the parent directory for insertion
    struct ext2_inode *parent_inode = get_inode_pointer(parent_inode_num);
    int i_block_idx = first_available_i_block(parent_inode_num, strlen(new_name));

    // new parent i_block case: no block has been assigned to this i_block
    if (parent_inode->i_block[i_block_idx] == 0) {
        // find a second available block
        int new_parent_block = find_first_available_block();
        parent_inode->i_block[i_block_idx] = new_parent_block;
    }
    
    // make the dir_entry of this new directory in the parent directory
    make_dir_entry_in_inode(parent_inode_num, new_name, new_inode_num, 'd');
    
    // update number of used directories to include the new directory
    gd->bg_used_dirs_count += 1;
}


int main (int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <path>\n", argv[0]);
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

    int parent_num;
    char child_name[strlen(argv[2]) + 1];
    int child_num;
    
    // remove trailing slashes from path
    remove_trailing_slashes(argv[2]);
    // verify that the path starts with '/' indicating absolute path
    if (verify_absolute_path_structure(argv[2]) == 0) {
        return ENOENT;
    }
    // edge case: when the entire path is just the root
    if (strlen(argv[2]) == 1 && argv[2][0] == '/') {
        return EEXIST;
    }
    
    // find the position of the string at which basename starts
    int basename_offset = get_basename_offset(argv[2]);
    if (basename_offset <= 0) {
        return ENOENT;
    }
    
    // construct the basename string
    strncpy(child_name, argv[2] + basename_offset, strlen(argv[2]) - basename_offset);
    child_name[strlen(argv[2]) - basename_offset] = '\0';
    
    // find the inode number of the parent directory
    parent_num = get_parent_inode_num_from_path(argv[2], basename_offset - 1);
    if (parent_num == -1) {
        return ENOENT;
    }

    // basename should not exist in the parent directory
    child_num = find_token_in_dir(parent_num, child_name);
    if (child_num != -1) {
        return EEXIST;
    }
    
    // ensure that there are enough free blocks and inodes to complete the operation
    int blocks_needed = 1 + inode_needs_new_block_for_new_dir_entry(parent_num, strlen(child_name));
    int inodes_needed = 1;
    if (gd->bg_free_inodes_count < inodes_needed || gd->bg_free_blocks_count < blocks_needed) {
        return ENOMEM;
    }

    // allocate a free inode for the new directory
    int free_inode = find_first_available_inode();

    // perform mkdir
    make_directory(parent_num, child_name, free_inode);
    return 0;    
}
