/* Copyright (C) 2023 Gramine contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/*
#include <stdio.h>

int main(void) {
    printf("Hello, world\n");
    return 0;
}
*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h> 
#include <time.h>
#include <sys/wait.h> 
#include <stdint.h>

/*
int main(void) {
    int ret = 0;

    printf("Hello, world\n");

    ret = syscall(451, "cp.txt", "rm.txt");
    if(ret != 0){
        printf("save syscall failed. ret:%d\n", ret);
        return 1;
    }
    printf("save syscall successed.\n");

    printf("restore successed.\n");
*/

/*
    pid_t pid = fork();

    if (pid < 0) {
        printf("fork failed\n");
        return 1;
    }
    else if (pid == 0) {
        printf("child process started\n");
        return 0;
    }
    else {
        printf("parent process, child pid=%d\n", pid);
        wait(NULL);
    }
*/

int main(void) {
    struct timespec start, end;
    long sec, nsec;
    int ret, ret2 = 0;
    uint64_t elapsed;

    printf("Hello, world\n");

    clock_gettime(CLOCK_MONOTONIC, &start);

    /* saveシステムコール呼び出し */
    ret = syscall(451, "cp.txt", "rm.txt");

    clock_gettime(CLOCK_MONOTONIC, &end);

    ret2 = syscall(452,&elapsed);

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

    printf("elapsed time: %lu usec\n", elapsed);

    return 0;
}


