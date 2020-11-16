#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "ext2.h"

unsigned char *disk;
struct ext2_group_desc *gd;
struct ext2_super_block *sb;
/*
    Given inode number, return the pointer to an inode struct from the inode table.
 */
struct ext2_inode *get_inode_pointer(int inode_num){
    int inode_idx = inode_num - 1;
    struct ext2_inode *inode = (struct ext2_inode *)(disk + (1024 * gd->bg_inode_table) + inode_idx*sizeof(struct ext2_inode));
    return inode;
}
/*
    Given block offset and a block index, return the pointer to a dir_entry struct.
 */
struct ext2_dir_entry *get_dir_entry_pointer(int block_num, int block_offset) {
    struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *)(disk + (block_num * EXT2_BLOCK_SIZE) + block_offset);
    return dir_entry;
}

/*
    Given inode number and i_block number, return the data block number.
 */
int get_block_number(int inode_num, int i_block_idx) {
    struct ext2_inode *inode = get_inode_pointer(inode_num);
    return inode->i_block[i_block_idx];
}

int get_block_bit_value(int block_num) {
    int block_idx = block_num - 1;
    int offset = block_idx % 8;
    int at_byte = (block_idx - offset) / 8;

    // Read byte at interest and output the according value
    char mask = 1;
    char block_char;
    strncpy(&block_char, (char *) (disk + gd->bg_block_bitmap* EXT2_BLOCK_SIZE + sizeof(char) * at_byte), sizeof(char));

    char bit;
    bit = block_char >> offset;
    bit = bit & mask;
    return bit;
}

int get_inode_bit_value(int inode_num) {
    int inode_idx = inode_num - 1;
    
    int offset = inode_idx % 8;
    int at_byte = (inode_idx - offset) / 8;

    // Read byte at interest and output the according value
    char mask = 1;
    char inode_char;
    strncpy(&inode_char, (char *) (disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE + sizeof(char) * at_byte), sizeof(char));

    char bit;
    bit = inode_char >> offset;
    bit = bit & mask;
    return bit;
}

// Modifies the bit at bit_index of block bitmap so that it becomes equivalent to 'value'
void update_block_bitmap(int block_num, int value) {
	// Verify that an update is "needed" by checking in bitmap to see if value is already set.
    if (get_block_bit_value(block_num) == value){
        // Exit functon now; no change is needed.
        return;
    }

    int bit_idx = block_num - 1;
    
    // Update both data in superblock and group descriptor first
    if (value == 0) {
        gd->bg_free_blocks_count++;
        sb->s_free_blocks_count++;
    } else if (value == 1) {
        gd->bg_free_blocks_count--;
        sb->s_free_blocks_count--;
    }
    // modifiy the bitmap
    char *bitmap; 
    bitmap = (char *) (disk + EXT2_BLOCK_SIZE *gd->bg_block_bitmap);
    int offset = bit_idx % 8;

    char bit_to_modify = bitmap[bit_idx / 8];

    if (value == 1) { 
        bit_to_modify = bit_to_modify | (1 << offset);
    } else {
        bit_to_modify = bit_to_modify & (~(1 << offset));
    }
    
    bitmap[bit_idx / 8] = bit_to_modify;
}

void update_inode_bitmap(int inode_num, int value) {
	// Verify that an update is "needed" by checking in bitmap to see if value is already set.
    if (get_inode_bit_value(inode_num) == value){
        // Exit functon now; no change is needed.
        return;
    }

    int inode_idx = inode_num - 1;
    // Update both data in superinode and group descriptor first
    if (value == 0) {
        gd->bg_free_inodes_count++;
        sb->s_free_inodes_count++;
    } else if (value == 1) {
        gd->bg_free_inodes_count--;
        sb->s_free_inodes_count--;
    }

    // Now modifiy the bitmap
    char *bitmap; 
    bitmap = (char *) (disk + EXT2_BLOCK_SIZE * gd->bg_inode_bitmap);
    int offset = inode_idx % 8;

    char bit_to_modify = bitmap[inode_idx / 8];
    if (value == 1) { 
        bit_to_modify = bit_to_modify | (1 << offset);
    } else {
        bit_to_modify = bit_to_modify & (~(1 << offset));
    }
    
    bitmap[inode_idx / 8] = bit_to_modify;
    
}

/*
    Returns the block number of the first available data block.
    Returns -1, if there are no available data blocks.
 */
int find_first_available_block() {
    int block_num;
    for (block_num = 1 ; block_num <= sb->s_blocks_count ; block_num++) {
        if (get_block_bit_value(block_num) == 0) {
            update_block_bitmap(block_num, 1);
            return block_num;
        }
    }
    return -1;
}


/*
    Returns the inode number of the first available inode.
    Returns -1, if there are no available inodes.
 */
int find_first_available_inode() {
    int inode_num;
    for (inode_num = EXT2_GOOD_OLD_FIRST_INO + 1 ; inode_num <= sb->s_inodes_count ; inode_num++) {
        if (get_inode_bit_value(inode_num) == 0) {
            update_inode_bitmap(inode_num, 1);
            return inode_num;
        }
    }
    return -1;
}

/*
    Given an i_mode found in the inode struct, return the type of file.
 */
char find_filetype(unsigned short i_mode) {
    // Create object mask for the 4 bits that we care in i_mode:
    unsigned int mask = 15 << 12; 
    unsigned int type = (i_mode & mask);

    if (type == EXT2_S_IFDIR) {
        return 'd';
    } else if (type == EXT2_S_IFREG) {
        return 'f';
    } else if (type == EXT2_S_IFLNK) {
        return 'l';
    }
    // Otherwise, return default file type
    return 'u'; 
}

/*
    Given an inode number of a directory, and a name, find if that name is in the directory.
    Return the inode number of the object with the same name as the token, if found.
    Return -1, if not found.

    if object exists, but is not a dir, return negative value of inode number
 */
int find_token_in_dir(int inode_num, char *token) {
    struct ext2_inode *inode = get_inode_pointer(inode_num);
    // inode must be of directory type
    int i;
    for (i = 0 ; i < inode->i_blocks / 2 ; i++) {
        // cycle through all the elements in i_block in this directory
        int block_offset = 0;
        while (block_offset < EXT2_BLOCK_SIZE) {
            struct ext2_dir_entry *dir_entry = get_dir_entry_pointer(inode->i_block[i], block_offset);
            // if token is found, update inode_index to search for the next token in the path
            if (strncmp(dir_entry->name, token, dir_entry->name_len) == 0) {
                //inode_index = i_block_index;
                return dir_entry->inode;
            }
            block_offset += dir_entry->rec_len;
        }
    }
    return -1;
}

int verify_absolute_path_structure(char *path) {
    if (strlen(path) >= 1 && path[0] == '/') {
        return 1;
    }
    return 0;
}

void remove_trailing_slashes(char *path) {
    int idx = strlen(path) - 1;
    while (idx > 0 && path[idx] == '/') {
        path[idx] = '\0';
        idx -= 1;
    }
}

/*
    Given a path, find the position of the basename.
 */
int get_basename_offset(char *path) {
    // start from the end of path
    int offset = strlen(path) - 1;
    while (offset >= 0 && path[offset] != '/') {
        offset -= 1;
    }
    return offset + 1;
}

/*
    Given a path, return the inode number of the parent directory.
    Return -1 if any part of the parent path is not a valid directory in that path.
    Also handles repeated slashes (ex. /a/b///c/)
 */
int get_parent_inode_num_from_path(char *path, int last_slash_offset) {
    // edge case: last slash happens to be root
    if (last_slash_offset == 0) {
        return EXT2_ROOT_INO;
    }
    int i;
    int repeated_slash = 1;
    int slash_idx = 0;
    int inode_num = EXT2_ROOT_INO;
    for (i = 1 ; i <= last_slash_offset ; i++) {
        if (path[i] != '/') {
            repeated_slash = 0;
        } else if (path[i] == '/' && repeated_slash == 1) {
            slash_idx = i;
        } else if (path[i] == '/' && repeated_slash == 0) {
            char name[i - slash_idx];
            strncpy(name, path + slash_idx + 1, i - slash_idx - 1);
            name[i - slash_idx] = '\0';

            struct ext2_inode *inode = get_inode_pointer(inode_num);
            if (find_filetype(inode->i_mode) != 'd') {
                return -1;
            }
            inode_num = find_token_in_dir(inode_num, name);
            if (inode_num == -1) {
                return -1;
            }
            slash_idx = i;
        }
    }
    return inode_num;
}

/*
    Create a new inode with the given inode number and file type.
    Returns the pointer to the newly created inode struct.
 */
struct ext2_inode *make_inode(int inode_num, char type) {
    struct ext2_inode *inode = get_inode_pointer(inode_num);
    // set inode type
    if (type == 'd') {
        inode->i_mode = EXT2_S_IFDIR;
    } else if (type == 'f') {
        inode->i_mode = EXT2_S_IFREG;
    } else if (type == 's') {
        inode->i_mode = EXT2_S_IFLNK;
    }

    inode->i_links_count = 0; // this gets incremented by helpers

    int i_block_idx;
    for (i_block_idx = 0 ; i_block_idx < 15 ; i_block_idx++) {
        inode->i_block[i_block_idx] = 0;
    }
    inode->i_block[0] = 0;
    inode->i_blocks = 0;
    inode->i_size = EXT2_BLOCK_SIZE;
    inode->osd1 = 0; // as said in ext2.h
    inode->i_gid = 0; // as said in ext2.h
    inode->i_uid = 0; // as said in ext2.h
    inode->i_dtime = 0;

    return inode;
}

/*
    Given the length of the name of the directory entry, compute the rec_len.
    Note: dir_entry->name is NOT null-terminated, so we cannot use strlen(dir_entry->name).
    However, we can use strlen when we first set dir_entry->name_len because the original string is null-terminated
 */
int compute_rec_len(int name_len) {
    int len = 8 + name_len;
    len = len + (4 - (len % 4));
    return len;
}

/*
    Given an index to a block of a directory, return the block offset of the start of the last dir_entry.
    If the block does not contain any dir_entries, return -1;
 */
int find_offset_of_last_dir_entry(int block_num) {
    int block_offset = 0;
    struct ext2_dir_entry *entry = get_dir_entry_pointer(block_num, block_offset);
    while (block_offset + entry->rec_len < EXT2_BLOCK_SIZE) {
        block_offset = block_offset + entry->rec_len;
        entry = get_dir_entry_pointer(block_num, block_offset);
    }
    return block_offset;
}

// Finds the first available i_block that can fit a dir_entry with the given name length
// Returns i_block index on success, -1 otherwise
int first_available_i_block(int inode_num, int name_len) {
    struct ext2_inode *inode = get_inode_pointer(inode_num);
    int i;
    for (i = 0; i < 12; i++) {
        // No block is assigned to this i_block
        if (inode->i_block[i] == 0) {
            return i;
        }

        int target_rec_len = compute_rec_len(name_len);
        int block_offset = 0;
        struct ext2_dir_entry *last_entry = get_dir_entry_pointer(inode->i_block[i], block_offset);
        while (last_entry->rec_len + block_offset < EXT2_BLOCK_SIZE) {
            block_offset = block_offset + last_entry->rec_len;
            last_entry = get_dir_entry_pointer(inode->i_block[i], block_offset);
        }
        int true_last_rec_len = compute_rec_len(last_entry->name_len);
        int available_space = EXT2_BLOCK_SIZE - block_offset - true_last_rec_len;
        if (target_rec_len <= available_space) {
            return i;
        }
    }
    return -1;
}

int inode_needs_new_block_for_new_dir_entry(int inode_num, int name_len) {
    int i_block_idx = first_available_i_block(inode_num, name_len);
    struct ext2_inode* inode = get_inode_pointer(inode_num);
    if (inode->i_block[i_block_idx] == 0) {
        return 1;
    }
    return 0;
}

void make_dir_entry_in_inode(int dir_num, char *entry_name, int entry_num, char type) {
    struct ext2_inode *dir = get_inode_pointer(dir_num);
    int i_block_idx = first_available_i_block(dir_num, strlen(entry_name));

    // the starting position in the block at which the new_entry will be placed
    int new_entry_offset;
    if (dir->i_block[i_block_idx] == 0) {
        dir->i_block[i_block_idx] = find_first_available_block();
        dir->i_blocks += 2;
        new_entry_offset = 0;
    } else {
        // find the last dir_entry in existing block and update its rec_len
        int last_offset = find_offset_of_last_dir_entry(dir->i_block[i_block_idx]);
        struct ext2_dir_entry *last_entry = get_dir_entry_pointer(dir->i_block[i_block_idx], last_offset);
        // remove padding
        last_entry->rec_len = compute_rec_len(last_entry->name_len); 
        new_entry_offset = last_offset + last_entry->rec_len;
    }

    // increase the link_count for the inode that the new_dir_entry points to
    struct ext2_inode *target_inode = get_inode_pointer(entry_num);
    target_inode->i_links_count += 1;

    // make new dir_entry at the position after the last dir_entry
    struct ext2_dir_entry *new_entry = get_dir_entry_pointer(dir->i_block[i_block_idx], new_entry_offset);
    new_entry->inode = entry_num;
    new_entry->name_len = strlen(entry_name);
    strncpy(new_entry->name, entry_name, new_entry->name_len);
    new_entry->rec_len = EXT2_BLOCK_SIZE - new_entry_offset; // padding added, this is now the last entry
    
    if (type == 'f') {
        new_entry->file_type = EXT2_FT_REG_FILE;
    } else if (type == 's') {
        new_entry->file_type = EXT2_FT_SYMLINK;
    } else if (type == 'd') {
        new_entry->file_type = EXT2_FT_DIR;
    } else {
        new_entry->file_type = EXT2_FT_UNKNOWN;
    }
}


int free_inode_count_from_bitmap() {
    int i;
    int free_inodes = 0;
    for (i = 1 ; i <= (sb->s_inodes_count) ; i++) {
        if (get_inode_bit_value(i) == 0) {
            free_inodes += 1;
        }
    }
    return free_inodes;
}


int free_block_count_from_bitmap() {
    int i;
    int free_blocks = 0;
    for (i = 1; i <= (sb->s_blocks_count) ; i++) {
        if (get_block_bit_value(i) == 0) {
            free_blocks += 1;
        }
    }
    return free_blocks;
}