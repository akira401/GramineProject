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

int main(void) {
    int ret = 0;

    printf("Hello, world\n");

/*
    ret = syscall(451, "cp.txt", "rm.txt");
    if(ret != 0){
        printf("save syscall failed. ret:%d\n", ret);
        return 1;
    }
    printf("save syscall successed.\n");
*/

    /* saveの後にforkして子プロセスで復元処理を確認 */
    pid_t pid = fork();

    if (pid < 0) {
        printf("fork failed\n");
        return 1;
    }
    else if (pid == 0) {
        /* 子プロセス（childモード）
         * receive_checkpoint_and_restore が実行される */
        printf("child process started\n");
        /* 子プロセスは復元処理後に何もせず終了 */
        return 0;
    }
    else {
        /* 親プロセス */
        printf("parent process, child pid=%d\n", pid);
        wait(NULL);
    }

    return 0;
}

