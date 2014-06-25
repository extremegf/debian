#ifndef FILECRYPT_H
#define FILECRYPT_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#include <stdint.h>
#endif

#include <linux/ioctl.h>

struct ext4_ioctl_encrypt {
	uint8_t key_id[16];
};
#define EXT4_ENCRYPT _IOR('e', 0xcf, struct ext4_ioctl_encrypt)

#define __NR_addkey 351

#endif
