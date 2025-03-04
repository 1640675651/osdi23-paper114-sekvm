#ifndef __KVM_PVOPS_H__
#define __KVM_PVOPS_H__

extern u64 __kvm_call_hyp(void *hypfn, ...);
#define kvm_pvops(call_num, ...) __kvm_call_hyp(call_num, ##__VA_ARGS__)

#define KVM_SET_DESC_PFN 0x81000
#define KVM_UNSET_DESC_PFN 0x82000
#define KVM_SET_BALLOON_PFN 0x83000
#define HVC_GET_SHMEM_SIZE 0x84000
#define HVC_GUEST_SHMEM_REGISTER 0x85000
#define HVC_GUEST_SHMEM_UNREGISTER 0x86000

#endif /* __KVM_PVOPS_H__ */
