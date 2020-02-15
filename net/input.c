#include "ns.h"

#define RX_PACKET_SIZE 2048

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server (using ipc_send with NSREQ_INPUT as value)
	//	do the above things in a loop
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	int r;
	char buf[RX_PACKET_SIZE];
	while (true) {
		if ((r = sys_net_recv(buf, RX_PACKET_SIZE)) < 0) 
			continue;
		while (sys_page_alloc(0, &nsipcbuf, PTE_U | PTE_W | PTE_P) < 0);
		nsipcbuf.pkt.jp_len = r;
		memcpy(&nsipcbuf.pkt.jp_data, buf, r);
		ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_U | PTE_W | PTE_P);
	}
}
