# Quarantine

Quarantine is a minimalist container sandbox written in C that isolates a target process using Linux kernel primitives such as namespaces, cgroups, and `pivot_root`. It is designed to demonstrate how container runtimes like Docker work under the hood, without relying on third-party libraries or daemons.

## Features

- **Namespace Isolation**: Uses `clone()` with `CLONE_NEWUSER`, `CLONE_NEWNS`, `CLONE_NEWPID`, and `CLONE_NEWUTS` to create an isolated environment.
- **Filesystem Isolation**: Uses a static Alpine Linux `rootfs` and performs a secure `pivot_root` inside a mount namespace.
- **User Namespace Mapping**: Maps container UID/GID 0 to host UID/GID 1000, enabling root operations inside the sandbox without host privileges.
- **Resource Limits**: Enforces 50MB memory and 50% CPU limits using cgroups v2.
- **PID Namespace**: Process runs as PID 1 inside the container.
- **Secure Parent-Child Sync**: Synchronizes setup using a pipe and uses `PR_SET_PDEATHSIG` to kill the sandboxed process if the parent dies.

## Architecture

- The parent process:
  - Sets up user and mount namespaces.
  - Writes UID/GID maps via `/proc/[pid]/uid_map` and `/gid_map`.
  - Configures CPU and memory cgroups under `/sys/fs/cgroup/sandbox_group/`.
  - Signals the child to proceed after setup.

- The child process:
  - Performs `pivot_root()` into `rootfs/` and unmounts the host's old root.
  - Mounts `/proc` and executes the target command via `execvp()`.

## Usage

```bash
make
sudo ./sandbox /bin/sh
```
**Note**: Ensure rootfs/ contains a valid Alpine Linux root filesystem.

## Dependencies

Linux kernel with support for:

- User namespaces

- Cgroups v2 mounted at /sys/fs/cgroup

- C compiler (GCC)

- make

## Files

- sandbox.c: Main entry point, handles cloning and namespace setup.

- cgroups.c: Cgroup v2 creation and resource limiting.

- rootfs/: Alpine-based filesystem used as the container root.

- Makefile: Builds the sandbox binary.
