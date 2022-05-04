#include <sys/mman.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

#define SIZE ((size_t)1024*1024*1024*1)

int main(int argc, char* argv[]) {
  void *p = NULL;
  struct timeval starttv, endtv;
  int ret = 0;
  size_t index = 0;

  p = mmap(NULL, SIZE, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) {
    perror("mmap failed");
    return 0;
  }

  ret = madvise(p, SIZE, MADV_HUGEPAGE);
  if (ret < 0) {
    perror("madvise");
    return 0;
  }
  ret = gettimeofday(&starttv, NULL);
  if (ret < 0) {
    perror("gettimeofday starttv.");
    return 0;
  }

  for (index = 0; index<SIZE; index+=4096) {
    *((int*)(p+index)) = 0xc5;
  }

  ret = gettimeofday(&endtv, NULL);
  if (ret < 0) {
    perror("gettimeofday starttv.");
    return 0;
  }

  printf("time cost: %ld\n", (endtv.tv_sec - starttv.tv_sec)*1000000 + endtv.tv_usec - starttv.tv_usec);
  return 0;
}
