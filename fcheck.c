#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <stdbool.h>

#include "types.h"
#include "fs.h"

#define BLOCK_SIZE (BSIZE)


int
main(int argc, char *argv[])
{
  int r,i,n,fsfd;
  char *addr;
  struct dinode *dip;
  struct superblock *sb;
  struct dirent *de;
  struct stat *st;

  if(argc < 2){
    fprintf(stderr, "Usage: fcheck <file_system_image>\n");
    exit(1);
  }


  fsfd = open(argv[1], O_RDONLY);
  if(fsfd < 0){
    fprintf(stderr, "image not found.\n");
    exit(1);
  }

  /* get file size from fstat */
  st = (struct stat *) malloc(sizeof(struct stat *));
  r = fstat(fsfd, st);
  if (r != 0) {
    fprintf(stderr, "stat\n");
    exit(1);
  }
 
  addr = mmap(NULL, st->st_size, PROT_READ, MAP_PRIVATE, fsfd, 0);
  if (addr == MAP_FAILED){
	perror("mmap failed");
	exit(1);
  }
  /* read the super block */
  sb = (struct superblock *) (addr + 1 * BLOCK_SIZE);
  printf("fs size %d, no. of blocks %d, no. of inodes %d \n", sb->size, sb->nblocks, sb->ninodes);

  /* read the inodes */
  dip = (struct dinode *) (addr + IBLOCK((uint)0)*BLOCK_SIZE); 
  printf("begin addr %p, begin inode %p , offset %ld \n", addr, dip, (char *)dip -addr);

  // read root inode
  printf("Root inode  size %d links %d type %d \n", dip[ROOTINO].size, dip[ROOTINO].nlink, dip[ROOTINO].type);

  // get the address of root dir
  de = (struct dirent *) (addr + (dip[ROOTINO].addrs[0])*BLOCK_SIZE);
  
  // check root dir
  if (de == NULL || de[0].inum != ROOTINO || de[0].inum != de[1].inum)
    fprintf(stderr, "ERROR: root directory does not exist.\n");

  // print the entries in the first block of root dir 

  n = dip[ROOTINO].size/sizeof(struct dirent);
  for (i = 0; i < n; i++,de++){
 	printf(" inum %d, name %s ", de->inum, de->name);
  	printf("inode  size %d links %d type %d \n", dip[de->inum].size, dip[de->inum].nlink, dip[de->inum].type);
  }

  short inode_types[sb->ninodes];
  uint direct_blocks[sb->ninodes * (NDIRECT + 1)]; // + 1 to indicate if indirect block is in use
  uint indirect_blocks[sb->ninodes * NINDIRECT];

  int j, index;
  for (i = 0; i < sb->ninodes; i++) {
    // get inode types
    inode_types[i] = dip[i + 1].type;
    
    // in use inode
    if (inode_types[i] > 0) {
      // get direct blocks
      
      for (j = 0; j < NDIRECT; j++) {
        index = i * (NDIRECT + 1) + j;
        if (dip[i + 1].addrs[j] > 0) {
          direct_blocks[index] = dip[i + 1].addrs[j];
          printf("inode %d direct block %d = %d\n", i + 1, j, direct_blocks[index]);
        } else {
          direct_blocks[index] = 0;
          printf("inode %d direct block %d unused.\n", i + 1, j);
        }
      }

      // get indirect blocks
      uint indirect_block_no = dip[i + 1].addrs[NDIRECT];
      if (indirect_block_no <= 0)
        continue;
      
      index = (i + 1) * NDIRECT;      
      direct_blocks[index] = indirect_block_no;
      printf("inode %d using indirect blocks. indirect block = %d\n", i + 1, direct_blocks[index]);

      uint *ib = (uint *) (addr + indirect_block_no * BLOCK_SIZE);
      for (j = 0; j < NINDIRECT; j++) {
        index = i * NINDIRECT + j;
        indirect_blocks[index] = *(ib + j);
        if (indirect_blocks[index] > 0)
          printf("inode %d indirect block %d = %d\n", i + 1, j, indirect_blocks[index]);
      }
    } else {
      for (j = 0; j < NDIRECT; j++) {
        index = i * (NDIRECT + 1) + j;
        direct_blocks[index] = 0;
      }
    }
    
  }

  // get blocks in use in bitmap
  int bits_in_use[BPB * (sb->size/BPB) + (sb->size%BPB)];
  char *bp;
  int b, bi;

  for(b = 0; b < sb->size; b += BPB){
    bp = addr + BBLOCK(b, sb->ninodes) * BLOCK_SIZE;
    for(bi = 0; bi < BPB; bi++){
      int m = 1 << (bi % 8);
      if((*(bp + bi/8) & m) != 0)  // Is block in use?
        bits_in_use[b + bi] = 1;
      else if (b + bi < sb->size)
        bits_in_use[b + bi] = 0;
    }
  }

  // check inode types
  for (i = 1; i <= sb->ninodes; i++) {
    if (inode_types[i - 1] < 0 || inode_types[i - 1] > 3) {
      fprintf(stderr, "ERROR: bad inode.\n");
      exit(1);
    }
  }

  // check if direct blocks are marked in use in bitmap
  for (i = 0; i < sb->ninodes; i++) {
    for (j = 0; j < NDIRECT; j++) {
      index = i * (NDIRECT + 1) + j;
      if (direct_blocks[index] != 0) {
        if (bits_in_use[direct_blocks[index]] == 0) {
          fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
          exit(1);
        }
      }
    }
  }

  exit(0);

}

