### N2.6 IP layer Processing

Assuming the skb is an IP packet, the skb will enter `ip_rcv()`, which then calls `ip_rcv_core()`. `ip_rcv_core` validates the IP header (checksum, checks on ip header length, etc), updates ip stats and based on the transport header set in the IP header will set `skb->transport_header`.

The protocol stacks maintain counts when packets enter and counts of the number of packets that were dropped. These numbers can be seen at `/proc/net/snmp`. The correspnding enum can be found at `include/uapi/linux/snmp.h`.

```
ip_rcv_core()
{
    __IP_UPD_PO_STATS(net, IPSTATS_MIB_IN, skb->len);

    iph = ip_hdr(skb);
    
    if (unlikely(ip_fast_csum((u8 *)iph, iph->ihl)))
        goto csum_error;
    
    skb->transport_header = skb->network_header + iph->ihl*4;
    return skb;

csum_error:
    __IP_INC_STATS(net, IPSTATS_MIB_CSUMERRORS);
    return NULL;
}
```



ip_rcv then sends the packet through the netfilter PREROUTING chain. The netfilter subsystem allows the userspace to filter/modify/drop packets based on the packet's attributes. Tools iptables/ip6tables are used to add/remove IP/IPv6 rules. The netfilter subsystem contains 5 chains, PREROUTING, INPUT, FORWARD, OUTPUT and POSTROUTING. Each chain contains rules and corresponding actions. If a rule matches a packet, the corresponding action is taken. We will look at them in a separate page dedicated to iptables. An easy example is that it is used to act as a firewall to drop unwanted traffic.

```
	                transport layer (TCP/UDP)

   ip_local_deliver_finish()
	        🠕                                   |
	      INPUT                               OUTPUT
	        |                                   🠗
	ROUTING DECISION  -----  FORWARDING  -----  +
	        🠕                                   |
	    PREROUTING                         POSTROUTING
	        |                                   🠗
	     ip_rcv()

	                     CORE NETWORKING 
```

While receiving a packet, at the end of `ip_rcv()` it enters the PREROUTING chain, at the end of which it enters `ip_rcv_finish()`. Based on the packet's ip address, a routing decision is taken if the packet should be locally consumed or if it is to forwarded to a different system. (I'll describe this in more detail in a separate page). If the packet should be locally consumed `ip_local_deliver()` is called. The packet then enters the INPUT chain, and finally comes out at `ip_local_deliver_finish()`.

Based on the protocol set in the IP header, the corresponding protocol handler is called. If a transport protocol is supported over IP, the corresponding handler is registered by calling inet_add_protocol().

Yes, this section skips a lot of content, I'll add a separate sections for IP processing, netfilter (esp. nftables) and routing.
