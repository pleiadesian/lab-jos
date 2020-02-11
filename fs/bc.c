
#include "fs.h"

// #define EVICT_POLICY

// Return the virtual address of this disk block.
void*
diskaddr(uint32_t blockno)
{
	if (blockno == 0 || (super && blockno >= super->s_nblocks))
		panic("bad block number %08x in diskaddr", blockno);
	return (char*) (DISKMAP + blockno * BLKSIZE);
}

// Is this virtual address mapped?
bool
va_is_mapped(void *va)
{
	return (uvpd[PDX(va)] & PTE_P) && (uvpt[PGNUM(va)] & PTE_P);
}

// Is this virtual address dirty?
bool
va_is_dirty(void *va)
{
	return (uvpt[PGNUM(va)] & PTE_D) != 0;
}

// Fault any disk block that is read in to memory by
// loading it from disk.
static void
bc_pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t blockno = ((uint32_t)addr - DISKMAP) / BLKSIZE;
	int r;

	// Check that the fault was within the block cache region
	if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
		panic("page fault in FS: eip %08x, va %08x, err %04x",
		      utf->utf_eip, addr, utf->utf_err);

	// Sanity check the block number.
	if (super && blockno >= super->s_nblocks)
		panic("reading non-existent block %08x\n", blockno);

	// Allocate a page in the disk map region, read the contents
	// of the block from the disk into that page.
	// Hint: first round addr to page boundary. fs/ide.c has code to read
	// the disk.
	//
	// LAB 5: you code here:
#ifndef EVICT_POLICY
	addr = ROUNDDOWN(addr, PGSIZE);
	if ((r = sys_page_alloc(0, addr, PTE_SYSCALL)) < 0)
		panic("sys_page_alloc: %e", r);

	if ((r = ide_read(blockno * BLKSECTS, addr, BLKSECTS)) < 0) 
		panic("ide_read: %e", r);

	// Clear the dirty bit for the disk block page since we just read the
	// block from disk
	if ((r = sys_page_map(0, addr, 0, addr, uvpt[PGNUM(addr)] & PTE_SYSCALL)) < 0)
		panic("in bc_pgfault, sys_page_map: %e", r);

	// Check that the block we read was allocated. (exercise for
	// the reader: why do we do this *after* reading the block
	// in?)
	if (bitmap && block_is_free(blockno))
		panic("reading free block %08x\n", blockno);
#else
	static uint32_t clock_pointer = 0;
	static uint32_t not_cold_cache = 0;

	addr = ROUNDDOWN(addr, PGSIZE);
	if (blockno <= 2) {
		// super block should not be evicted
		if ((r = sys_page_alloc(0, addr, PTE_SYSCALL)) < 0)
			panic("sys_page_alloc: %e", r);		
	} else if (not_cold_cache < BCSIZE) {
		block_cache[not_cold_cache] = addr;
		cprintf("put page %08x into block cache %d\n", (uintptr_t)addr, not_cold_cache);
		not_cold_cache++;
		if ((r = sys_page_alloc(0, addr, PTE_SYSCALL)) < 0)
			panic("sys_page_alloc: %e", r);
	} else {
		// find victim page
		bool victim_not_found = true;
		void *victim_addr = NULL;
		while (victim_not_found) {
			uint32_t bc_pointer = clock_pointer % BCSIZE;
			void *page_addr = block_cache[bc_pointer];
			if (!(uvpt[PGNUM(page_addr)] & PTE_A)) {
				victim_addr = page_addr;
				block_cache[bc_pointer] = addr;
				victim_not_found = false;
				cprintf("evict page %08x from block cache %d\n", (uintptr_t)page_addr, bc_pointer);
				cprintf("put page %08x into block cache %d\n", (uintptr_t)addr, bc_pointer);
			} else {
				// flush dirty page
				if (uvpt[PGNUM(page_addr)] & PTE_D) 
					flush_block(page_addr);
				// reset access bit
				if ((r = sys_page_map(0, page_addr, 0, page_addr,
									uvpt[PGNUM(page_addr)] &
									(PTE_SYSCALL))) < 0)
					panic("sys_page_map: %e", r);
				clock_pointer++;
				cprintf("reset page %08x's access bit\n", (uintptr_t)page_addr);
			}
		}
		// flush victim page
		if (uvpt[PGNUM(victim_addr)] & PTE_D) 
			flush_block(victim_addr);

		// remap page to new candidate
		if ((r = sys_page_map(0, victim_addr, 0, addr, PTE_SYSCALL)) < 0) 
			panic("sys_page_map: %e", r); 

		// unmap victim page
		if ((r = sys_page_unmap(0, victim_addr)) < 0) 
			panic("sys_page_unmap: %e", r);

	}
	if ((r = ide_read(blockno * BLKSECTS, addr, BLKSECTS)) < 0) 
		panic("ide_read: %e", r);

	// clear dirty bit
	if ((r = sys_page_map(0, addr, 0, addr, PTE_SYSCALL)) < 0)
		panic("in bc_pgfault, sys_page_map: %e", r);

	if (bitmap && block_is_free(blockno))
		panic("reading free block %08x\n", blockno);
#endif
}

// Flush the contents of the block containing VA out to disk if
// necessary, then clear the PTE_D bit using sys_page_map.
// If the block is not in the block cache or is not dirty, does
// nothing.
// Hint: Use va_is_mapped, va_is_dirty, and ide_write.
// Hint: Use the PTE_SYSCALL constant when calling sys_page_map.
// Hint: Don't forget to round addr down.
void
flush_block(void *addr)
{
	uint32_t blockno = ((uint32_t)addr - DISKMAP) / BLKSIZE;

	if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
		panic("flush_block of bad va %08x", addr);

	// LAB 5: Your code here.
	// panic("flush_block not implemented");
	if (!va_is_mapped(addr) || !va_is_dirty(addr)) 
		return;

	int r;
	addr = ROUNDDOWN(addr, PGSIZE);
	if ((r = ide_write(blockno * BLKSECTS, addr, BLKSECTS)) < 0) 
		panic("ide_write: %e", r);

	// Clear the dirty bit
	if ((r = sys_page_map(0, addr, 0, addr, uvpt[PGNUM(addr)] & PTE_SYSCALL)) < 0)
		panic("in flush_block, sys_page_map: %e", r);
}

// Test that the block cache works, by smashing the superblock and
// reading it back.
static void
check_bc(void)
{
	struct Super backup;

	// back up super block
	memmove(&backup, diskaddr(1), sizeof backup);

	// smash it
	strcpy(diskaddr(1), "OOPS!\n");
	flush_block(diskaddr(1));
	assert(va_is_mapped(diskaddr(1)));
	assert(!va_is_dirty(diskaddr(1)));

	// clear it out
	sys_page_unmap(0, diskaddr(1));
	assert(!va_is_mapped(diskaddr(1)));

	// read it back in
	assert(strcmp(diskaddr(1), "OOPS!\n") == 0);

	// fix it
	memmove(diskaddr(1), &backup, sizeof backup);
	flush_block(diskaddr(1));

	// Now repeat the same experiment, but pass an unaligned address to
	// flush_block.

	// back up super block
	memmove(&backup, diskaddr(1), sizeof backup);

	// smash it
	strcpy(diskaddr(1), "OOPS!\n");

	// Pass an unaligned address to flush_block.
	flush_block(diskaddr(1) + 20);
	assert(va_is_mapped(diskaddr(1)));

	// Skip the !va_is_dirty() check because it makes the bug somewhat
	// obscure and hence harder to debug.
	//assert(!va_is_dirty(diskaddr(1)));

	// clear it out
	sys_page_unmap(0, diskaddr(1));
	assert(!va_is_mapped(diskaddr(1)));

	// read it back in
	assert(strcmp(diskaddr(1), "OOPS!\n") == 0);

	// fix it
	memmove(diskaddr(1), &backup, sizeof backup);
	flush_block(diskaddr(1));

	cprintf("block cache is good\n");
}

void
bc_init(void)
{
	struct Super super;
	set_pgfault_handler(bc_pgfault);
	check_bc();

	// cache the super block by reading it once
	memmove(&super, diskaddr(1), sizeof super);
}

