#include <stdio.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h> 

int main() {
    int ret = 0;

    ret = fork();

    return 0;
}
