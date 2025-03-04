#ifndef __ARM_STAGE2_H__
#define __ARM_STAGE2_H__
#include <linux/memblock.h>
#include <linux/kvm_host.h>
#include <linux/hashtable.h>
#include <asm/hypsec_boot.h>
#include <asm/hypsec_mmu.h>
#include <asm/hypsec_vcpu.h>
#include <asm/hypsec_mmio.h>
#include <asm/kvm_mmu.h>
#include <asm/hypsec_constant.h>

/* Handler for ACTLR_EL1 is not defined */
#define SHADOW_SYS_REGS_SIZE		(DISR_EL1)
#define SHADOW_32BIT_REGS_SIZE		3
#define SHADOW_SYS_REGS_DESC_SIZE	(SHADOW_SYS_REGS_SIZE + SHADOW_32BIT_REGS_SIZE)
#define NUM_SHADOW_VCPU_CTXT		(EL2_MAX_VMID * HYPSEC_MAX_VCPUS)

#define VCPU_IDX(vmid, vcpu_id) \
	(vmid * HYPSEC_MAX_VCPUS) + vcpu_id

struct shared_data {
	struct kvm kvm_pool[EL2_MAX_VMID];
	struct kvm_vcpu vcpu_pool[EL2_MAX_VMID * HYPSEC_MAX_VCPUS];
};

struct el2_per_cpu_data {
	int vmid;
	int vcpu_id;
	struct kvm_cpu_context core_ctxt;
	struct s2_host_regs *host_regs;
};

typedef struct b_arch_spinlock_t b_arch_spinlock_t;
struct b_arch_spinlock_t {
	volatile unsigned int lock;
};

enum hypsec_init_state {
	INVALID = 0,
	MAPPED,
	READY,
	VERIFIED,
	ACTIVE
};

struct el2_load_info {
	unsigned long load_addr;
	unsigned long size;
	unsigned long el2_remap_addr;
	int el2_mapped_pages;
	uint8_t signature[64];
};

struct int_vcpu {
	struct kvm_vcpu *vcpu;
	int vcpu_pg_cnt;
	enum hypsec_init_state state;
	u32 ctxtid;
};

struct el2_vm_info {
	u64 vttbr;
	int vmid;
	int load_info_cnt;
	int kvm_pg_cnt;
	bool inc_exe;
	enum hypsec_init_state state;
	struct el2_load_info load_info[HYPSEC_MAX_LOAD_IMG];
	b_arch_spinlock_t shadow_pt_lock;
	b_arch_spinlock_t vm_lock;
	struct kvm *kvm;
	struct int_vcpu int_vcpus[HYPSEC_MAX_VCPUS];
	struct shadow_vcpu_context *shadow_ctxt[HYPSEC_MAX_VCPUS];
	uint8_t key[16];
	uint8_t iv[16];
	uint8_t public_key[32];
	bool powered_on;

	/* For shmem */
	u64 shmem_guest_base_addr;
	u64 shmem_guest_size;

	/* For VM private pool */
	u64 page_pool_start;
	unsigned long used_pages;
	unsigned long pmd_used_pages;
	unsigned long pud_used_pages;
	unsigned long pte_used_pages;
};

struct el2_data {
	struct memblock_region regions[32];
	struct s2_memblock_info s2_memblock_info[32];
	struct s2_cpu_arch arch;

	int regions_cnt;
	u64 page_pool_start;
	phys_addr_t host_vttbr;

	u64 shmem_base;
	u64 shmem_size;

	unsigned long used_pages;
	unsigned long used_tmp_pages;
	unsigned long pl011_base;
	unsigned long uart_8250_base;

	b_arch_spinlock_t s2pages_lock;
	b_arch_spinlock_t abs_lock;
	b_arch_spinlock_t el2_pt_lock;
	b_arch_spinlock_t console_lock;
	b_arch_spinlock_t smmu_lock;
	b_arch_spinlock_t spt_lock;

	kvm_pfn_t ram_start_pfn;
	struct s2_page s2_pages[SZ_2M];

	struct shadow_vcpu_context shadow_vcpu_ctxt[NUM_SHADOW_VCPU_CTXT];
	int used_shadow_vcpu_ctxt;

	struct s2_sys_reg_desc s2_sys_reg_descs[SHADOW_SYS_REGS_DESC_SIZE];

	struct el2_vm_info vm_info[EL2_VM_INFO_SIZE];
	int used_vm_info;
	unsigned long last_remap_ptr;

	struct el2_smmu_cfg smmu_cfg[EL2_SMMU_CFG_SIZE];
	struct el2_arm_smmu_device smmus[SMMU_NUM];
	int el2_smmu_num;

	u32 next_vmid;
	phys_addr_t vgic_cpu_base;
	bool installed;

	struct el2_per_cpu_data per_cpu_data[HYPSEC_MAX_CPUS];

	unsigned long core_start, core_end;

	uint64_t hacl_hash[80U];
        uint32_t hacl_hash0[64U];

	uint8_t key[16];

	unsigned long smmu_page_pool_start;
	unsigned long smmu_pgd_used_pages;
	unsigned long smmu_pmd_used_pages;

	u64 phys_mem_start;
	u64 phys_mem_size;
};

void init_el2_data_page(void);

static inline void _arch_spin_lock(b_arch_spinlock_t *lock)
{
	unsigned int tmp;

	asm volatile(
	"	sevl\n"
	"1:	wfe\n"
	"2:	ldaxr	%w0, %1\n"
	"	cbnz	%w0, 1b\n"
	"	stxr	%w0, %w2, %1\n"
	"	cbnz	%w0, 2b\n"
	: "=&r" (tmp), "+Q" (lock->lock)
	: "r" (1)
	: "cc", "memory");
}

static inline void _arch_spin_unlock(b_arch_spinlock_t *lock)
{
	asm volatile(
	"	stlr	%w1, %0\n"
	: "=Q" (lock->lock) : "r" (0) : "memory");
}

static inline void stage2_spin_lock(b_arch_spinlock_t *lock)
{	
	_arch_spin_lock(lock);
}

static inline void stage2_spin_unlock(b_arch_spinlock_t *lock)
{
	_arch_spin_unlock(lock);
}

static inline void el2_init_vgic_cpu_base(phys_addr_t base)
{
	struct el2_data *el2_data = (void *)kvm_ksym_ref(el2_data_start);
	el2_data->vgic_cpu_base = base;
}

static inline struct el2_data* get_el2_data_start(void) 
{
	struct el2_data *el2_data = kern_hyp_va(kvm_ksym_ref(el2_data_start));
	return el2_data;
}

static inline struct shared_data* get_shared_data_start(void) 
{
	struct shared_data *shared_data = kern_hyp_va(kvm_ksym_ref(shared_data_start));
	return shared_data;
}

static inline int get_cpuid(void)
{
	return read_cpuid_mpidr() & MPIDR_HWID_BITMASK;
}

extern void __noreturn __hyp_panic(void);

extern void printhex_ul(unsigned long input);
extern void print_string(char *input);

extern void stage2_inject_el1_fault(unsigned long addr);
void el2_memset(void *b, int c, int len);
void el2_memcpy(void *dest, void *src, size_t len);
int el2_memcmp(void *dest, void *src, size_t len);

int el2_hex_to_bin(char ch);
int el2_hex2bin(unsigned char *dst, const char *src, int count);

extern void el2_smmu_alloc_pgd(u32 cbndx, u32 vmid, u32 num);
extern void el2_smmu_free_pgd(u32 cbndx, u32 num);
extern void el2_arm_lpae_map(u64 iova, phys_addr_t paddr, u64 prot, u32 cbndx, u32 num);
extern phys_addr_t el2_arm_lpae_iova_to_phys(u64 iova, u32 cbndx, u32 num);
extern void el2_smmu_clear(u64 iova, u32 cbndx, u32 num);
extern void el2_kvm_phys_addr_ioremap(u32 vmid, u64 gpa, u64 pa, u64 size);

void encrypt_buf(u32 vmid, void *buf, uint32_t len);
void decrypt_buf(u32 vmid, void *buf, uint32_t len);

extern void el2_boot_from_inc_exe(u32 vmid);
extern bool el2_use_inc_exe(u32 vmid);
extern unsigned long search_load_info(u32 vmid, struct el2_data *el2_data,
				      unsigned long addr);

extern int el2_alloc_vm_info(struct kvm *kvm);

int handle_pvops(u32 vmid, u32 vcpuid);
void save_encrypted_vcpu(struct kvm_vcpu *vcpu);

//extern void set_pfn_owner(struct el2_data *el2_data, phys_addr_t addr,
//				unsigned long pgnum, u32 vmid);

extern phys_addr_t host_alloc_stage2_page(unsigned int num);
extern phys_addr_t host_alloc_pgd(unsigned int num);
extern phys_addr_t host_alloc_pud(unsigned int num);
extern phys_addr_t host_alloc_pmd(unsigned int num);
extern phys_addr_t host_alloc_pte(unsigned int num);
extern void init_hypsec_io(void);

/* VM Bootstrap */
extern int hypsec_register_kvm(void);
extern int hypsec_register_vcpu(u32 vmid, int vcpu_id);

extern u32 __hypsec_register_kvm(void);
extern int __hypsec_register_vcpu(u32 vmid, int vcpu_id);

struct el2_vm_info* vmid_to_vm_info(u32 vmid);
struct int_vcpu* vcpu_id_to_int_vcpu(struct el2_vm_info *vm_info, int vcpu_id);

extern void map_vgic_cpu_to_shadow_s2pt(u32 vmid, struct el2_data *el2_data);

extern struct kvm* hypsec_alloc_vm(u32 vmid);
extern struct kvm_vcpu* hypsec_alloc_vcpu(u32 vmid, int vcpu_id);

static void inline set_per_cpu(int vmid, int vcpu_id)
{
	struct el2_data *el2_data = get_el2_data_start();
	int pcpuid = get_cpuid();
	el2_data->per_cpu_data[pcpuid].vmid = vmid;
	el2_data->per_cpu_data[pcpuid].vcpu_id = vcpu_id;
};

//int get_cur_vmid(void);
//int get_cur_vcpu_id(void);
static int inline get_cur_vmid(void)
{
	struct el2_data *el2_data = get_el2_data_start();
	int pcpuid = get_cpuid();
	return el2_data->per_cpu_data[pcpuid].vmid;
};

static int inline get_cur_vcpu_id(void)
{
	struct el2_data *el2_data = get_el2_data_start();
	int pcpuid = get_cpuid();
	return el2_data->per_cpu_data[pcpuid].vcpu_id;
};


static u64 inline get_shadow_ctxt(u32 vmid, u32 vcpuid, u32 index)
{
	struct el2_data *el2_data = get_el2_data_start();
	int offset = VCPU_IDX(vmid, vcpuid);
	u64 val;
	if (index < V_FAR_EL2)
		val = el2_data->shadow_vcpu_ctxt[offset].regs[index]; 
	else if (index == V_FAR_EL2)
		val = el2_data->shadow_vcpu_ctxt[offset].far_el2;
	else if (index == V_HPFAR_EL2)
		val = el2_data->shadow_vcpu_ctxt[offset].hpfar;
	else if (index == V_HCR_EL2)
		val = el2_data->shadow_vcpu_ctxt[offset].hcr_el2;
	else if (index == V_EC)
		val = el2_data->shadow_vcpu_ctxt[offset].ec;
	else if (index == V_DIRTY)
		val = el2_data->shadow_vcpu_ctxt[offset].dirty;
	else if (index == V_FLAGS)
		val = el2_data->shadow_vcpu_ctxt[offset].flags;
	else if (index >= SYSREGS_START) {
		index -= SYSREGS_START;
		val = el2_data->shadow_vcpu_ctxt[offset].sys_regs[index];
	} else {
		print_string("\rinvalid get shadow ctxt\n");
		val = INVALID64;
	}

	return val;
};

//TODO: Define the following
static void inline set_shadow_ctxt(u32 vmid, u32 vcpuid, u32 index, u64 value) {
	struct el2_data *el2_data = get_el2_data_start();
	int offset = VCPU_IDX(vmid, vcpuid);
	//el2_data->shadow_vcpu_ctxt[offset].regs[index] = value;
	if (index < V_FAR_EL2)
		el2_data->shadow_vcpu_ctxt[offset].regs[index] = value; 
	else if (index == V_FAR_EL2)
		el2_data->shadow_vcpu_ctxt[offset].far_el2 = value;
	else if (index == V_HPFAR_EL2)
		el2_data->shadow_vcpu_ctxt[offset].hpfar = value;
	else if (index == V_HCR_EL2)
		el2_data->shadow_vcpu_ctxt[offset].hcr_el2 = value;
	else if (index == V_EC)
		el2_data->shadow_vcpu_ctxt[offset].ec = value;
	else if (index == V_DIRTY)
		el2_data->shadow_vcpu_ctxt[offset].dirty = value;
	else if (index == V_FLAGS)
		el2_data->shadow_vcpu_ctxt[offset].flags = value;
	else if (index >= SYSREGS_START) {
		index -= SYSREGS_START;
		el2_data->shadow_vcpu_ctxt[offset].sys_regs[index] = value;
	} else
		print_string("\rinvalid set shadow ctxt\n");
}

static u32 inline get_shadow_ctxt_esr(u32 vmid, u32 vcpuid)
{
	struct el2_data *el2_data = get_el2_data_start();
	int offset = VCPU_IDX(vmid, vcpuid);
	return el2_data->shadow_vcpu_ctxt[offset].esr;
};

static void inline set_shadow_ctxt_esr(u32 vmid, u32 vcpuid, u32 value) 
{
	struct el2_data *el2_data = get_el2_data_start();
	int offset = VCPU_IDX(vmid, vcpuid);
	el2_data->shadow_vcpu_ctxt[offset].esr = value;
}

void save_shadow_kvm_regs(void);
void restore_shadow_kvm_regs(void);

void __vm_sysreg_restore_state_nvhe(u32 vmid, u32 vcpuid);
void __vm_sysreg_save_state_nvhe(u32 vmid, u32 vcpuid);

void __vm_sysreg_restore_state_nvhe_opt(u32 vmid, u32 vcpuid);
void __vm_sysreg_save_state_nvhe_opt(u32 vmid, u32 vcpuid);

void v_grant_stage2_sg_gpa(u32 vmid, u64 addr, u64 size);
void v_revoke_stage2_sg_gpa(u32 vmid, u64 addr, u64 size);

void init_hacl_hash(struct el2_data *el2_data);
uint64_t get_hacl_hash_sha2_constant_k384_512(int i);
uint32_t get_hacl_hash_sha2_constant_k224_256(int i);

static u64 inline get_pt_vttbr(u32 vmid)
{
	struct el2_data *el2_data = get_el2_data_start();
	if (vmid < COREVISOR) {
		return el2_data->vm_info[vmid].vttbr;
	} else {
		return read_sysreg(ttbr0_el2);
	}
}

static void inline set_pt_vttbr(u32 vmid, u64 vttbr) {
	struct el2_data *el2_data = get_el2_data_start();
	el2_data->vm_info[vmid].vttbr = vttbr;
};

static void inline set_host_running_vcpu(u32 vmid, u32 vcpuid) {
	struct shared_data *shared_data;
	int offset = VCPU_IDX(vmid, vcpuid);
	struct kvm_vcpu *vcpu;
	shared_data = get_shared_data_start();
	vcpu = &shared_data->vcpu_pool[offset];
	kern_hyp_va(vcpu->arch.host_cpu_context)->__hyp_running_vcpu = vcpu;
}

static inline struct kvm_cpu_context* get_vcpu_host_cpu_context(u32 vmid, u32 vcpuid) {
	struct shared_data *shared_data;
	int offset = VCPU_IDX(vmid, vcpuid);
	struct kvm_vcpu *vcpu;
	shared_data = get_shared_data_start();
	vcpu = &shared_data->vcpu_pool[offset];
	return kern_hyp_va(vcpu->arch.host_cpu_context);
}

static inline bool get_vcpu_was_preempted(u32 vmid, u32 vcpuid) {
	struct shared_data *shared_data;
	int offset = VCPU_IDX(vmid, vcpuid);
	struct kvm_vcpu *vcpu;
	shared_data = get_shared_data_start();
	vcpu = &shared_data->vcpu_pool[offset];
	return vcpu->arch.was_preempted;
}

static inline void set_vcpu_was_preempted(u32 vmid, u32 vcpuid, bool preempted) {
	struct shared_data *shared_data;
	int offset = VCPU_IDX(vmid, vcpuid);
	struct kvm_vcpu *vcpu;
	shared_data = get_shared_data_start();
	vcpu = &shared_data->vcpu_pool[offset];
	vcpu->arch.was_preempted = preempted;
}

static inline void set_vcpu_esr_el2(u32 vmid, u32 vcpuid, u32 esr_el2) {
	struct shared_data *shared_data;
	int offset = VCPU_IDX(vmid, vcpuid);
	struct kvm_vcpu *vcpu;
	shared_data = get_shared_data_start();
	vcpu = &shared_data->vcpu_pool[offset];
	vcpu->arch.fault.esr_el2 = esr_el2;
}

static inline phys_addr_t get_host_vttbr(void)
{
	struct el2_data *el2_data = get_el2_data_start();
	return el2_data->host_vttbr;
};

static inline struct kvm_cpu_context* get_core_context(void)
{
	struct el2_data *el2_data = get_el2_data_start();
	int pcpuid = get_cpuid();
	return &el2_data->per_cpu_data[pcpuid].core_ctxt;
};

#endif /* __ARM_STAGE2_H__ */
