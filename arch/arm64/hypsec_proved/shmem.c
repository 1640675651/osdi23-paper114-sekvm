#include <asm/hypsec_shmem.h>
#include <asm/hypsec_host.h>
#include <asm/kvm_hyp.h>
#include <linux/kvm.h>

#include "hypsec.h"

void __hyp_text shmem_register(u64 base, u64 size) {
	struct el2_data *el2_data = get_el2_data_start();
	int i;

	print_string("[SeKVM] Registering Shared Memory\n");

	el2_data->shmem_base = base; 
	el2_data->shmem_size = size; 

	for (i = 0; i < size / PAGE_SIZE; i++) {	
		u64 page_base = base + i * PAGE_SIZE;
		assign_pfn_to_vm(SHHMEM, (u64)i, page_base/PAGE_SIZE);
	
	}
	
}

u64 __hyp_text handle_get_shmem_size(u32 vmid, u64 addr, u64 size) {
        struct el2_data *el2_data = get_el2_data_start();

        u64 shmem_size = el2_data->shmem_size;

        return shmem_size;      


}
