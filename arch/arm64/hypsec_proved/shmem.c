#include <asm/hypsec_shmem.h>
#include <asm/hypsec_host.h>
#include <asm/kvm_hyp.h>
#include <linux/kvm.h>

#include "hypsec.h"
#include "printf.h"

void __hyp_text print_num(u64 num)
{
	char num_str[21];
	u64 num_copy = num;
	int i=19;
	while(num_copy)
	{
		num_str[i] = num_copy % 10 + '0';
		num_copy /= 10;
		i--;
	}
	num_str[20] = 0;
	print_string(num_str+i+1);
}

void __hyp_text shmem_register(u64 base, u64 size) {
	struct el2_data *el2_data = get_el2_data_start();
	int i;

	printf("[SeKVM_EL2] Registering Shared Memory\n");
	printf("[SeKVM_EL2] Base host physical address = %#lx\n", base);

	if(base % SZ_2M != 0)
	{
		print_string("[SeKVM_EL2] Host memory base is not 2MB aligned. Aborting.\n");
		return;
	}
	if(size % SZ_2M != 0)
	{
		print_string("[SeKVM_EL2] Host memory size is not 2MB aligned, Aborting.\n");
		return;
	}

	// get abs lock

	el2_data->shmem_base = base; 
	el2_data->shmem_size = size; 

	for (i = 0; i < size / PAGE_SIZE; i++) {
		u64 page_base = base + i * PAGE_SIZE;
		//print_string("[SeKVM_EL2] Marking page starting at physical address ");
		//print_num(page_base);
		//print_string(" as shared\n");
		assign_pfn_to_vm(SHHMEM, (u64)0, page_base/PAGE_SIZE);
	}
	
}

u64 __hyp_text handle_get_shmem_size() {
	struct el2_data *el2_data = get_el2_data_start();

	u64 shmem_size = el2_data->shmem_size;

	return shmem_size;      


}

// XXX: Fix this for 2MB mappings
extern void __hyp_text __kvm_tlb_flush_vmid_ipa_shadow(phys_addr_t ipa);
void __hyp_text handle_guest_shmem_unregister(u32 vmid) {
	struct el2_data *el2_data;
	u64 shmem_base, shmem_size, guest_base;
	u64 addr_offset, shmem_base_pfn, num_pages;
	u32 pfn_count;
	struct el2_vm_info *vm_info;
	int i, j;

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

	num_pages = shmem_size / SZ_2M;
	shmem_base_pfn = shmem_base / SZ_2M;

	acquire_lock_s2page();
	// Iterate over each page
	for (i = 0; i < num_pages; i++) {
		// Offset from base 
		addr_offset = (i * SZ_2M);

		// Set each mapping to zero and clear tlb
		map_pfn_vm(vmid, guest_base + addr_offset, 0UL, 2UL);
		__kvm_tlb_flush_vmid_ipa_shadow(guest_base);

		// Decrement page reference counter 
		for(j = 0;j < SZ_2M/PAGE_SIZE;j++)
		{
			pfn_count = get_pfn_count(shmem_base_pfn + (i*SZ_2M + j*PAGE_SIZE)/PAGE_SIZE);
			set_pfn_count(shmem_base_pfn + (i*SZ_2M + j*PAGE_SIZE)/PAGE_SIZE, pfn_count - 1);
		}
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
	int i, j;
	//char debug_out[100];

	print_string("[SeKVM_EL2] Guest registering for shared memory\n");
	//snprintf(debug_out, 450, "[SeKVM] VMID: %u guest_base: %llu\n", vmid, guest_base);

	/* print_string("[SeKVM] VMID: "); */
	/* print_num(vmid); */
	/* print_string(" guest_base: "); */
	/* print_num(guest_base); */
	/* print_string("\n"); */

	printf("[SeKVM_EL2] VMID %d, guest_base: %#lx\n", vmid, guest_base);

	// XXX: properly check this
	if (guest_base % SZ_2M) {
		printf("[SeKVM_EL2] Guest provided base unaligned to 2MB\n");
		return;
	}

	// get el2 data lock 
	acquire_lock_core();
	el2_data = get_el2_data_start();

	// read physical base, size from el2_data
	shmem_base = el2_data->shmem_base;
	shmem_size = el2_data->shmem_size;

	// lock vm info lock
	acquire_lock_vm(vmid);

	// write IPA range to vm_info 
	vm_info = vmid_to_vm_info(vmid);
	vm_info->shmem_guest_base_addr = guest_base;
	vm_info->shmem_guest_size = shmem_size; // probably don't need this, will always be same as field in el2_data

	// unlock vm info lock
	release_lock_vm(vmid);

	num_pages = shmem_size / SZ_2M;
	shmem_base_pfn = shmem_base / SZ_2M;

	acquire_lock_s2page();
	// Iterate over each page
	for (i = 0; i < num_pages; i++) {
		// Offset from base 
		addr_offset = (i * SZ_2M);

		// Map each page
		map_pfn_vm(vmid, guest_base + addr_offset, 0UL, 2UL);
		__kvm_tlb_flush_vmid_ipa_shadow(guest_base);
		printf("[SeKVM_EL2] map IPA %#lx to PA %#lx\n", guest_base + addr_offset, shmem_base + addr_offset);
		map_pfn_vm(vmid, guest_base + addr_offset, shmem_base + addr_offset, 2UL);
		__kvm_tlb_flush_vmid_ipa_shadow(guest_base);

		// XXX: Fix this for every 4KB Page
		// Increment page reference counter
		for(j = 0;j < SZ_2M/PAGE_SIZE;j++)
		{
			pfn_count = get_pfn_count(shmem_base_pfn + (i*SZ_2M + j*PAGE_SIZE)/PAGE_SIZE);
			set_pfn_count(shmem_base_pfn + (i*SZ_2M + j*PAGE_SIZE)/PAGE_SIZE, pfn_count + 1);
		}

	}

		// When to flush TLB?

	//printf("[SeKVM] before memset %#lx\n", *(unsigned long *)__el2_va(shmem_base));
	//el2_memset(__el2_va(shmem_base), -1, SZ_2M);
	//printf("[SeKVM] after memset %#lx\n", *(unsigned long *)__el2_va(shmem_base));
	/* u64 val = pt_load(vmid, shmem_base); */
	/* print_string("[SeKVM] VMID: "); */
	/* print_num(vmid); */
	/* print_string(" first u64 = "); */
	/* print_num(val); */
	/* print_string("\n"); */
	
	release_lock_s2page();

	release_lock_core();
	
	//snprintf(debug_out, 450, "[SeKVM] VMID: %u shmem_base: %llu\n", vmid, shmem_base);
	print_string("[SeKVM_EL2] VMID: ");
	print_num(vmid);
	print_string(" shmem_base: ");
	print_num(shmem_base);
	print_string("\n");

	//snprintf(debug_out, 450, "[SeKVM] VMID: %u shmem_size: %llu\n", vmid, shmem_size);
	print_string("[SeKVM_EL2] VMID: ");
	print_num(vmid);
	print_string(" shmem_size: ");
	print_num(shmem_size);
	print_string("\n");

	//snprintf(debug_out, 450, "[SeKVM] VMID: %u num_pages: %llu, base_pfn: %llu\n", vmid, num_pages, shmem_base_pfn);
	print_string("[SeKVM_EL2] VMID: ");
	print_num(vmid);
	print_string(" num_pages: ");
	print_num(num_pages);
	print_string(" base_pfn ");
	print_num(shmem_base_pfn);
	print_string("\n");

	//snprintf(debug_out, 450, "[SeKVM] (for last page) VMID: %u guest page address: %llu, physical page address: %llu, pfn_count: %u", vmid, guest_base + addr_offset, shmem_base + addr_offset, pfn_count);
	print_string("[SeKVM_EL2] (for last page) VMID: ");
	print_num(vmid);
	print_string(" guest page address: ");
	print_num(guest_base + addr_offset);
	print_string(" physical page address: ");
	print_num(shmem_base + addr_offset);
	print_string(" pfn_count: ");
	print_num(pfn_count);
	print_string("\n");

	print_string("[SeKVM_EL2] Guest finished registering for shared memory\n");

}

