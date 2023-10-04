#ifndef HYPSEC_SHMEM_H
#define HYPSEC_SHMEM_H

#include <linux/types.h>    

#define SHHMEM (EL2_MAX_VMID + 2)

extern void shmem_register(u64 base, u64 size);
extern u64 handle_get_shmem_size(void);
extern void handle_guest_shmem_register(u32 vmid, u64 guest_base);
extern void handle_guest_shmem_unregister(u32 vmid);

#endif //HYPSEC_SHMEM_H
