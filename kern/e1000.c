#include <kern/e1000.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/error.h>

static struct E1000 *base;

struct tx_desc *tx_descs;
#define N_TXDESC (PGSIZE / sizeof(struct tx_desc))
char tx_packet_buffer[N_TXDESC][TX_PACKET_SIZE];

int
e1000_tx_init()
{
	// Allocate one page for descriptors
	struct PageInfo *pp = page_alloc(ALLOC_ZERO);
	if (pp == NULL) 
		panic("e1000_tx_init: out of memory");
	tx_descs = (struct tx_desc *)mmio_map_region(page2pa(pp), PGSIZE);

	// Initialize all descriptors
	memset(tx_packet_buffer, '\0', N_TXDESC * TX_PACKET_SIZE);
	for (int i = 0; i < N_TXDESC; i++) {
		tx_descs[i].addr = PADDR(tx_packet_buffer[i]);
		tx_descs[i].cmd = E1000_TX_CMD_RS | E1000_TX_CMD_EOP;
		tx_descs[i].status = E1000_TX_STATUS_DD;
	}

    // Set hardward registers
	// Look kern/e1000.h to find useful definations
	base->TDBAL = page2pa(pp);
	base->TDBAH = 0;
	base->TDLEN = PGSIZE;
	base->TDH = 0;
	base->TDT = 0;
	base->TCTL = E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT_ETHER |
					E1000_TCTL_COLD_FULL_DUPLEX;
	base->TIPG = E1000_TIPG_DEFAULT;

	return 0;
}

struct rx_desc *rx_descs;
#define N_RXDESC (PGSIZE / sizeof(struct rx_desc))
char rx_packet_buffer[N_RXDESC][RX_PACKET_SIZE];

int
e1000_rx_init()
{
	// Allocate one page for descriptors
	struct PageInfo *pp = page_alloc(ALLOC_ZERO);
	if (pp == NULL) 
		panic("e1000_rx_init: out of memory");
	rx_descs = (struct rx_desc *)mmio_map_region(page2pa(pp), PGSIZE);

	// Initialize all descriptors
	// You should allocate some pages as receive buffer
	memset(rx_packet_buffer, '\0', N_RXDESC * RX_PACKET_SIZE);
	for (int i = 0; i < N_RXDESC; i++) 
		rx_descs[i].addr = PADDR(rx_packet_buffer[i]);

	// Set hardward registers
	// Look kern/e1000.h to find useful definations
	base->RAL = QEMU_MAC_LOW;
	base->RAH = QEMU_MAC_HIGH;
	for (int i = 0; i < 128; i++) 
		base->MTA[i] = 0;
	base->RDBAL = page2pa(pp);
	base->RDBAH = 0;
	base->RDLEN = PGSIZE;
	base->RDH = 0;
	base->RDT = N_RXDESC - 1;
	base->RCTL = E1000_RCTL_BSIZE_2048 | E1000_RCTL_SECRC;
	base->RCTL |= E1000_RCTL_EN;
	return 0;
}

int
pci_e1000_attach(struct pci_func *pcif)
{
	// Enable PCI function
	pci_func_enable(pcif);

	// Map MMIO region and save the address in 'base;
	base = (struct E1000 *)mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);

	cprintf("device status register: 0x%08x\n", base->STATUS);

	e1000_tx_init();
	e1000_rx_init();
	return 0;
}

int
e1000_tx(const void *buf, uint32_t len)
{
	// Send 'len' bytes in 'buf' to ethernet
	// Hint: buf is a kernel virtual address
	uint32_t tdt = base->TDT;
	struct tx_desc *desc = &tx_descs[tdt];
	if (!(desc->status & E1000_TX_STATUS_DD)) 
		return -E_AGAIN;

	memset(tx_packet_buffer[tdt], '\0', TX_PACKET_SIZE);
	memmove(tx_packet_buffer[tdt], buf, len);
	desc->length = len;
	desc->status &= ~E1000_TX_STATUS_DD;
	base->TDT = (base->TDT + 1) % N_TXDESC;

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
	uint32_t rdt = (base->RDT + 1) % N_TXDESC;
	struct rx_desc *desc = &rx_descs[rdt];
	if (!(desc->status & E1000_RX_STATUS_DD)) 
		return -E_AGAIN;

	if (len < desc->length) 
		panic("e1000_rx: buf is too small to hold the packet");

	memmove(buf, rx_packet_buffer[rdt], desc->length);
	desc->status &= ~E1000_RX_STATUS_DD;
	base->RDT = rdt;

	return desc->length;
}
