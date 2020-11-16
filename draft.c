	if((t_inode->i_links_count) == 0 && t_inode->i_dtime > 0){
		unset_bitmap('i', t_idx); //delete inode using helper function
		int num_blocks = t_inode->i_blocks / 2;
		sb->s_free_inodes_count++;
		blgrp->bg_free_inodes_count++;

        for(i = 0; i < num_blocks; i++){    
            int block = t_inode->i_block[i];
            unset_bitmap('b', block); 
            sb->s_free_blocks_count++;
		    blgrp->bg_free_blocks_count++;
  
        }
	}else{		
		t_inode->i_dtime = 10;
	}
	
	//free allocated variables
	free(filepath); 
	free(filename);
	return 1;			//DO ERROR CHECKS FOR FILEPATH DIVIDE PATH
}