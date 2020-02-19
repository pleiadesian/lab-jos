#include "ns.h"

#define N_TXDESC (PGSIZE / 16)
#define TX_PACKET_SIZE 1518

extern union Nsipc nsipcbuf;

#ifdef ZERO_COPY
char *tx_packet_buffer;
#endif

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet request (using ipc_recv)
	//	- send the packet to the device driver (using sys_net_send)
	//	do the above things in a loop
	int r;
	envid_t whom;

#ifdef ZERO_COPY
	tx_packet_buffer = (char *)UTXBASE;
#endif

	while (true) {
		if ((r = ipc_recv(&whom, &nsipcbuf, NULL)) < 0) {
			cprintf("ipc_recv: %e", r);
			continue;
		}
		if (whom != ns_envid) {
			cprintf("output: sender is not network server\n");
			continue;
		}
		if (r != NSREQ_OUTPUT) {
			cprintf("output: the value sent by the sender is not NSREQ_OUTPUT\n");
			continue;
		}
#ifdef ZERO_COPY
		uint32_t tdt = sys_net_tdt();
		if (tdt < 0) 
			continue;
		
		memset(tx_packet_buffer + tdt * TX_PACKET_SIZE, '\0', TX_PACKET_SIZE);
		memcpy((char *)(tx_packet_buffer + tdt * TX_PACKET_SIZE), nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len);
#endif
		while ((r = sys_net_send(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len)) < 0);
	}
}
