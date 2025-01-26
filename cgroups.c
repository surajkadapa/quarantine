#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "cgroups.h"

#define CGROUP_DIR "/sys/fs/cgroup"
#define MEMORY_LIMIT_FILE "memory.max"
#define CPU_MAX_FILE "cpu.max"
#define PROCS_FILE "cgroup.procs"

void write_file(char *path, char *line) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "Failed to open file %s: %m\n", path);
        exit(1);
    }
    
    if (fwrite(line, 1, strlen(line), f) < 0) {
        fprintf(stderr, "Failed to write to file %s\n", path);
        fclose(f);
        exit(1);
    }
    
    if (fclose(f) != 0) {
        fprintf(stderr, "Failed to close file %s: %m\n", path);
        exit(1);
    }
}

void setup_cgroups(
    const char *cgroup_name, 
    int pid, 
    size_t memory_limit, 
    const char *cpu_max
) {
    char path[256];

    // create the cgroup directory
    snprintf(path, sizeof(path), "%s/%s", CGROUP_DIR, cgroup_name);
    if (mkdir(path, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "Failed to create cgroup directory %s: %m\n", path);
        exit(1);
    }

    // set memory limits
    snprintf(path, sizeof(path), "%s/%s/memory.max", CGROUP_DIR, cgroup_name);
    char mem_limit_str[20];
    snprintf(mem_limit_str, sizeof(mem_limit_str), "%zu", memory_limit);
    write_file(path, mem_limit_str);

    // set CPU max
    snprintf(path, sizeof(path), "%s/%s/cpu.max", CGROUP_DIR, cgroup_name);
    write_file(path, (char*)cpu_max);

    // add process to the cgroup
    snprintf(path, sizeof(path), "%s/%s/cgroup.procs", CGROUP_DIR, cgroup_name);
    char pid_str[20];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);
    write_file(path, pid_str);
}