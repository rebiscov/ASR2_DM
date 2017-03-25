#include <stdlib.h>
#include <stdio.h>
#include "utils.h"

void write(FILE *fd, char *bits){
  int i = 0;

  while(bits[i] != '\0'){
    switch (bits[i]){
    case '0':
      fputc(0, fd);
      break;
    case '1':
      fputc(1, fd);
      break;
    default:
      printf("Not a valid bitstring\n");
      exit(EXIT_FAILURE);
    }
    i++;
  }
}

void print(unsigned int out, int bits){
  int i = 0;
  for (i = bits-1; i >= 0; i--){
    printf("%d", (out >> i)&1);
  }
  printf("\n");
}

int main(int argc, char *argv[]){
  FILE *fd = NULL;
  unsigned int out;

  fd = fopen("example.ex", "wb+");
  printf("Input: %s\n", argv[1]);
  write(fd, argv[1]);
  fseek(fd, 0, SEEK_SET);

  out = read_uint32_bigendian(fd);
  print(out, 32);
  
  fclose(fd);

  return 0;
}
