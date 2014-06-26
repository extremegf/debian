git pull
make -j5 SUBDIRS=fs/ext4 modules
cp /root/linux-3.13.3/fs/ext4/ext4.ko /lib/modules/3.13.3+/extra/ext4.ko
cp /root/linux-3.13.3/fs/ext4/ext4.ko /lib/modules/3.13.3+/extra/ext4/ext4.ko
cp /root/linux-3.13.3/fs/ext4/ext4.ko /lib/modules/3.13.3+/kernel/fs/ext4/ext4.ko
/etc/kernel/postinst.d/initramfs-tools 3.13.3+ /boot/vmlinuz-3.13.3+
