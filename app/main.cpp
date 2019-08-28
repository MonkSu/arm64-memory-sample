#include <stdio.h>
#include <malloc.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/types.h>
#include <fcntl.h>
#include <stdlib.h>

#define MAP_SIZE (10*4096)

int bss_var;
int data_var0 = 1;

int main(int argc,char **argv)
{
    int i;
    int fd, ret = 0;
    char *map_vadr;
    unsigned long vadr;

#if 1
    printf("\n");
    printf("_________________________________________________________\n\n");
    printf("Text location:\n");
    printf("Address of main(Code Segment):0x%016lX\n",main);
    printf("_________________________________________________________\n\n");
    int stack_var0 = 2;
    printf("Stack Location:\n");
    printf("Initial end of stack:0x%016lX\n",&stack_var0);
    int stack_var1 = 3;
    printf("new end of stack:0x%016lX\n",&stack_var1);
    printf("_________________________________________________________\n\n");
    printf("Data Location:\n");
    printf("Address of data_var(Data Segment):0x%016lX\n",&data_var0);
    static int data_var1 = 4;
    printf("New end of data_var(Data Segment):0x%016lX\n",&data_var1);
    printf("_________________________________________________________\n\n");
    printf("BSS Location:\n");
    printf("Address of bss_var:0x%016lX\n",&bss_var);
    printf("_________________________________________________________\n\n");
    char *heap0 = (char*)sbrk((ptrdiff_t)0);
    printf("Heap Location:\n");
    printf("Initial end of heap:0x%016lX\n",heap0);
    brk(heap0 + 4);
    heap0 = (char*)sbrk((ptrdiff_t)0);

    printf("New end of heap:0x%016lX\n",heap0);
    printf("_________________________________________________________\n\n");

    char *heap1_mmap =  (char*)malloc(4*1024*1024);
    printf("Mmap Location:\n");
    printf("mmap space: 0x%016lX \n", heap1_mmap);

    printf("_________________________________________________________\n\n");
#endif
#if 0
    int i = 0;
    char* heap_out[800] = {0};
    /* malloc can reach the virtual space limit */
    for (i = 0; i < sizeof(heap_out)/sizeof(heap_out[0]); ++i){
        heap_out[i] = (char*) malloc(100*1024);
        if (heap_out[i] == NULL) {
            printf("out of memberoy\n");
            return 0;
        }
        if (i%80 == 0) {
            printf("malloc [%03d] : 0x%016lX \n", i, heap_out[i]);
        }
        usleep(100*1000);
    }
    /* But if you use this part of the space, it is limited by physical memory.
       Because there is no swap space */
    for (i = 0; i < sizeof(heap_out)/sizeof(heap_out[0]); ++i) {
        if (i%80 == 0) {
            printf("set heap_out[%03d]=0x%016lX\n", i, heap_out[i]);
        }
        memset(heap_out[i], 1, 100*1024);
        /* crash out of memory !! */
    }
#endif
    sleep(1);

    if ((fd = open("/dev/mapdrv", O_RDWR)) < 0) {
        perror("open");
        ret = -1;
        goto fail_open;
    }
    printf("To mmap size     : 0x%016lX \n", MAP_SIZE);

    map_vadr = (char*)mmap((void*)0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED , fd, 0);
    if (map_vadr == MAP_FAILED) {
        perror("mmap");
        ret = -1;
        printf("Failed to mmap\n");
        goto fail_close;
    }
    printf("To mmap map_vadr : 0x%016lX \n", map_vadr);
    for (vadr = (unsigned long)map_vadr, i = 0;
         vadr < (unsigned long)&(map_vadr[MAP_SIZE]);
         vadr += 4096, i++) {
         printf("mmap %d %s\n", i, (char*)vadr);
         sprintf((char *)vadr, "User vmalloc space ! %d", i);
    }
    if (-1 == munmap(map_vadr, MAP_SIZE)) {
        ret = -1;
    }
fail_close:
    close(fd);
fail_open:
    exit(ret);
}