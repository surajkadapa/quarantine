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

#define STACKSIZ (1024*1024)
#define CGROUP_DIR "/sys/fs/cgroup"
#define MEMORY_LIMIT_FILE "memory.max"
#define CPU_MAX_FILE "cpu.max"
#define PROCS_FILE "cgroup.procs"
static char cmd_stack[STACKSIZ];


static void prepare_procfs();
static void prepare_mntns(char *rootfs);

struct params {
    int fd[2];
    char **argv;
};

static void die(const char *fmt, ...){
    va_list param;
    va_start(param, fmt);
    vfprintf(stderr, fmt, param);
    va_end(param);
    exit(1);
}

static void parse_args(int argc, char **argv, struct params *params){
#define NEXT_ARG() do { argc--; argv++; } while (0)
    NEXT_ARG();
    if (argc < 1) {
        printf("Nothing to do!\n");
        exit(0);
    }

    params->argv = argv;
#undef NEXT_ARG
}

void await_setup(int pipe){
    char buf[2];
    if(read(pipe, buf, 2) != 2)
        die("failed to read frm pipe: %m\n");
}

static int cmd_exec(void *arg){
    if (prctl(PR_SET_PDEATHSIG, SIGKILL))
        die("cant set_deathsig for child prcs: %m\n");

    struct params *params = (struct params*) arg;

    await_setup(params->fd[0]);

    prepare_mntns("rootfs");
    //assuming 0 in the current namespace maps to some unprivilaged UID in the parent namespace
    if (setgid(0) == -1)
        die("Failed to setgid: %m\n");
    if (setuid(0) == -1)
        die("Failed to setuid: %m\n");

    char **argv = params->argv;
    char *cmd = argv[0];

    printf("----------------%s---------------\n", cmd);

    if(execvp(cmd, argv) == -1)
        die("failed to exec %s: %m\n", cmd);

    die("done\n");
    return 1;
}


static void write_file(char *path, char *line){
    FILE *f = fopen(path, "w");
    if (f == NULL){
        die("failed to open file %s: %m\n", path);
    }
    if (fwrite(line, 1, strlen(line), f) < 0){
        die("failed to write to file %s: \n", path);
    }
    if (fclose(f) != 0){
        die("failed to close file %s: %m\n", path);
    }
}

static void prepare_userns(int pid){
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

static void prepare_procfs(){
    if(mkdir("/proc", 0555) && errno != EEXIST)
        die("failed to make /proc: %m\n");
    if(mount("proc","/proc","proc",0,""))
        die("failed to mount proc: %m\n");
}
static void prepare_mntns(char *rootfs){
    const char *mnt = rootfs;

    //mouting the rootfs from the alpine root filesystem
    if(mount(rootfs, mnt, "ext4", MS_BIND, ""))
        die("failed to mount %s: %m\n", rootfs, mnt);

    if(chdir(mnt))
        die("failed to chdir to rootfs mounted at %s: %m\n", mnt);

    const char *old_fs = ".old_fs";
    if(mkdir(old_fs, 0777) && errno != EEXIST)
        die("failed to mkdir put_old %s: %m\n", old_fs);

    if(syscall(SYS_pivot_root, ".", old_fs))
        die("failed to execute pivot_root from %s to %s: %m\n", rootfs, old_fs);

    if(chdir("/"))
        die("failed to access new root: %m\n");
    //have to unmount the old filesystem(old_fs)

    //need to create a new PID namespace
    prepare_procfs();
    if(umount2(old_fs, MNT_DETACH))
        die("failed to unmount old_fs %s: %m\n", old_fs);
}
void setup_cgroups(const char *cgroup_name, int pid, size_t memory_limit, const char *cpu_max) {
    char path[256];

    // Create the cgroup directory
    snprintf(path, sizeof(path), "%s/%s", CGROUP_DIR, cgroup_name);
    if (mkdir(path, 0755) == -1 && errno != EEXIST) {
        die("failed to create cgroup directory %s: %m\n", path);
    }

    // Set memory limits
    snprintf(path, sizeof(path), "%s/%s/memory.max", CGROUP_DIR, cgroup_name);
    char mem_limit_str[20];
    snprintf(mem_limit_str, sizeof(mem_limit_str), "%zu", memory_limit);
    write_file(path, mem_limit_str);

    // Set CPU max (debug values)
    snprintf(path, sizeof(path), "%s/%s/cpu.max", CGROUP_DIR, cgroup_name);
    printf("Writing to %s: %s\n", path, cpu_max); // Debug
    write_file(path, cpu_max);

    // Add process to the cgroup
    snprintf(path, sizeof(path), "%s/%s/cgroup.procs", CGROUP_DIR, cgroup_name);
    char pid_str[20];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);
    write_file(path, pid_str);
}

int main(int argc, char **argv)
{
    struct params params;
    memset(&params, 0, sizeof(struct params));
    parse_args(argc, argv, &params);
    if (pipe(params.fd) < 0)
        die("Failed to create pipe: %m");
    int clone_flags = SIGCHLD | CLONE_NEWUTS | CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID;
    int cmd_pid = clone(cmd_exec, cmd_stack + STACKSIZ, clone_flags, &params);

    if (cmd_pid < 0)
        die("Failed to clone: %m\n");

    //setting up cgroups for the sandboxed process
    size_t memory_limit =  50*1024*1024; //50MB
    int cpu_shares = 256;
    setup_cgroups("sandbox_group", cmd_pid, 104857600, "50000 100000");

    int pipe = params.fd[1];

    prepare_userns(cmd_pid); //namepspace setup
    if (write(pipe, "OK", 2) != 2)
        die("Failed to write to pipe: %m");
    if (close(pipe))
        die("Failed to close pipe: %m");
    if (waitpid(cmd_pid, NULL, 0) == -1)
        die("Failed to wait pid %d: %m\n", cmd_pid);

    return 0;
}
