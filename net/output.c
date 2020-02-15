#include "ns.h"

extern union Nsipc nsipcbuf;

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
		while ((r = sys_net_send(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len)) < 0) {
			cprintf("sys_net_send: %e\n", r);
		}
	}
}
