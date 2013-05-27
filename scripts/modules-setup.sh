#!/bin/sh -x
set e

uts=`uname -r`
modules_file=/etc/sysconfig/modules/dm-targets.modules
ko_path=/lib/modules/$uts/kernel/drivers/md/dm-mod.ko
initrd_path=/boot/initrd-2.6.32-220.23.1.tb750.el5.x86_64.img

# move dm-mod.ko to current kernel's driver path
rmmod dm_mod
if [ "$uts" == "2.6.32-131.6.1.tbay6.master.x86_64" ];then
	ko_file="dm-mod.ko.2.6.32-131.6.1.tbay6.master.x86_64"
else
	echo "dismatch kernel version"
	exit 1
fi

if [ -e $ko_path ];then
	mv $ko_path ./dm-mod.ko.bak
fi
cp -f ./dm-mod.ko $ko_path

# rebuild initrd
if [ -e $initrd_path ];then
	mv $initrd_path ./initrd.bak
fi
pushd /boot
mkinitrd --without-usb --force-lvm-probe /boot/initrd-2.6.32-131.6.1.tbay6.master.x86_64.img 2.6.32-131.6.1.tbay6.master.x86_64
popd

# fdisk lists device size in 1K blocks, get_target_size()
# return device size in 512bytes sectors.
get_target_size() {
	dev=$1
	block_size=`fdisk -l | grep "$dev" | sed "s/\*//g" | awk '{print $4}'`
	expr "$block_size" \* 2
}

rm -rf $modules_file
echo "#!/bin/bash" >> $modules_file
for dev in b c d e f g h i j k l;do
	size=`get_target_size sd"$dev"1`
	str=`printf "echo \"0 %s linear /dev/%s 0\" | dmsetup create %s" "$size" "sd""$dev""1" "sd""$dev""1"`
	echo $str
	echo "$str" >> $modules_file
done
# /etc/sysconfig/modules/*.modules will be executed
# before /etc/fstab mounted
chmod +x $modules_file


# setup /etc/fstab
cat /etc/fstab | sed "s/^LABEL=disk[0-9].*$//g" | sed "/^$/d" >.tmp_fstab
i=1
for dev in b c d e f g h i j k l;do
	str=`printf "/dev/mapper/%s /apsarapangu/disk%d ext4 defaults,noatime,nodiratime 0 0" sd"$dev"1 $i`
	echo $str
	echo $str>> .tmp_fstab
	i=`expr $i + 1`
done
mv /etc/fstab ./fstab.bak
mv .tmp_fstab /etc/fstab
