#include <kern/e1000.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/error.h>

static struct E1000 *base;

struct tx_desc *tx_descs;
#define N_TXDESC (PGSIZE / sizeof(struct tx_desc))
#define TX_PACKET_SIZE 1518
// struct tx_desc tx_descs[N_TXDESC] __attribute__((aligned(16)));
char tx_packet_buffer[N_TXDESC][TX_PACKET_SIZE];

int
e1000_tx_init()
{
	// Allocate one page for descriptors
	struct PageInfo *pp = page_alloc(ALLOC_ZERO);
	if (pp == NULL) 
		panic("e1000_tx_init: out of memory");
	tx_descs = page2kva(pp);
	// page_insert(kern_pgdir, pp, tx_descs, PTE_PCD | PTE_PWT | PTE_W);
	// tx_descs = (struct tx_desc *)KSTACKTOP;
	// pte_t *pte = pgdir_walk(kern_pgdir, tx_descs, true);

	// Initialize all descriptors
	for (int i = 0; i < N_TXDESC; i++) {
		tx_descs[i].addr = PADDR(tx_packet_buffer[i]);
		tx_descs[i].cmd = E1000_TX_CMD_RS | E1000_TX_CMD_EOP;
		tx_descs[i].status = E1000_TX_STATUS_DD;
	}

    // Set hardward registers
	// Look kern/e1000.h to find useful definations
	// base->TDBAL = PADDR(tx_descs);
	base->TDBAL = page2pa(pp);
	base->TDBAH = 0;
	base->TDLEN = PGSIZE;
	base->TDH = 0;
	base->TDT = 0;
	base->TCTL |= E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT_ETHER |
					E1000_TCTL_COLD_FULL_DUPLEX;
	base->TIPG = E1000_TIPG_DEFAULT;

	return 0;
}

struct rx_desc *rx_descs;
#define N_RXDESC (PGSIZE / sizeof(struct rx_desc))

int
e1000_rx_init()
{
	// Allocate one page for descriptors

	// Initialize all descriptors
	// You should allocate some pages as receive buffer

	// Set hardward registers
	// Look kern/e1000.h to find useful definations

	return 0;
}

int
pci_e1000_attach(struct pci_func *pcif)
{
	// Enable PCI function
	pci_func_enable(pcif);

	// Map MMIO region and save the address in 'base;
	base = (struct E1000 *)mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);

	// cprintf("device status register: 0x%08x\n", base->STATUS);

	e1000_tx_init();
	e1000_rx_init();
	return 0;
}

int
e1000_tx(const void *buf, uint32_t len)
{
	// Send 'len' bytes in 'buf' to ethernet
	// Hint: buf is a kernel virtual address

	return 0;
}

int
e1000_rx(void *buf, uint32_t len)
{
	// Copy one received buffer to buf
	// You could return -E_AGAIN if there is no packet
	// Check whether the buf is large enough to hold
	// the packet
	// Do not forget to reset the decscriptor and
	// give it back to hardware by modifying RDT

	return 0;
}
