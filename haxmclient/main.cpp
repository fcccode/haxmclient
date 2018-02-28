#include <cstdio>

#include "haxm.h"

void printRegs(struct vcpu_state_t *regs) {
	printf("EAX = %08x   EBX = %08x   ECX = %08x   EDX = %08x   ESI = %08x   EDI = %08x  EFER = %08x\n", regs->_eax, regs->_ebx, regs->_ecx, regs->_edx, regs->_esi, regs->_edi, regs->_efer);
	printf("CR0 = %08x   CR2 = %08x   CR3 = %08x   CR4 = %08x   ESP = %08x   EBP = %08x   GDT = %08x:%04x\n", regs->_cr0, regs->_cr2, regs->_cr3, regs->_cr4, regs->_esp, regs->_ebp, regs->_gdt.base, regs->_gdt.limit);
	printf("DR0 = %08x   DR1 = %08x   DR2 = %08x   DR3 = %08x   DR6 = %08x   DR7 = %08x   IDT = %08x:%04x\n", regs->_dr0, regs->_dr1, regs->_dr2, regs->_dr3, regs->_dr6, regs->_dr7, regs->_idt.base, regs->_idt.limit);
	printf(" CS = %04x   DS = %04x   ES = %04x   FS = %04x   GS = %04x   SS = %04x   TR = %04x   PDE = %08x   LDT = %08x:%04x\n", regs->_cs.selector, regs->_ds.selector, regs->_es.selector, regs->_fs.selector, regs->_gs.selector, regs->_ss.selector, regs->_tr.selector, regs->_pde, regs->_ldt.base, regs->_ldt.limit);
	printf("EIP = %08x   EFLAGS = %08x\n", regs->_eip, regs->_eflags);
}

void printFPURegs(struct fx_layout *fpu) {
	printf("FCW =     %04x   FSW =     %04x   FTW =       %02x   FOP =     %04x   MXCSR = %08x\n", fpu->fcw, fpu->fsw, fpu->ftw, fpu->fop, fpu->mxcsr);
	printf("FIP = %08x   FCS =     %04x   FDP = %08x   FDS = %08x    mask = %08x\n", fpu->fip, fpu->fcs, fpu->fdp, fpu->fds, fpu->mxcsr_mask);
	for (int i = 0; i < 8; i++) {
		printf("  ST%d = %010x   %+.20Le\n", i, *(uint64_t *)&fpu->st_mm[i], *(long double *)&fpu->st_mm[i]);
	}
	for (int i = 0; i < 8; i++) {
		printf(" XMM%d = %08x %08x %08x %08x     %+.7e %+.7e %+.7e %+.7e     %+.15le %+.15le\n", i,
			*(uint32_t *)&fpu->mmx_1[i][0], *(uint32_t *)&fpu->mmx_1[i][4], *(uint32_t *)&fpu->mmx_1[i][8], *(uint32_t *)&fpu->mmx_1[i][12],
			*(float *)&fpu->mmx_1[i][0], *(float *)&fpu->mmx_1[i][4], *(float *)&fpu->mmx_1[i][8], *(float *)&fpu->mmx_1[i][12],
			*(double *)&fpu->mmx_1[i][0],  *(double *)&fpu->mmx_1[i][8]
		);
	}
}

//#define DO_MANUAL_INIT
//#define DO_MANUAL_JMP
//#define DO_MANUAL_PAGING

int main() {
	// Allocate memory for the RAM and ROM
	const uint32_t ramSize = 256 * 4096; // 1 MiB
	const uint32_t ramBase = 0x00000000;
	const uint32_t romSize = 16 * 4096; // 64 KiB
	const uint32_t romBase = 0xFFFF0000;

	char *ram;
	ram = (char *)_aligned_malloc(ramSize, 0x1000);
	memset(ram, 0, ramSize);

	char *rom;
	rom = (char *)_aligned_malloc(romSize, 0x1000);
	memset(rom, 0xf4, romSize);

	// Write initialization code to ROM and a simple program to RAM
	{
		uint32_t addr;
		#define emit(buf, code) {memcpy(&buf[addr], code, sizeof(code) - 1); addr += sizeof(code) - 1;}
		
		// --- Start of ROM code ----------------------------------------------------------------------------------------------

		// --- 16-bit real mode -----------------------------------------------------------------------------------------------

		// Jump to initialization code and define GDT/IDT table pointer
		addr = 0xfff0;
		#ifdef DO_MANUAL_INIT
		emit(rom, "\xf4");                             // [0xfff0] hlt
		emit(rom, "\x90");                             // [0xfff1] nop
		#else
		emit(rom, "\xeb\xc6");                         // [0xfff0] jmp    short 0x1b8
		#endif
		emit(rom, "\x18\x00\x00\x00\xff\xff");         // [0xfff2] GDT pointer: 0xffff0000:0x0018
		emit(rom, "\x10\x01\x18\x00\xff\xff");         // [0xfff8] IDT pointer: 0xffff0018:0x0110
		// There's room for two bytes at the end, so let's fill it up with HLTs
		emit(rom, "\xf4");                             // [0xfffe] hlt
		emit(rom, "\xf4");                             // [0xffff] hlt
		
		// GDT table
		addr = 0x0000;
		emit(rom, "\x00\x00\x00\x00\x00\x00\x00\x00"); // [0x0000] GDT entry 0: null
		emit(rom, "\xff\xff\x00\x00\x00\x9b\xcf\x00"); // [0x0008] GDT entry 1: code (full access to 4 GB linear space)
		emit(rom, "\xff\xff\x00\x00\x00\x93\xcf\x00"); // [0x0010] GDT entry 2: data (full access to 4 GB linear space)

		// IDT table
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0018] Vector 0x00: Divide by zero
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0020] Vector 0x01: Reserved
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0028] Vector 0x02: Non-maskable interrupt
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0030] Vector 0x03: Breakpoint (INT3)
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0038] Vector 0x04: Overflow (INTO)
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0040] Vector 0x05: Bounds range exceeded (BOUND)
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0048] Vector 0x06: Invalid opcode (UD2)
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0050] Vector 0x07: Device not available (WAIT/FWAIT)
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0058] Vector 0x08: Double fault
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0060] Vector 0x09: Coprocessor segment overrun
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0068] Vector 0x0A: Invalid TSS
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0070] Vector 0x0B: Segment not present
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0078] Vector 0x0C: Stack-segment fault
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0080] Vector 0x0D: General protection fault
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0088] Vector 0x0E: Page fault
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0090] Vector 0x0F: Reserved
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x0098] Vector 0x10: x87 FPU error
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x00a0] Vector 0x11: Alignment check
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x00a8] Vector 0x12: Machine check
		emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x00b0] Vector 0x13: SIMD Floating-Point Exception
		for (uint8_t i = 0x14; i <= 0x1f; i++) {
			emit(rom, "\x03\x10\x08\x00\x00\x8f\x00\x10"); // [0x00b8..0x0110] Vector 0x14..0x1F: Reserved
		}
		
		// User defined IDTs
		emit(rom, "\x00\x10\x08\x00\x00\x8f\x00\x10"); // [0x0118] Vector 0x20: Just IRET
		emit(rom, "\x01\x10\x08\x00\x00\x8f\x00\x10"); // [0x0120] Vector 0x21: HLT, then IRET

		// Load GDT and IDT tables
		addr = 0xffb8;
		emit(rom, "\x66\x2e\x0f\x01\x16\xf2\xff");     // [0xffb8] lgdt   [cs:0xfff2]
		emit(rom, "\x66\x2e\x0f\x01\x1e\xf8\xff");     // [0xffbf] lidt   [cs:0xfff8]

		// Enter protected mode
		emit(rom, "\x0f\x20\xc0");                     // [0xffc6] mov    eax, cr0
		emit(rom, "\x0c\x01");                         // [0xffc9] or      al, 1
		emit(rom, "\x0f\x22\xc0");                     // [0xffcb] mov    cr0, eax
		#ifdef DO_MANUAL_JMP
		emit(rom, "\xf4")                              // [0xffce] hlt
		#else
		emit(rom, "\x66\xea\x00\xff\xff\xff\x08\x00"); // [0xffce] jmp    dword 0x8:0xffffff00
		#endif

		// --- 32-bit protected mode ------------------------------------------------------------------------------------------
		
		// Prepare memory for paging
		// (based on https://github.com/unicorn-engine/unicorn/blob/master/tests/unit/test_x86_soft_paging.c)
		// 0x1000 = Page directory
		// 0x2000 = Page table (identity map RAM)
		// 0x3000 = Page table (identity map ROM)
		// 0x4000 = Page table (0x10000xxx -> 0x00005xxx, 0x10001xxx -> 0x00006xxx)
		// 0x5000 = Data area (first dword reads 0xdeadbeef)
		// 0x6000 = Interrupt handler code area

		// Load segment registers
		addr = 0xff00;
		#ifdef DO_MANUAL_PAGING
		emit(rom, "\xf4");                             // [0xff00] hlt
		emit(rom, "\x90");                             // [0xff01] nop
		#else
		emit(rom, "\x33\xc0");                         // [0xff00] xor    eax, eax
		#endif
		emit(rom, "\xb0\x10");                         // [0xff02] mov     al, 0x10
		emit(rom, "\x8e\xd8");                         // [0xff04] mov     ds, eax
		emit(rom, "\x8e\xc0");                         // [0xff06] mov     es, eax
		emit(rom, "\x8e\xd0");                         // [0xff08] mov     ss, eax

		// Clear page directory
		emit(rom, "\xbf\x00\x10\x00\x00");             // [0xff0a] mov    edi, 0x1000
		emit(rom, "\xb9\x00\x10\x00\x00");             // [0xff0f] mov    ecx, 0x1000
		emit(rom, "\x31\xc0");                         // [0xff14] xor    eax, eax
		emit(rom, "\xf3\xab");                         // [0xff16] rep    stosd

		// Write 0xdeadbeef at physical memory address 0x5000
		emit(rom, "\xbf\x00\x50\x00\x00");             // [0xff18] mov    edi, 0x5000
		emit(rom, "\xb8\xef\xbe\xad\xde");             // [0xff1d] mov    eax, 0xdeadbeef
		emit(rom, "\x89\x07");                         // [0xff22] mov    [edi], eax

        // Identity map the RAM to 0x00000000
		emit(rom, "\xb9\x00\x01\x00\x00");             // [0xff24] mov    ecx, 0x100
		emit(rom, "\xbf\x00\x20\x00\x00");             // [0xff29] mov    edi, 0x2000
		emit(rom, "\xb8\x03\x00\x00\x00");             // [0xff2e] mov    eax, 0x0003
        // aLoop:
		emit(rom, "\xab");                             // [0xff33] stosd
		emit(rom, "\x05\x00\x10\x00\x00");             // [0xff34] add    eax, 0x1000
		emit(rom, "\xe2\xf8");                         // [0xff39] loop   aLoop

        // Identity map the ROM
		emit(rom, "\xb9\x10\x00\x00\x00");             // [0xff3b] mov    ecx, 0x10
		emit(rom, "\xbf\xc0\x3f\x00\x00");             // [0xff40] mov    edi, 0x3fc0
		emit(rom, "\xb8\x03\x00\xff\xff");             // [0xff45] mov    eax, 0xffff0003
        // bLoop:
		emit(rom, "\xab");                             // [0xff4a] stosd
		emit(rom, "\x05\x00\x10\x00\x00");             // [0xff4b] add    eax, 0x1000
		emit(rom, "\xe2\xf8");                         // [0xff50] loop   bLoop

        // Map physical address 0x5000 to virtual address 0x10000000
		emit(rom, "\xbf\x00\x40\x00\x00");             // [0xff52] mov    edi, 0x4000
		emit(rom, "\xb8\x03\x50\x00\x00");             // [0xff57] mov    eax, 0x5003
		emit(rom, "\x89\x07");                         // [0xff5c] mov    [edi], eax

        // Map physical address 0x6000 to virtual address 0x10001000
		emit(rom, "\xbf\x04\x40\x00\x00");             // [0xff5e] mov    edi, 0x4004
		emit(rom, "\xb8\x03\x60\x00\x00");             // [0xff63] mov    eax, 0x6003
		emit(rom, "\x89\x07");                         // [0xff68] mov    [edi], eax

        // Add page tables into page directory
		emit(rom, "\xbf\x00\x10\x00\x00");             // [0xff6a] mov    edi, 0x1000
		emit(rom, "\xb8\x03\x20\x00\x00");             // [0xff6f] mov    eax, 0x2003
		emit(rom, "\x89\x07");                         // [0xff74] mov    [edi], eax
		emit(rom, "\xbf\xfc\x1f\x00\x00");             // [0xff76] mov    edi, 0x1ffc
		emit(rom, "\xb8\x03\x30\x00\x00");             // [0xff7a] mov    eax, 0x3003
		emit(rom, "\x89\x07");                         // [0xff7f] mov    [edi], eax
		emit(rom, "\xbf\x00\x11\x00\x00");             // [0xff81] mov    edi, 0x1100
		emit(rom, "\xb8\x03\x40\x00\x00");             // [0xff86] mov    eax, 0x4003
		emit(rom, "\x89\x07");                         // [0xff8a] mov    [edi], eax

        // Load the page directory register
		emit(rom, "\xb8\x00\x10\x00\x00");             // [0xff8c] mov    eax, 0x1000
		emit(rom, "\x0f\x22\xd8");                     // [0xff91] mov    cr3, eax

        // Enable paging
		emit(rom, "\x0f\x20\xc0");                     // [0xff94] mov    eax, cr0
		emit(rom, "\x0d\x00\x00\x00\x80");             // [0xff97] or     eax, 0x80000000
		emit(rom, "\x0f\x22\xc0");                     // [0xff9c] mov    cr0, eax

        // Clear EAX
		emit(rom, "\x31\xc0");                         // [0xff9f] xor    eax, eax

        // Load using virtual memory address; EAX = 0xdeadbeef
		emit(rom, "\xbe\x00\x00\x00\x10");             // [0xffa4] mov    esi, 0x10000000
		emit(rom, "\x8b\x06");                         // [0xffa9] mov    eax, [esi]

		// First stop
		emit(rom, "\xf4");                             // [0xffab] hlt
		
		// Jump to RAM
		emit(rom, "\xe9\x54\x00\x00\x10");             // [0xffac] jmp    0x10000004
		// 7 bytes to spare... 0xffb8 contains initialization code

		// --- End of ROM code ------------------------------------------------------------------------------------------------

		// --- Start of RAM code ----------------------------------------------------------------------------------------------
		addr = 0x5004; // Addresses 0x5000..0x5003 are reserved for 0xdeadbeef
		// Note that these addresses are mapped to virtual addresses 0x10000000 through 0x10000fff
		
		// Do some basic stuff
		emit(ram, "\xba\x78\x56\x34\x12");             // [0x5004] mov    edx, 0x12345678
		emit(ram, "\xbf\x00\x00\x00\x10");             // [0x5009] mov    edi, 0x10000000
		emit(ram, "\x31\xd0");                         // [0x500e] xor    eax, edx
		emit(ram, "\x89\x07");                         // [0x5010] mov    [edi], eax
		emit(ram, "\xf4");                             // [0x5012] hlt

		// Setup a proper stack
		emit(ram, "\x31\xed");                         // [0x5013] xor    ebp, ebp
		emit(ram, "\xbc\x00\x00\x10\x00");             // [0x5015] mov    esp, 0x100000

		// Test the stack
		emit(ram, "\x68\xfe\xca\x0d\xf0");             // [0x501a] push   0xf00dcafe
		emit(ram, "\x5a");                             // [0x501f] pop    edx
		emit(ram, "\xf4");                             // [0x5020] hlt

		// Call interrupts
		emit(ram, "\xcd\x20");                         // [0x5021] int    0x20
		emit(ram, "\xcd\x21");                         // [0x5023] int    0x21
		emit(ram, "\xf4");                             // [0x5025] hlt

		addr = 0x6000; // Interrupt handlers
		// Note that these addresses are mapped to virtual addresses 0x10001000 through 0x10001fff
		// 0x20: Just IRET
		emit(ram, "\xcf");                             // [0x6000] iretd
		
		// 0x21: HLT, then IRET
		emit(ram, "\xf4");                             // [0x6001] hlt
		emit(ram, "\xcf");                             // [0x6002] iretd

		// 0x00 .. 0x1F: Clear stack then IRET
		emit(ram, "\x83\xc4\x04");                     // [0x6003] add    esp, 4
		emit(ram, "\xcf");                             // [0x6006] iretd

		#undef emit
	}


	Haxm haxm;

	HaxmStatus status = haxm.Initialize();
	switch (status) {
	case HXS_NOT_FOUND: printf("HAXM module not found: %d\n", haxm.GetLastError()); return -1;
	case HXS_INIT_FAILED: printf("Failed to open HAXM device %d\n", haxm.GetLastError()); return -1;
	default: break;
	}

	// Check version
	auto ver = haxm.GetModuleVersion();
	printf("HAXM module loaded\n");
	printf("  Current version: %d\n", ver->cur_version);
	printf("  Compatibility version: %d\n", ver->compat_version);

	// Check capabilities
	auto caps = haxm.GetCapabilities();
	printf("\nCapabilities:\n");
	if (caps->wstatus & HAX_CAP_STATUS_WORKING) {
		printf("  HAXM can be used on this host\n");
		if (caps->winfo & HAX_CAP_FASTMMIO) {
			printf("  HAXM kernel module supports fast MMIO\n");
		}
		if (caps->winfo & HAX_CAP_EPT) {
			printf("  Host CPU supports Extended Page Tables (EPT)\n");
		}
		if (caps->winfo & HAX_CAP_UG) {
			printf("  Host CPU supports Unrestricted Guest (UG)\n");
		}
		if (caps->winfo & HAX_CAP_64BIT_SETRAM) {
			printf("  64-bit RAM functionality is present\n");
		}
		if ((caps->wstatus & HAX_CAP_MEMQUOTA) && caps->mem_quota != 0) {
			printf("    Global memory quota enabled: %lld MB available\n", caps->mem_quota);
		}
	}
	else {
		printf("  HAXM cannot be used on this host\n");
		if (caps->winfo & HAX_CAP_FAILREASON_VT) {
			printf("  Intel VT-x not supported or disabled\n");
		}
		if (caps->winfo & HAX_CAP_FAILREASON_NX) {
			printf("  Intel Execute Disable Bit (NX) not supported or disabled\n");
		}
		return -1;
	}
	
	// ------------------------------------------------------------------------

	// Now let's have some fun!

	// Create a VM
	HaxmVM *vm;
	HaxmVMStatus vmStatus = haxm.CreateVM(&vm);
	switch (vmStatus) {
	case HXVMS_CREATE_FAILED: printf("Failed to create VM: %d\n", haxm.GetLastError()); return -1;
	}

	printf("\nVM created: ID %x\n", vm->ID());
	if (vm->FastMMIOEnabled()) {
		printf("  Fast MMIO enabled\n");
	}

	// Allocate RAM
	vmStatus = vm->AllocateMemory(ram, ramSize, ramBase, HXVM_MEM_RAM);
	switch (vmStatus) {
	case HXVMS_MEM_MISALIGNED: printf("Failed to allocate RAM: host memory block is misaligned\n"); return -1;
	case HXVMS_MEMSIZE_MISALIGNED: printf("Failed to allocate RAM: memory block is misaligned\n"); return -1;
	case HXVMS_ALLOC_MEM_FAILED: printf("Failed to allocate RAM: %d\n", vm->GetLastError()); return -1;
	case HXVMS_SET_MEM_FAILED: printf("Failed to configure RAM: %d\n", vm->GetLastError()); return -1;
	}

	printf("  Allocated %d bytes of RAM at physical address 0x%08x\n", ramSize, ramBase);

	// Allocate ROM
	vmStatus = vm->AllocateMemory(rom, romSize, romBase, HXVM_MEM_ROM);
	switch (vmStatus) {
	case HXVMS_MEM_MISALIGNED: printf("Failed to allocate ROM: host memory block is misaligned\n"); return -1;
	case HXVMS_MEMSIZE_MISALIGNED: printf("Failed to allocate ROM: memory block is misaligned\n"); return -1;
	case HXVMS_ALLOC_MEM_FAILED: printf("Failed to allocate ROM: %d\n", vm->GetLastError()); return -1;
	case HXVMS_SET_MEM_FAILED: printf("Failed to configure ROM: %d\n", vm->GetLastError()); return -1;
	}

	printf("  Allocated %d bytes of ROM at physical address 0x%08x\n", romSize, romBase);

	// Add a virtual CPU to the VM
	HaxmVCPU *vcpu;
	HaxmVCPUStatus vcpuStatus = vm->CreateVCPU(&vcpu);
	switch (vcpuStatus) {
	case HXVCPUS_CREATE_FAILED: printf("Failed to create VCPU for VM: %d\n", vm->GetLastError()); return -1;
	}

	printf("  VCPU created with ID %d\n", vcpu->ID());
	printf("    VCPU Tunnel allocated at 0x%016llx\n", (uint64_t)vcpu->Tunnel());
	printf("    VCPU I/O tunnel allocated at 0x%016llx\n", (uint64_t)vcpu->IOTunnel());
	printf("\n");

	// Get CPU registers
	struct vcpu_state_t regs;
	vcpuStatus = vcpu->GetRegisters(&regs);
	switch (vcpuStatus) {
	case HXVCPUS_FAILED: printf("Failed to get VCPU registers: %d\n", vcpu->GetLastError()); return -1;
	}
	
	// Get FPU registers
	struct fx_layout fpu;
	vcpuStatus = vcpu->GetFPURegisters(&fpu);
	switch (vcpuStatus) {
	case HXVCPUS_FAILED: printf("Failed to get VCPU floating point registers: %d\n", vcpu->GetLastError()); return -1;
	}

	#ifdef DO_MANUAL_INIT
	// Load GDT/IDT tables
	regs._gdt.base = regs._idt.base = 0xffffffd8;
	regs._gdt.limit = regs._idt.limit = 0x0018;

	// Enter protected mode
	regs._cr0 |= 1;

	// Skip initialization code
	regs._eip = 0xffce;

	vcpu->SetRegisters(&regs);
	#endif
	
	printf("\nInitial CPU register state:\n");
	printRegs(&regs);
	//printFPURegs(&fpu);
	printf("\n");

	// ----- Start of emulation -----------------------------------------------------------------------------------------------

	// The CPU starts in 16-bit real mode.
	// Memory addressing is based on segments and offsets, where a segment is basically a 16-byte offset.

	// Run the CPU!
	vcpu->Run();

	#ifdef DO_MANUAL_JMP
	// Do the jmp dword 0x8:0xffffff00 manually
	vcpu->GetRegisters(&regs);

	// Set basic register data
	regs._cs.selector = 0x0008;
	regs._eip = 0xffffff00;

	// Find GDT entry in memory
	uint64_t gdtEntry;
	if (regs._gdt.base >= ramBase && regs._gdt.base <= ramBase + ramSize - 1) {
		// GDT is in RAM
		gdtEntry = *(uint64_t *)&ram[regs._gdt.base - ramBase + regs._cs.selector];
	}
	else if (regs._gdt.base >= romBase && regs._gdt.base <= romBase + romSize - 1) {
		// GDT is in ROM
		gdtEntry = *(uint64_t *)&rom[regs._gdt.base - romBase + regs._cs.selector];
	}

	// Fill in the rest of the CS info with data from the GDT entry
	regs._cs.ar = ((gdtEntry >> 40) & 0xf0ff);
	regs._cs.base = ((gdtEntry >> 16) & 0xfffff) | (((gdtEntry >> 56) & 0xff) << 20);
	regs._cs.limit = ((gdtEntry & 0xffff) | (((gdtEntry >> 48) & 0xf) << 16));
	if (regs._cs.ar & 0x8000) {
		// 4 KB pages
		regs._cs.limit = (regs._cs.limit << 12) | 0xfff;
	}

	vcpu->SetRegisters(&regs);

	// Run the CPU again!
	vcpu->Run();
	#endif

	#ifdef DO_MANUAL_PAGING
	// Prepare the registers
	vcpu->GetRegisters(&regs);
	regs._eax = 0;
	regs._esi = 0x10000000;
	regs._eip = 0xffffff9c;
	regs._cr0 = 0xe0000011;
	regs._cr3 = 0x1000;
	regs._ss.selector = regs._ds.selector = regs._es.selector = 0x0010; 
	regs._ss.limit = regs._ds.limit = regs._es.limit = 0xffffffff;
	regs._ss.base = regs._ds.base = regs._es.base = 0;
	regs._ss.ar = regs._ds.ar = regs._es.ar = 0xc093;

	vcpu->SetRegisters(&regs);

	// Clear page directory
	memset(&ram[0x1000], 0, 0x1000);
	
	// Write 0xdeadbeef at physical memory address 0x5000
	*(uint32_t *)&ram[0x5000] = 0xdeadbeef;

	// Identity map the RAM
	for (uint32_t i = 0; i < 0x100; i++) {
		*(uint32_t *)&ram[0x2000 + i * 4] = 0x0003 + i * 0x1000;
	}

	// Identity map the ROM
	for (uint32_t i = 0; i < 0x10; i++) {
		*(uint32_t *)&ram[0x3fc0 + i * 4] = 0xffff0003 + i * 0x1000;
	}

	// Map physical address 0x5000 to virtual address 0x10000000
	*(uint32_t *)&ram[0x4000] = 0x5003;

	// Add page tables into page directory
	*(uint32_t *)&ram[0x1000] = 0x2003;
	*(uint32_t *)&ram[0x1ffc] = 0x3003;
	*(uint32_t *)&ram[0x1100] = 0x4003;

	// Run the CPU again!
	vcpu->Run();
	#endif

	// ----- First part -------------------------------------------------------------------------------------------------------

	printf("Testing data in virtual memory\n\n");

	// Get CPU status
	auto tunnel = vcpu->Tunnel();
	switch (tunnel->_exit_status) {
	case HAX_EXIT_HLT:
		printf("Emulation exited due to HLT instruction as expected!\n");
		break;
	default:
		printf("Emulation exited for another reason: %d\n", tunnel->_exit_status);
		break;
	}

	// Get CPU registers again
	vcpuStatus = vcpu->GetRegisters(&regs);
	switch (vcpuStatus) {
	case HXVCPUS_FAILED: printf("Failed to get VCPU registers: %d\n", vcpu->GetLastError()); return -1;
	}

	// Get FPU registers again
	vcpuStatus = vcpu->GetFPURegisters(&fpu);
	switch (vcpuStatus) {
	case HXVCPUS_FAILED: printf("Failed to get VCPU floating point registers: %d\n", vcpu->GetLastError()); return -1;
	}

	// Validate first stop output
	if (regs._eip == 0xffffffab && regs._cs.selector == 0x0008) {
		printf("Emulation stopped at the right place!\n");
		if (regs._eax == 0xdeadbeef) {
			printf("And we got the right result!\n");
		}
	}

	printf("\nFirst stop CPU register state:\n");
	printRegs(&regs);
	//printFPURegs(&fpu);
	printf("\n");

	// ----- Second part ------------------------------------------------------------------------------------------------------
	
	printf("Testing code in virtual memory\n\n");

	// Run CPU once more
	vcpu->Run();
	switch (tunnel->_exit_status) {
	case HAX_EXIT_HLT:
		printf("Emulation exited due to HLT instruction as expected!\n");
		break;
	default:
		printf("Emulation exited for another reason: %d\n", tunnel->_exit_status);
		break;
	}

	// Refresh CPU registers
	vcpuStatus = vcpu->GetRegisters(&regs);
	switch (vcpuStatus) {
	case HXVCPUS_FAILED: printf("Failed to get VCPU registers: %d\n", vcpu->GetLastError()); return -1;
	}

	// Refresh FPU registers
	vcpuStatus = vcpu->GetFPURegisters(&fpu);
	switch (vcpuStatus) {
	case HXVCPUS_FAILED: printf("Failed to get VCPU floating point registers: %d\n", vcpu->GetLastError()); return -1;
	}

	// Validate second stop output
	if (regs._eip == 0x10000013) {
		printf("Emulation stopped at the right place!\n");
		uint32_t memValue = *(uint32_t *)&ram[0x5000];
		if (regs._eax == 0xcc99e897 && regs._edx == 0x12345678 && memValue == 0xcc99e897) {
			printf("And we got the right result!\n");
		}
	}

	printf("\nCPU register state:\n");
	printRegs(&regs);
	//printFPURegs(&fpu);
	printf("\n");

	// ----- Stack ------------------------------------------------------------------------------------------------------------

	printf("Testing the stack\n\n");

	// Run CPU once more
	vcpu->Run();
	switch (tunnel->_exit_status) {
	case HAX_EXIT_HLT:
		printf("Emulation exited due to HLT instruction as expected!\n");
		break;
	default:
		printf("Emulation exited for another reason: %d\n", tunnel->_exit_status);
		break;
	}

	// Refresh CPU registers
	vcpuStatus = vcpu->GetRegisters(&regs);
	switch (vcpuStatus) {
	case HXVCPUS_FAILED: printf("Failed to get VCPU registers: %d\n", vcpu->GetLastError()); return -1;
	}

	// Refresh FPU registers
	vcpuStatus = vcpu->GetFPURegisters(&fpu);
	switch (vcpuStatus) {
	case HXVCPUS_FAILED: printf("Failed to get VCPU floating point registers: %d\n", vcpu->GetLastError()); return -1;
	}

	// Validate stack results
	if (regs._eip == 0x10000021) {
		printf("Emulation stopped at the right place!\n");
		uint32_t memValue = *(uint32_t *)&ram[0xffffc];
		if (regs._edx == 0xf00dcafe && regs._esp == 0x00100000 && memValue == 0xf00dcafe) {
			printf("And we got the right result!\n");
		}
	}

	printf("\nCPU register state:\n");
	printRegs(&regs);
	//printFPURegs(&fpu);
	printf("\n");

	// ----- Interrupts -------------------------------------------------------------------------------------------------------

	printf("Testing interrupts\n\n");
	
	// First stop at the HLT inside INT 0x21
	vcpu->Run();
	switch (tunnel->_exit_status) {
	case HAX_EXIT_HLT:
		printf("Emulation exited due to HLT instruction as expected!\n");
		break;
	default:
		printf("Emulation exited for another reason: %d\n", tunnel->_exit_status);
		break;
	}

	// Refresh CPU registers
	vcpuStatus = vcpu->GetRegisters(&regs);
	switch (vcpuStatus) {
	case HXVCPUS_FAILED: printf("Failed to get VCPU registers: %d\n", vcpu->GetLastError()); return -1;
	}

	// Refresh FPU registers
	vcpuStatus = vcpu->GetFPURegisters(&fpu);
	switch (vcpuStatus) {
	case HXVCPUS_FAILED: printf("Failed to get VCPU floating point registers: %d\n", vcpu->GetLastError()); return -1;
	}

	// Validate registers
	if (regs._eip == 0x10001002) {
		printf("Emulation stopped at the right place!\n");
	}

	printf("\nCPU register state:\n");
	printRegs(&regs);
	//printFPURegs(&fpu);
	printf("\n");


	// Now we should hit the HLT after INT 0x21
	vcpu->Run();
	switch (tunnel->_exit_status) {
	case HAX_EXIT_HLT:
		printf("Emulation exited due to HLT instruction as expected!\n");
		break;
	default:
		printf("Emulation exited for another reason: %d\n", tunnel->_exit_status);
		break;
	}

	// Refresh CPU registers
	vcpuStatus = vcpu->GetRegisters(&regs);
	switch (vcpuStatus) {
	case HXVCPUS_FAILED: printf("Failed to get VCPU registers: %d\n", vcpu->GetLastError()); return -1;
	}

	// Refresh FPU registers
	vcpuStatus = vcpu->GetFPURegisters(&fpu);
	switch (vcpuStatus) {
	case HXVCPUS_FAILED: printf("Failed to get VCPU floating point registers: %d\n", vcpu->GetLastError()); return -1;
	}

	// Validate registers
	if (regs._eip == 0x10000026) {
		printf("Emulation stopped at the right place!\n");
	}

	printf("\nFinal CPU register state:\n");
	printRegs(&regs);
	//printFPURegs(&fpu);

	_aligned_free(rom);
	_aligned_free(ram);
	
	return 0;
}
