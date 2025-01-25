mount --bind rootfs rootfs
cd rootfs
pivot_root . put_old
mkdir put_old
pivot_root . put_old
cd /
ls proc
ls put_old
unmount -l put_old
umount -l put_old
ls proc
pwd
ls
exit
