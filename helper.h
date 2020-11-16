#include "ext2.h"

extern unsigned char *disk;
extern struct ext2_group_desc *gd;
extern struct ext2_super_block *sb;

struct ext2_inode *get_inode_pointer(int inode_num);
struct ext2_dir_entry *get_dir_entry_pointer(int block_num, int block_offset);
int get_block_number(int inode_num, int i_block_idx);

int get_block_bit_value(int block_num);
int get_inode_bit_value(int inode_num);
void update_block_bitmap(int block_num, int value);
void update_inode_bitmap(int inode_num, int value);

int find_first_available_block();
int find_first_available_inode();
int first_available_i_block(int inode_num, int name_len);

char find_filetype(unsigned short i_mode);

int find_token_in_dir(int inode_num, char *token);

int verify_absolute_path_structure(char *path);
void remove_trailing_slashes(char *path);
int get_basename_offset(char *path);
int get_parent_inode_num_from_path(char *path, int last_slash_offset);

struct ext2_inode *make_inode(int inode_num, char type);
int compute_rec_len(int name_len);
int find_offset_of_last_dir_entry(int block_num);

int inode_needs_new_block_for_new_dir_entry(int inode_num, int name_len);
void make_dir_entry_in_inode(int dir_num, char *entry_name, int entry_num, char type);

int free_inode_count_from_bitmap();
int free_block_count_from_bitmap();
