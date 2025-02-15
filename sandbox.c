#define _GNU_SOURCE
#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <wait.h>
#include <memory.h>
#include <syscall.h>
#include <errno.h>
#include <unistd.h>

#include "sandbox.h"
#include "cgroups.h"

#define STACKSIZ (1024*1024)
static char cmd_stack[STACKSIZ];

void die(const char *fmt, ...) {
    va_list param;
    va_start(param, fmt);
    vfprintf(stderr, fmt, param);
    va_end(param);
    exit(1);
}

void parse_args(int argc, char **argv, struct params *params) {
#define NEXT_ARG() do { argc--; argv++; } while (0)
    NEXT_ARG();
    if (argc < 1) {
        printf("Nothing to do!\n");
        exit(0);
    }

    params->argv = argv;
#undef NEXT_ARG
}

void await_setup(int pipe) {
    char buf[2];
    if (read(pipe, buf, 2) != 2)
        die("failed to read from pipe: %m\n");
}

static int cmd_exec(void *arg) {
    struct params *params = (struct params*) arg;

    if (prctl(PR_SET_PDEATHSIG, SIGKILL))
        die("can't set death signal for child process: %m\n");

    await_setup(params->fd[0]);

    prepare_mntns("rootfs");
    
    if (setgid(0) == -1)
        die("Failed to setgid: %m\n");
    if (setuid(0) == -1)
        die("Failed to setuid: %m\n");

    char **argv = params->argv;
    char *cmd = argv[0];

    printf("############%s############\n", cmd);

    if (execvp(cmd, argv) == -1)
        die("failed to exec %s: %m\n", cmd);

    die("Execution completed\n");
    return 1;
}

void prepare_userns(int pid) {
    char path[100];
    char line[100];
    int uid = 1000;
    
    sprintf(path, "/proc/%d/uid_map", pid);
    sprintf(line, "0 %d 1\n", uid);
    write_file(path, line);

    sprintf(path, "/proc/%d/setgroups", pid);
    sprintf(line, "deny");
    write_file(path, line);

    sprintf(path, "/proc/%d/gid_map", pid);
    sprintf(line, "0 %d 1\n", uid);
    write_file(path, line);
}

void prepare_mntns(char *rootfs) {
    const char *mnt = rootfs;
    const char *old_fs = ".old_fs";

    if (mount(rootfs, mnt, "ext4", MS_BIND, ""))
        die("failed to mount %s: %m\n", rootfs, mnt);

    if (chdir(mnt))
        die("failed to chdir to rootfs mounted at %s: %m\n", mnt);

    if (mkdir(old_fs, 0777) && errno != EEXIST)
        die("failed to mkdir put_old %s: %m\n", old_fs);

    if (syscall(SYS_pivot_root, ".", old_fs))
        die("failed to execute pivot_root from %s to %s: %m\n", rootfs, old_fs);

    if (chdir("/"))
        die("failed to access new root: %m\n");

    prepare_procfs();
    
    if (umount2(old_fs, MNT_DETACH))
        die("failed to unmount old_fs %s: %m\n", old_fs);
}

void prepare_procfs(void) {
    if (mkdir("/proc", 0555) && errno != EEXIST)
        die("failed to make /proc: %m\n");
    
    if (mount("proc", "/proc", "proc", 0, ""))
        die("failed to mount proc: %m\n");
}

int main(int argc, char **argv) {
    struct params params;
    memset(&params, 0, sizeof(struct params));
    
    parse_args(argc, argv, &params);
    
    if (pipe(params.fd) < 0)
        die("Failed to create pipe: %m");
    
    int clone_flags = SIGCHLD | CLONE_NEWUTS | CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID;
    int cmd_pid = clone(cmd_exec, cmd_stack + STACKSIZ, clone_flags, &params);

    if (cmd_pid < 0)
        die("Failed to clone: %m\n");

    // setting up cgroups for the sandboxed process
    size_t memory_limit = 50 * 1024 * 1024;  // 50MB
    setup_cgroups("sandbox_group", cmd_pid, memory_limit, "50000 100000");

    int pipe = params.fd[1];

    prepare_userns(cmd_pid);
    
    if (write(pipe, "OK", 2) != 2)
        die("Failed to write to pipe: %m");
    
    if (close(pipe))
        die("Failed to close pipe: %m");
    
    if (waitpid(cmd_pid, NULL, 0) == -1)
        die("Failed to wait pid %d: %m\n", cmd_pid);

    return 0;
}