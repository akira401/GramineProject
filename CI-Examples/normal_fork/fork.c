#include <stdio.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h> 

int main() {
    int ret = 0;

    printf("save syscall run...\n");

    ret = syscall(451, "test.txt", "test2.txt");

    if(ret != 0){
      printf("save syscall failed. ret:%d\n", ret);
    }
    else{
      printf("save syscall successed.\n");
    }

    return 0;
}
