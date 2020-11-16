#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <libgen.h>
#include "ext2.h"
#include "helper.h"

int traverse_and_verifiy_inodes(struct ext2_inode *inode);

unsigned char convert_file_type(unsigned short i_mode){
    unsigned int mask = 15 << 12; 
    unsigned int type = (i_mode & mask);

    if (type == EXT2_S_IFDIR) {
        return EXT2_FT_DIR;
    } else if (type == EXT2_S_IFREG) {
        return EXT2_FT_REG_FILE;
    } else if (type == EXT2_S_IFLNK) {
        return EXT2_FT_SYMLINK;
    }
    // Otherwise, return default file type
    return EXT2_FT_UNKNOWN; 
}



// Verify if block is enabled. If not, enables it
// Returns 1, if block has not been enabled
int verify_block_enabled_or_enable(int block_num) {
	if (get_block_bit_value(block_num) == 0) {
		update_block_bitmap(block_num, 1);
		return 1;
	} else {
		return 0;
	}
}

// Function perfroms test e
int verify_blocks_form_inode(struct ext2_inode *test_inode){
	// Traverse though each block in the inode, indicated by *assumingly* 
	int i;
	int indirect_iterations = 0;
	int error_count = 0;

	int iteration_counts = (test_inode->i_blocks)/2;
	if (iteration_counts >= 13){
		indirect_iterations = iteration_counts - 12;
		iteration_counts = 12;
	}

	for (i = 0 ; i < 12 ; i++) {
		if (test_inode->i_block[i] != 0) {
			error_count += verify_block_enabled_or_enable(test_inode->i_block[i]);
		}
	}

	if (indirect_iterations > 0) {
		// Check the indirect block itself:
		error_count += verify_block_enabled_or_enable(test_inode->i_block[12]);
		int j;
		// Index 12 is the pointer to a indirect block
		int *indirect_blocks = (int *)(disk + (test_inode->i_block[12] * EXT2_BLOCK_SIZE));

		for (j = 0 ; j < indirect_iterations ; j++){
			error_count += verify_block_enabled_or_enable(indirect_blocks[j]);
		}
		
	}
	return error_count;

}


// For ext2_checker: indirect recursive function that takes a paritualr 
// block_number, verifiy data intergratity (features b, c, d, e) of those
// each ext2_file_entry in block if they are not a dir; otherwise intiaize
// recursion by calling traverse_and_verifiy_inodes on the dir.
int edit_or_recurse(int block_num){
	int offset = 0;
	int total_errors = 0;
	struct ext2_dir_entry *curr_entry; 

	while (offset < EXT2_BLOCK_SIZE) {
		curr_entry = get_dir_entry_pointer(block_num, offset);
		if (curr_entry->inode ==0) {
			offset += curr_entry->rec_len;
			continue;
		}
		// The real type of the dir_entry can only be veriifed though inode's
		// i_mode:
		struct ext2_inode *curr_entry_inode = get_inode_pointer(curr_entry->inode);

		// Test and protentially fix for feature (b)
		if (curr_entry->file_type != convert_file_type(curr_entry_inode->i_mode)){
			// There exist need to fix; perform. 
			curr_entry->file_type = convert_file_type(curr_entry_inode->i_mode);
			total_errors += 1;
			printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", curr_entry->inode);
		}

		// Continue testing: (c)
		if (get_inode_bit_value(curr_entry->inode) == 0) {
			// There exist problem for enablity; enable the bit on table
			update_inode_bitmap(curr_entry->inode, 1);
			total_errors += 1;
			printf("Fixed: inode [%d] not marked as in-use\n", curr_entry->inode);
		}

		// Continue testing: (d)
		if (curr_entry_inode->i_dtime != 0) {
			curr_entry_inode->i_dtime = 0;
			total_errors += 1;
			printf("Fixed: valid inode marked for deletion: [%d]\n", curr_entry->inode);
		}

		// Continue testing: (e)
		int fix_e = verify_blocks_form_inode(curr_entry_inode);
		if (fix_e > 0) {
			total_errors += fix_e;
			printf("Fixed: D in-use data blocks not marked in data bitmap for inode: [%d]\n", curr_entry->inode);
		}

		offset += curr_entry->rec_len;
		
		// Now with all the problem sloved with this ext2_dir_entry, decide if this is 
		// end case or continue to (indirect) recurse
		if (curr_entry->file_type == EXT2_FT_DIR) {
			// Continue to recurse; feed traverse_and_verifiy_inodes with 
			// curr_entry_inode
			if (!(curr_entry->name_len == 1 && strncmp(curr_entry->name, ".", 1) == 0) 
				&& !(curr_entry->name_len == 2 && strncmp(curr_entry->name, "..", 2) == 0)) {
				total_errors += traverse_and_verifiy_inodes(curr_entry_inode);
			}
		} 
	}

	return total_errors;

}

// Indirect recursive function that works with edit_or_recurse. Will 
// verifiy feature b, c, d, e for the inode and it's children.
// NOTE: because it visit childrens, ext2_inode must be a parent and DIR!!!
int traverse_and_verifiy_inodes(struct ext2_inode *inode) {
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
	return total_fixes;
}

// Since all these steps all relating in visiting each *enabled* inode, they shall
// be traversed together
int step_b_c_d_e(){
	struct ext2_inode *root_inode = get_inode_pointer(EXT2_ROOT_INO);
	int edits = traverse_and_verifiy_inodes(root_inode);
	return edits;
}

// Perfrom step a
int step_a(){
	// Assume the total block count in superblock is correct
	int total_fixes = 0;
	// count free inode and blocks from bitmap
	int bitmap_free_inodes = free_inode_count_from_bitmap();
	int bitmap_free_blocks = free_block_count_from_bitmap();
	
	if (bitmap_free_blocks != sb->s_free_blocks_count) {
		int diff = bitmap_free_blocks - sb->s_free_blocks_count;
		if (diff < 0) {
			diff = diff * (-1);
		}
		
		total_fixes += diff;
		sb->s_free_blocks_count = bitmap_free_blocks;
		printf("Fixed: Superblock's free blocks counter was off by %d compared to the bitmap\n", diff);
	}
	if (bitmap_free_blocks != gd->bg_free_blocks_count) {
		int diff = bitmap_free_blocks - gd->bg_free_blocks_count;
		if (diff < 0) {
			diff = diff * (-1);
		}
		total_fixes += diff;
		gd->bg_free_blocks_count = bitmap_free_blocks;
		printf("Fixed: Group descriptor's free blocks counter was off by %d compared to the bitmap\n", diff);
	}
	if (bitmap_free_inodes != sb->s_free_inodes_count) {
		int diff = bitmap_free_inodes - sb->s_free_inodes_count;
		if (diff < 0) {
			diff = diff * (-1);
		}
		total_fixes += diff;
		sb->s_free_inodes_count = bitmap_free_inodes ;
		printf("Fixed: Superblock's free inodes counter was off by %d compared to the bitmap\n", diff);
	}
	if (bitmap_free_inodes != gd->bg_free_inodes_count) {
		int diff = bitmap_free_inodes - gd->bg_free_inodes_count;
		if (diff < 0) {
			diff = diff * (-1);
		}
		total_fixes += diff;
		gd->bg_free_inodes_count = bitmap_free_inodes ;
		printf("Fixed: Group descriptor's free inodes counter was off by %d compared to the bitmap\n", diff);
	}
	return total_fixes;

}


int main (int argc, char **argv) {
	int total = 0;
	// As always, main function contains arg tests
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
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

    
	total += step_a();
    total += step_b_c_d_e();

    if (total > 0){
    	printf("%d file system inconsistencies repaired!\n", total);
    } else {
    	printf("No file system inconsistencies detected!\n");
    }

}
