int find_non_dir_type_token_in_dir(int inode_num, char *token) {
    struct ext2_inode *inode = get_inode_pointer(inode_num);
    // inode must be of directory type
    int i;
    for (i = 0 ; i < inode->i_blocks / 2 ; i++) {
        char *block =(char*)(disk + EXT2_BLOCK_SIZE*(inode->i_block[i]));
        // cycle through all the elements in i_block in this directory
        int block_offset = 0;
        while (block_offset < EXT2_BLOCK_SIZE) {
            struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry*) (block + block_offset);
            // if token is found, update inode_index to search for the next token in the path
            if (dir_entry->file_type == EXT2_FT_DIR && strcmp(dir_entry->name, token) == 0) {
                //inode_index = i_block_index;
                return dir_entry->inode;
            }
            block_offset += dir_entry->rec_len;
        }
    }
    return -1;
}