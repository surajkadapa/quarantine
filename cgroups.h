#ifndef CGROUPS_H
#define CGROUPS_H

#include <stddef.h>

void setup_cgroups(
    const char *cgroup_name, 
    int pid, 
    size_t memory_limit, 
    const char *cpu_max
);

void write_file(char *path, char *line);

#endif 