#include <stdio.h>
#include <stdlib.h>
#include "libnetfiles.h"

int main() {

  //Try to open a file without netserverinit
  if(netopen("test.txt", ORDWR) == -1){ 
    printf("Test case: open file without netinit correctly threw an error.\n"); 
  } else { 
    printf("Test case: open file without netinit incorrectly attempted to open the file.\n");
    return -1; 
  }
  ///////////
 
  //Try to init to vi.cs.rutgers.edu
  if (netserverinit("vi.cs.rutgers.edu") == 0) { 
    printf("Test case: netserverinit correctly made connection to remote server.\n"); 
  } else{ 
    printf("Test case: netserverinit failed to make a connection to remote server.\n"); 
    return -1;
  }

  if(netopen("NONEXISTENT_FILE", O_RDWR) == -1) { 
    printf("Test case: netopen on a file that doesn't exist correctly threw an error.\n"); 
  } else {
    printf("Test case: netopen on a file that doesn't exist incorrectly tried to open the file.\n");
    return -1;
  }
  ////////////
  
  int fd = 0;
  char *r_buffer = malloc(4);
  const char *w_buffer = malloc(4);
  size_t nbyte_r = 4;
  size_t nbyte_w = 4;

  //Open readonly. The write command should throw error.
  fd = netopen("test.txt", O_RDONLY);
 
  if (fd == -1) {
   printf("error: could not open remote file @ line 39\n");
   return -1;
  }

  if (netread(fd, r_buffer, nbyte_r) == -1) {
    printf("error: could not read remote file @ line 46\n");
    return -1;
  }

  if (netwrite(fd, w_buffer, nbyte_w) == -1) {
    printf("Test case: open read-only and attempt to write correctly returned error\n");
  } else {
    printf("error: netwrite incorrectly worked on a readonly file @ line 51\n");
    return -1;
  }

  if (netclose(fd) == -1) {
    printf("error: could not close remote file @ line 58\n");
    return -1;
  }  
  //////////

  fd = 0;
  free(r_buffer);
  free(w_buffer);
  r_buffer = malloc(4);
  w_buffer = malloc(4);

  //Open writeonly. Read command should error out.
  fd = netopen("test.txt", O_WRONLY);
 
  if (fd == -1) {
    printf("error: could not open remote file @ line 73\n");
    return -1;
  }

  if (netread(fd, r_buffer, nbyte_r) == -1) {
    printf("Test case: open write only and attempt to read correctly returned error\n");
  } else {
    printf("error: netread incorrectly worked on a writeonly file @ line 78\n");
  }

  if(netwrite(fd, w_buffer, nbyte_w) == -1) {
    printf("error: could not write to remote file @ line 84\n");
    return -1;
  }

  if(netclose(fd) == -1) {
    printf("error: could not write to remote file @ line 89\n");
    return -1;
  }
  ////////////

  fd = 0;
  free(r_buffer);
  free(w_buffer);
  r_buffer = malloc(4);
  w_buffer = malloc(4);

  //Open read-write. Both commands should work.
  fd = netopen("test.txt", O_RDWR);

  if (fd == -1) {
    printf("error: could not open remote file @ line 104\n");
    return -1;
  }
  
  if (netread(fd, r_buffer, nbyte_r) == -1) {
    printf("error: netread failed in read/write mode @ line 109\n");
    return -1;
  } else {
    printf("Test case: open in R/W mode successful\n");
  }
 
  if (netwrite(fd, w_buffer, nbyte_w) == -1) {
    printf("error: netwrite failed in read/write mode @ line 116\n");
    return -1;
  } else {
    printf("Test case: write in R/W mode successful\n");
  }
  ////////////


  free(r_buffer);
  free(w_buffer);
  
  return 0;
}
