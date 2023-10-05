#include <asm/hypsec_shmem.h>
#include <asm/hypsec_host.h>
#include <asm/kvm_hyp.h>
#include <linux/kvm.h>

#include "hypsec.h"

void __hyp_text shmem_register(u64 base, u64 size) {
	struct el2_data *el2_data = get_el2_data_start();
	int i;

	print_string("[SeKVM] Registering Shared Memory\n");

	// get abs lock

	el2_data->shmem_base = base; 
	el2_data->shmem_size = size; 

	for (i = 0; i < size / PAGE_SIZE; i++) {	
		u64 page_base = base + i * PAGE_SIZE;
		assign_pfn_to_vm(SHHMEM, (u64)0, page_base/PAGE_SIZE);
	
	}
	
}

u64 __hyp_text handle_get_shmem_size() {
	struct el2_data *el2_data = get_el2_data_start();

	u64 shmem_size = el2_data->shmem_size;

	return shmem_size;      


}

extern void kvm_tlb_flush_vmid_ipa_host(phys_addr_t ipa);
void __hyp_text handle_guest_shmem_unregister(u32 vmid) {
	struct el2_data *el2_data;
	u64 shmem_base, shmem_size, guest_base;
	u64 addr_offset, shmem_base_pfn, num_pages;
	u32 pfn_count;
	struct el2_vm_info *vm_info;
	int i;

	// get el2 data lock 
	acquire_lock_core();
	el2_data = get_el2_data_start();

	// read physical base, size from el2_data
	shmem_base = el2_data->shmem_base;
	shmem_size = el2_data->shmem_size;

	// lock vm info lock
	acquire_lock_vm(vmid);

	// read guest_base from vm_info, then set it to 0
	vm_info = vmid_to_vm_info(vmid);
	guest_base = vm_info->shmem_guest_base_addr;
	
	vm_info->shmem_guest_base_addr = 0; 
	vm_info->shmem_guest_size = 0; 

	// unlock vm info lock
	release_lock_vm(vmid);

	num_pages = shmem_size / PAGE_SIZE;
	shmem_base_pfn = shmem_base / PAGE_SIZE;

	acquire_lock_s2page();
	// Iterate over each page
	for (i = 0; i < num_pages; i++) {
		// Offset from base 
		addr_offset = (i * PAGE_SIZE);

		// Set each mapping to zero and clear tlb
		map_pfn_vm(vmid, guest_base + addr_offset, 0UL, 3UL);
		kvm_tlb_flush_vmid_ipa_host(guest_base + addr_offset);

		// Increment page reference counter 
		pfn_count = get_pfn_count(shmem_base_pfn + i);
		set_pfn_count(shmem_base_pfn + i, pfn_count + 1);
	}

	release_lock_s2page();

	release_lock_core();

}

void __hyp_text handle_guest_shmem_register(u32 vmid, u64 guest_base) {
	struct el2_data *el2_data;
	u64 shmem_base, shmem_size;
	u64 addr_offset, shmem_base_pfn, num_pages;
	u32 pfn_count;
	struct el2_vm_info *vm_info;
	int i;
	char debug_out[500];

	print_string("[SeKVM] Guest registering for shared memory\n");
	snprintf(debug_out, 450, "[SeKVM] VMID: %u guest_base: %llu", vmid, guest_base);
	print_string(debug_out);
	// get el2 data lock 
	acquire_lock_core();
	el2_data = get_el2_data_start();

	// read physical base, size from el2_data
	shmem_base = el2_data->shmem_base;
	shmem_size = el2_data->shmem_size;

	snprintf(debug_out, 450, "[SeKVM] VMID: %u shmem_base: %llu", vmid, shmem_base);
	print_string(debug_out);
	snprintf(debug_out, 450, "[SeKVM] VMID: %u shmem_size: %llu", vmid, shmem_size);
	print_string(debug_out);
	// lock vm info lock
	acquire_lock_vm(vmid);

	// write IPA range to vm_info 
	vm_info = vmid_to_vm_info(vmid);
	vm_info->shmem_guest_base_addr = guest_base;
	vm_info->shmem_guest_size = shmem_size; // probably don't need this, will always be same as field in el2_data

	// unlock vm info lock
	release_lock_vm(vmid);

	num_pages = shmem_size / PAGE_SIZE;
	shmem_base_pfn = shmem_base / PAGE_SIZE;

	snprintf(debug_out, 450, "[SeKVM] VMID: %u num_pages: %llu, base_pfn: %llu", vmid, num_pages, shmem_base_pfn);
	print_string(debug_out);

	acquire_lock_s2page();
	// Iterate over each page
	for (i = 0; i < num_pages; i++) {
		// Offset from base 
		addr_offset = (i * PAGE_SIZE);

		// Map each page
		map_pfn_vm(vmid, guest_base + addr_offset, 0UL, 3UL);
		kvm_tlb_flush_vmid_ipa_host(guest_base + addr_offset);
		map_pfn_vm(vmid, guest_base + addr_offset, shmem_base + addr_offset, 3UL);
		kvm_tlb_flush_vmid_ipa_host(guest_base + addr_offset);

		// Increment page reference counter 
		pfn_count = get_pfn_count(shmem_base_pfn + i);
		set_pfn_count(shmem_base_pfn + i, pfn_count + 1);
		snprintf(debug_out, 450, "[SeKVM] VMID: %u guest page address: %llu, physical page address: %llu, pfn_count: %u", vmid, guest_base + addr_offset, shmem_base + addr_offset, pfn_count);
		print_string(debug_out);
	}

		// When to flush TLB?

	release_lock_s2page();

	release_lock_core();

	print_string("[SeKVM] Guest finished registering for shared memory\n");

}

