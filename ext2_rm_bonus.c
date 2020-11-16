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

// Function verifies if the inode at inode_index have at least one hard link,
// otherwise the inode will be unset.
void verify_inode(int inode_index){
	struct ext2_inode *victim_inode = get_inode_pointer(inode_index);

	if (victim_inode->i_links_count == 0) {
		int i;
		int j;
		int indirect_iterations = 0;

		// Disable the rest of the blocks
		int iteration_counts = (victim_inode->i_blocks)/2;
		if (iteration_counts >= 13){
			indirect_iterations = iteration_counts - 12;
			iteration_counts = 12;
		}

		for (i=0; i<iteration_counts; i++){			
			update_block_bitmap(victim_inode->i_block[i], 0);
		}

		if (indirect_iterations) {
			// Index 12 is the pointer to a indirect block
			int *indirect_blocks = (int *) get_dir_entry_pointer(
			victim_inode->i_block[12], 0);

			for (j=0; j<indirect_iterations; j++){
				// set all blocks found in indirect blocks into 0
				if (indirect_blocks[j]) {
				update_block_bitmap(indirect_blocks[j], 0);
				}
			}
			update_block_bitmap(victim_inode->i_block[12], 0);
		}

		// Note deletion time
		victim_inode->i_dtime = (unsigned int) time(NULL);
		// Delete the inode by removing it form the field of bitmap
		update_inode_bitmap(inode_index, 0);
	}
}

// Removes the ext2_dir_entry victim_entry with respect to the entry that comes 
// before it, last_entry
// Returns the index of the inode that contains the removed item
int remove_dir_entry(	struct ext2_dir_entry *victim_entry, 
						struct ext2_dir_entry *last_entry,
						struct ext2_inode *parent_inode,
						int i_block_index
						){

	int victim_inode_index = victim_entry->inode;
	struct ext2_inode *victim_inode = get_inode_pointer(victim_inode_index);

	// RMB: decrement i_links_count for victim_inode!

	// Few things to check (in order) : 
	// -	if this file takes up the entire block, 
	// 		then the block should be unset & disabled and information to be updated to
	// 		bitmap.
	// -	If the victim_inode just happens to be the first one in block: set its inode
	// 		to 0 to invalidate
	//		

	if (victim_entry->rec_len == EXT2_BLOCK_SIZE){	
		// Empty this block
		update_block_bitmap(parent_inode->i_block[i_block_index], 0);
		parent_inode->i_block[i_block_index] = 0;
		parent_inode->i_blocks -= 2;

	} else if (last_entry == NULL) {
		// Set inode to 0 to invalidate
		victim_entry->inode = 0;

	} else {
		last_entry->rec_len+= victim_entry->rec_len;
	}

	victim_inode->i_links_count--;
	return victim_inode_index;

}

// Function that removes the file entry with name "victim_name" from parent_inode_num
void remove_victim_at_inode(int parent_inode_num, char* victim_name){

	// Things to do to garentee removal:
	//	-	Traverse though the i_block of the parent block, once vimtim is found:
	//			-	Get victim inode
	//			-	Use victim's inode to find the block that's associated with it
	//			-	Remove block
	//			-	Decrease hard link count in inode as blocks have been decreased
	//			-	Update the rec_len count for the dir before victim

	struct ext2_inode *parent_inode = get_inode_pointer(parent_inode_num);

	int i;
	for (i=0; i<(parent_inode->i_blocks)/2; i++){
		struct ext2_dir_entry *current_entry = get_dir_entry_pointer(parent_inode->i_block[i], 0);
		struct ext2_dir_entry *last_entry = NULL; // This will become userful in later steps

		// Traverse though this entire block
		int offset = 0;
		while (offset < EXT2_BLOCK_SIZE) {

			// Check if this dir_entry happens to be the target: the victim to be removed
			if (strncmp(victim_name, current_entry->name, current_entry->name_len) == 0) {	

				int victim_inode_index = remove_dir_entry(current_entry, last_entry, parent_inode, i);

				// Now that the dir_entry is gone, check if the inode does not have any hard-links
				// left, which if is the case, then remove the inode
				verify_inode(victim_inode_index);

				return;
			}

			// Update last and curr_dir_entry
			offset += current_entry->rec_len;
			last_entry = current_entry;
			current_entry = get_dir_entry_pointer(parent_inode->i_block[i], offset);
		}
	}


}


int r_delete_from_inode(struct ext2_inode *inode) {
	int i;
	int indirect_iterations = 0;
	int total_fixes = 0;

	int iteration_counts = (inode->i_blocks)/2;
	if (iteration_counts >= 13) {
		indirect_iterations = iteration_counts - 12;
		iteration_counts = 12;
	}

	for (i = 0; i < 12; i++) {
		if (inode->i_block[i] != 0) {
			total_fixes += edit_or_recurse(inode->i_block[i]);
		}
	}

	if (indirect_iterations > 0) {
		int j;
		// Index 12 is the pointer to a indirect block
		int *indirect_blocks = (int *)(disk + (inode->i_block[12] * EXT2_BLOCK_SIZE));

		for (j = 0; j < indirect_iterations ; j++){
			// set all blocks found in indirect blocks into 0
			if (indirect_blocks[j] != 0) {
				total_fixes += edit_or_recurse(indirect_blocks[j]);
			}
		}

		// if code has entered this block; then it's nesseary to test if this 
		// block happens to be enabled
		if (verify_block_enabled_or_enable(inode->i_block[12])) {
			total_fixes += 1;
			printf("Fixed the indirect block of %d\n", inode->i_block[12]);
		}

	}
	// Call remove on the inode itself

	remove_victim_at_inode()
	return 0;
}


// Main function: takes in args, process them and ensure that they are correct 
// before passing them into opeartional function rm

int main (int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> (-r) <path>\n", argv[0]);
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

    int r_flag = 0;
    if (strlen(argv[2]) == 2 && strncmp(argv[2], "-r", 2) == 0) {
        r_flag = 1;
    }

	// remove trailing slashes from path
    remove_trailing_slashes(argv[2 + r_flag]);
	// verify that the path starts with '/' indicating absolute path
    if (verify_absolute_path_structure(argv[2 + r_flag]) == 0) {
        return ENOENT;
    }
    // Error check: absolute path must start from root directory
    if (strlen(argv[2 + r_flag]) >= 1 && argv[2 + r_flag][0] != '/') {
        return EINVAL;
    }
    // corner case
    if (strlen(argv[2 + r_flag]) == 1 && argv[2 + r_flag][0] == '/') {
        return EINVAL;
    }

    int parent_num;
    char child_name[strlen(argv[2 + r_flag]) + 1];
    
    // find the position of the string at which basename starts
    int basename_offset = get_basename_offset(argv[2 + r_flag]);
    if (basename_offset <= 0) {
        return ENOENT;
    }
    
    // construct the basename string
    strncpy(child_name, argv[2 + r_flag] + basename_offset, strlen(argv[2 + r_flag]) - basename_offset);
    child_name[strlen(argv[2 + r_flag]) - basename_offset] = '\0';
    
    // find the inode number of the parent directory
    parent_num = get_parent_inode_num_from_path(argv[2 + r_flag], basename_offset - 1);
    if (parent_num == -1) {
        return ENOENT;
    }
    struct ext2_inode* parent = get_inode_pointer(parent_num);
	// parent must be a directory
	if (find_filetype(parent->i_mode) != 'd') {
		return ENOTDIR;
	}

    int child_num = find_token_in_dir(parent_num, child_name);
    if (child_num == -1) {
        return ENOENT; // file not existing
    }
    struct ext2_inode* child = get_inode_pointer(child_num);

    r_delete_from_inode(child);



    // if child exists but is a directory
    if (find_filetype(child->i_mode) == 'd') {
        // perform recrusive remove on the directory
        recursive_remove_everything_in_dir(child_num);
        remove_dir_from_inode(parent_num, child_num);
    } else {
        // perform regular "remove" operation
        remove_victim_at_inode(parent_num, child_name);
    }


}
