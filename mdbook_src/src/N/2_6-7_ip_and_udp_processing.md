# N2.6-N2.7 IP and UDP Processing

### N2.6 IP layer

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

### N2.7 UDP layer

If the packet is an UDP packet, `udp_rcv` is the protocol handler called, which internally calls `__udp4_lib_rcv()`. First the packet header is validated, pseudo ip checksum is checked and then if the packet is unicast, based on the port numbers the socket is looked up, and then `udp_unicast_rcv_skb()` is called, which then calls `udp_queue_rcv_skb()`.

`udp_queue_rcv_skb()` checks if the udp socket has a registered function to handle encapsulated packets. If the handler is found the corresponding handler is called, which processes the packet further. For example in case of XFRM encapsulation `xfrm4_udp_encap_rcv()` is registered as the handler. (XRFM short for transform, adds support to add encrypted tunnels in the kernel).

If no encap_rcv handler is found, full udp checksum is done and `__udp_queue_rcv_skb()` is called. Internally it calls `__udp_enqueue_schedule_skb()` which checks if the sk memory is sufficient to add the packet and then calls `__skb_queue_tail()` to enqueue the packet into `sk_receive_queue`. If the application has called the recv() system call and is waiting for the packet the process moves to __TASK_STOPPED state and the scheduler no longer schedules it. `sk->sk_data_ready(sk)` is called so that it's state is set to TASK_INTERRUPTIBLE, and the scheduler then schedules the application. On waking up, the packet is dequeued from the queue and the application recv()s the packet data. Receiving a packet and socket calls will be described in a separate page.

```c
__udp4_lib_rcv()
{
    uh   = udp_hdr(skb);
    if (udp4_csum_init(skb, uh, proto)) //pseudo ip csum
        goto csum_error;

    sk = __udp4_lib_lookup_skb(skb, uh->source, uh->dest, udptable);
    if (sk) {
        return udp_unicast_rcv_skb(sk, skb, uh)
        {
            ret = udp_queue_rcv_skb(sk, skb);
                  //continued below..
        }
    }

}

udp_queue_rcv_skb(sk, skb)
{
    struct udp_sock *up = udp_sk(sk);

    encap_rcv = READ_ONCE(up->encap_rcv);
    if (encap_rcv) {
        if (udp_lib_checksum_complete(skb))
            goto csum_error;
    
        ret = encap_rcv(sk, skb);
    }
    
    udp_lib_checksum_complete(skb);
    return __udp_queue_rcv_skb(sk, skb)
    {
        rc = __udp_enqueue_schedule_skb(sk, skb)
        {
    
            rmem = atomic_read(&sk->sk_rmem_alloc);
            if (rmem > sk->sk_rcvbuf)
                goto drop;
    
            __skb_queue_tail(&sk->sk_receive_queue, skb);
            sk->sk_data_ready(sk);
            // == sock_def_readable()
        }
    }

} 
```
