# Summary

- [0. Preface](./Preface.md)
- [N. Linux_Networking](./N_Linux_Networking.md)
	- [N1. Setup Qemu](./N/1_setup_qemu.md)
		- [N1. Setup UML (older)](./N/1_setup_uml.md)
	- [N2. Packet RX path 1 : Basic](./N/2_Packet_RX_Basic.md)
		- [N2.1-N2.3 Top Half Processing](./N/2_1-3_top_half_processing.md)
		- [N2.4-N2.5 Bottom Half Processing](./N/2_4-5_bottom_half_processing.md)
		- [N2.6-N2.7 IP and UDP Processing](./N/2_6-7_ip_and_udp_processing.md)
	- [N3. Packet TX path 1 : Basic](./N/3_Packet_TX_Basic.md)
		- [N3.1-3.2 sendmsg() from userspace](./N/3_1-3_sendmsg_from_userspace.md)
		- [N3.3-3.4 alloc skb and send_skb](./N/3_3-4_alloc_and_send_skb.md)
		- [N3.5-3.8 NET_TX and driver xmit](./N/3_5-8_net_tx_and_driver_xmit.md)
	- [N4. (WIP) Socket Programming BTS](./N/4_Socket_Programming_BTS.md)

- [M. Miscellaneous](./M_Miscellaneous.md)
	- [CSE 222a, Notes](./M/1_CSE222a_Notes.md)
	- [Cache Side Channel Attacks](./M/2_Cache_Side_Channel_Attacks.md)

- [A. Appendix](./Appendix.md)
