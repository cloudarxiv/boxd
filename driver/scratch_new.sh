#! /bin/bash
die() {
    echo -e "\e[1;31mERROR: $1\e[0m" >&2
    exit 1
}

check_prereq() {
    [ "$EUID" -ne "0" ] && die "This script needs root permissions"
}

check_prereq
make clean
sudo rmdir /sys/fs/cgroup/someslice.slice
sudo modprobe -r nvidia_uvm
sudo rm /lib/modules/6.11.11+/kernel/drivers/video/nvidia-uvm.ko 
make modules -j$(nproc)
sudo make modules_install -j$(nproc)   
sudo dmesg -C
sudo modprobe nvidia_uvm
