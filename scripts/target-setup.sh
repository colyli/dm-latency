#!/bin/sh -x

# fdisk lists device size in 1K blocks, get_target_size()
# return device size in 512bytes sectors.
get_target_size() {
	dev=$1
	block_size=`fdisk -l | grep "$dev" | sed "s/\*//g" | awk '{print $4}'`
	expr "$block_size" \* 2
}

set +e
for dev in b1 c1 d1 e1 f1 g1 h1 i1 j1 k1 l1;do
	umount /dev/sd$dev
done
set -e

modprobe dm-mod
for dev in b1 c1 d1 e1 f1 g1 h1 i1 j1 k1 l1;do
	size=`get_target_size sd$dev`
	echo "0 $size linear /dev/sd$dev 0" | dmsetup create sd"$dev"
done

# /etc/fstab is updated in modules_setup.sh, we can mount all partitions
# just with -a option.
mount -a
