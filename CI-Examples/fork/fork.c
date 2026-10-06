/*
#include <stdio.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h> 

int main() {
    int ret = 0;

    printf("save syscall run...\n");

    ret = syscall(451, "cp.txt", "rm.txt");

    if(ret != 0){
      printf("save syscall failed. ret:%d\n", ret);
    }
    else{
      printf("save syscall successed.\n");
    }

    return 0;
}
*/

/*
#include <stdio.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h> 
#include <time.h>

int main() {
    int ret = 0;
    struct timespec start, end;
    long sec, nsec;

    printf("save syscall run...\n");

    clock_gettime(CLOCK_MONOTONIC, &start);

    ret = syscall(451, "cp.txt", "rm.txt");

    clock_gettime(CLOCK_MONOTONIC, &end);

    sec  = end.tv_sec  - start.tv_sec;
    nsec = end.tv_nsec - start.tv_nsec;
    if (nsec < 0) {
        sec--;
        nsec += 1000000000L;
    }

    if(ret != 0){
      printf("save syscall failed. ret:%d\n", ret);
    }
    else{
      printf("save syscall successed.\n");
    }

    printf("elapsed time: %ld.%09ld seconds\n", sec, nsec);

    return 0;
}
*/


#include <stdio.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h> 
#include <time.h>

int main() {
    int ret = 0;
    struct timespec start, end;
    long sec, nsec;

    fork();

    printf("save syscall run...\n");

    clock_gettime(CLOCK_MONOTONIC, &start);
/*
    ret = syscall(451, "cp.txt", "rm.txt");
*/
    clock_gettime(CLOCK_MONOTONIC, &end);

    sec  = end.tv_sec  - start.tv_sec;
    nsec = end.tv_nsec - start.tv_nsec;
    if (nsec < 0) {
        sec--;
        nsec += 1000000000L;
    }

    if(ret != 0){
      printf("save syscall failed. ret:%d\n", ret);
    }
    else{
      printf("save syscall successed.\n");
    }

    printf("elapsed time: %ld.%09ld seconds\n", sec, nsec);

    return 0;
}


