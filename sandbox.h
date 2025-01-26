#ifndef SANDBOX_H
#define SANDBOX_H

#include <stddef.h>

struct params {
    int fd[2];   
    char **argv; 
};

void parse_args(int argc, char **argv, struct params *params);

void await_setup(int pipe);

void prepare_userns(int pid);

void prepare_mntns(char *rootfs);

void prepare_procfs(void);

void die(const char *fmt, ...);

#endif 