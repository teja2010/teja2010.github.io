# N2.7 UDP layer

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
