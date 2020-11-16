#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <libgen.h>
#include <time.h>
#include <errno.h>
#include "ext2.h"
#include "helper.h"

// This is the function dedicated for dir entires that are for sure to be restored
void restore_dir_entry(	struct ext2_dir_entry *entry_to_be_restored, 
						struct ext2_inode *parent_inode,
						int i_block_index
						) {

	// Find the inode to be restored
	int inode_num_to_be_restored = entry_to_be_restored->inode;
	struct ext2_inode *restored_inode = get_inode_pointer(inode_num_to_be_restored);

	// if inode is used by other sources, then restore is not possible
	if (get_inode_bit_value(inode_num_to_be_restored) == 1) {
		exit(EEXIST);
	}
	// Continue re-enab-ing
	update_inode_bitmap(inode_num_to_be_restored, 1);

	// Read from the re-enabled inode, restore the nesseary values
	restored_inode->i_links_count += 1;
	restored_inode->i_dtime = 0;
	int indirect_iterations = 0;

	// re-enable the blocks of the inode
	int iteration_counts = (restored_inode->i_blocks)/2;
	if (iteration_counts >= 13){
		indirect_iterations = iteration_counts - 12;
		iteration_counts = 12;
	}

	int i;
	for (i=0; i<iteration_counts; i++){			
		update_block_bitmap(restored_inode->i_block[i], 1);
	}

	if (indirect_iterations) {
		int j;

		// Index 12 is the pointer to a indirect block
		int *indirect_blocks = (int *) get_dir_entry_pointer(
		restored_inode->i_block[12], 0);

		for (j=0; j<indirect_iterations; j++){
			// set all blocks found in indirect blocks into 0
			if (indirect_blocks[j]) {
			update_block_bitmap(indirect_blocks[j], 1);
			}
		}
		update_block_bitmap(restored_inode->i_block[12], 1);
	}
}

// The function should just do the reverse of remove
// Since the parent inode is known; serach through all the blocks in parent
// inode to see if there exist the file to be restored. 
void restore_vicitim_at_inode(int parent_inode_num, char* restore_name){
	// After determining a filesystem that should be restored; 
	// following will be done: 
	// - Re-enable vicitim's inode if not enabled
	// - Re-enable block
	// - incrememnt hardlink count for the inode-to-be-restored
	// - Restore rec-len to its proper amount

	struct ext2_inode *parent_inode = get_inode_pointer(parent_inode_num);

	int i;
	for (i=0; i<(parent_inode->i_blocks)/2; i++){
		struct ext2_dir_entry *current_entry = get_dir_entry_pointer(parent_inode->i_block[i], 0);
		// struct ext2_dir_entry *last_entry = NULL; 

		int offset = 0;
		int expected_rec_len = 0;
		while (offset < EXT2_BLOCK_SIZE) {
			expected_rec_len = compute_rec_len(current_entry->name_len);
			// ===== Logic Here ===== // 
			// If rec_len does not match expected; there should be the deleted entry in here
			if (expected_rec_len < current_entry->rec_len){
				// dir_entries that enters this loop are all canadidates for containing a 
				// "removed" file_entry within it's rec_len

				// Get the ext2_dir_entry that is hidden in current_entry
				struct ext2_dir_entry *hidden_entry = get_dir_entry_pointer(
					parent_inode->i_block[i], offset + expected_rec_len);

				if (strncmp(restore_name, hidden_entry->name, strlen(restore_name)) == 0) {
					// All test case passed, this shall be the file/ext2_dir_entry to be restored
					restore_dir_entry(hidden_entry, parent_inode, i);

					// Following operations restore pre-rm rec_lens
					int hidden_rec_len = current_entry->rec_len - expected_rec_len;
					current_entry->rec_len = expected_rec_len;
					hidden_entry->rec_len = hidden_rec_len;

					return;
				}

			}
			offset += current_entry->rec_len;
			// last_entry = current_entry;
			current_entry = get_dir_entry_pointer(parent_inode->i_block[i], offset);
		}
	}
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

	// remove trailing slashes from path
    remove_trailing_slashes(argv[2]);
	// verify that the path starts with '/' indicating absolute path
    if (verify_absolute_path_structure(argv[2]) == 0) {
        return ENOENT;
    }
    // Error check: absolute path must start from root directory
    if (strlen(argv[2]) >= 1 && argv[2][0] != '/') {
        return EINVAL;
    }
	// edge case: when the entire path is just the root
    if (strlen(argv[2]) == 1 && argv[2][0] == '/') {
		return EEXIST;
    }

    int parent_num;
    char child_name[strlen(argv[2]) + 1];

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
	struct ext2_inode* parent = get_inode_pointer(parent_num);
	// parent must be a directory
	if (find_filetype(parent->i_mode) != 'd') {
		return ENOTDIR;
	}

    // ensure that there are enough free blocks and inodes to complete the operation
    int blocks_needed = 1 + inode_needs_new_block_for_new_dir_entry(parent_num, strlen(child_name));
    int inodes_needed = 1;
    if (gd->bg_free_inodes_count < inodes_needed || gd->bg_free_blocks_count < blocks_needed) {
        return ENOMEM;
    }

    // Enough error checks, run function that does needed operation

    restore_vicitim_at_inode(parent_num, child_name);
}
