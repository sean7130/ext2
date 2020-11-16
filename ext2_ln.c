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

void do_symlink(int src_num, int dest_num, char *ln_filepath, char *ln_filename) {
    int symlink_num = find_first_available_inode();
    struct ext2_inode *symlink = make_inode(symlink_num, 's');
    // allocate block and put it in symlink inode
    int symlink_block_num = find_first_available_block();
    symlink->i_block[0] = symlink_block_num;
    symlink->i_blocks += 2;
    symlink->i_size = strlen(ln_filepath);
    // put data into symlink_block
    char *symlink_block = (char *)(disk + EXT2_BLOCK_SIZE * symlink_block_num);
    strncpy(symlink_block, ln_filepath, strlen(ln_filepath));
    // make the dir_entry in dest_inode
    // this helper updates the symlink inode i_link_count automatically
    make_dir_entry_in_inode(dest_num, ln_filename, symlink_num, 's'); 
}

void do_hardlink(int src_num, int dest_num, char *filepath, char *ln_filename) {
    make_dir_entry_in_inode(dest_num, ln_filename, src_num, 'f');
}

int main (int argc, char **argv) {
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "Usage (-s for symlink): %s <image file name> (-s) <path to source file> <path to dest>\n", argv[0]);
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

    int s_flag = 0;
    if (strlen(argv[2]) == 2 && strncmp(argv[2], "-s", 2) == 0) {
        s_flag = 1;
    }

    // ------------------- handle dest path -----------------------
    int dest_parent_num;
    char dest_child_name[strlen(argv[3 + s_flag]) + 1];
    int dest_child_num;

    // remove trailing slashes from path
    remove_trailing_slashes(argv[3 + s_flag]);
    // verify that the path starts with '/' indicating absolute path
    if (verify_absolute_path_structure(argv[3 + s_flag]) == 0 || verify_absolute_path_structure(argv[2 + s_flag]) == 0) {
        return ENOENT;
    }
    // edge case: when the entire path is just the root
    if ((strlen(argv[3 + s_flag]) == 1 && argv[3 + s_flag][0] == '/') || (strlen(argv[2 + s_flag]) == 1 && argv[2 + s_flag][0] == '/')) {
        return EEXIST;
    }
    // find the position of the string at which basename starts
    int dest_basename_offset = get_basename_offset(argv[3 + s_flag]);
    if (dest_basename_offset <= 0) {
        return ENOENT;
    }
    // construct the basename string
    strncpy(dest_child_name, argv[3 + s_flag] + dest_basename_offset, strlen(argv[3 + s_flag]) - dest_basename_offset);
    dest_child_name[strlen(argv[3 + s_flag]) - dest_basename_offset] = '\0';
    // find the inode number of the parent directory
    dest_parent_num = get_parent_inode_num_from_path(argv[3 + s_flag], dest_basename_offset - 1);
    if (dest_parent_num == -1) {
        return ENOENT;
    }
    dest_child_num = find_token_in_dir(dest_parent_num, dest_child_name);

    // ------------------- handle source path -----------------------
    int src_parent_num;
    char src_child_name[strlen(argv[2 + s_flag]) + 1];
    int src_child_num;

    // remove trailing slashes from path
    remove_trailing_slashes(argv[2 + s_flag]);
    // find the position of the string at which basename starts
    int src_child_offset = get_basename_offset(argv[2 + s_flag]);
    if (src_child_offset <= 0) {
        return ENOENT;
    }
    // construct the basename string
    strncpy(src_child_name, argv[2 + s_flag] + src_child_offset, strlen(argv[2 + s_flag]) - src_child_offset);
    src_child_name[strlen(argv[2 + s_flag]) - src_child_offset] = '\0';
    // find the inode number of the parent directory
    src_parent_num = get_parent_inode_num_from_path(argv[2 + s_flag], src_child_offset - 1);
    if (src_parent_num == -1) {
        return ENOENT;
    }
    src_child_num = find_token_in_dir(src_parent_num, src_child_name);

    // ---------- determine new filename and where to link ------------
    int dest_child_name_len = strlen(dest_child_name);
    int src_child_name_len = strlen(src_child_name);
    // allocate cp_filename to be able to store either child_name
    int max_path_len = dest_child_name_len;
    if (max_path_len < src_child_name_len) {
        max_path_len = src_child_name_len;
    }
    char ln_filename[max_path_len + 1];

    int dest_num;
    if (src_child_num == -1) {
        return ENOENT;
    } 
    if (dest_child_num != -1) {
        struct ext2_inode *dest_child = get_inode_pointer(dest_child_num);
        if (find_filetype(dest_child->i_mode) != 'd') {
            return ENOENT;
        }
        // if dest_child is a directory, update location and filename of linking
        if (find_filetype(dest_child->i_mode) == 'd') {
            // since child is a directory, place the link under the child dir
            dest_num = dest_child_num;
            // filename of link is the same as the source filename
            strncpy(ln_filename, src_child_name, src_child_name_len);
            ln_filename[src_child_name_len] = '\0';
        }
    } else {
        dest_num = dest_parent_num;
        int dest_child_name_len = strlen(dest_child_name);
        // filename of link is the same as the destination filename
        strncpy(ln_filename, dest_child_name, dest_child_name_len);
        ln_filename[dest_child_name_len] = '\0';
    }

    int num_of_inodes;
    int num_of_blocks;
    // verify there is enough space to do this operation
    if (s_flag == 0) {
        num_of_inodes = 1;
        num_of_blocks = 1 + inode_needs_new_block_for_new_dir_entry(dest_num, strlen(ln_filename));
    } else {
        num_of_blocks = inode_needs_new_block_for_new_dir_entry(dest_num, strlen(ln_filename));
        num_of_inodes = 0;
    }
    if (gd->bg_free_blocks_count < num_of_blocks || gd->bg_free_inodes_count < num_of_inodes) {
        return ENOMEM;
    }

    if (s_flag == 0) {
        do_hardlink(src_child_num, dest_num, argv[2 + s_flag], ln_filename);
    } else {
        do_symlink(src_child_num, dest_num, argv[2 + s_flag], ln_filename);
    }


}