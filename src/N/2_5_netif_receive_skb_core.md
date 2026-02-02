# N2.5 `__netif_receive_skb_core()`

`__netif_receive_skb()` internally calls `__netif_receive_skb_core()` for the main packet processing. `__netif_receive_skb_core()` is large function which handles multiple ways in which a packet can be processed. This section tries to cover some of them.

1. skb timestamp:
`skb->tstamp` field is filled with the time at which the kernel began processing the packet. This information is used in various protocol stacks. One example is it's usage by AF_PACKET, which is the protocol tools like wireshark use to collect packet dumps. AF_PACKET extracts the timestamp from `skb->tstamp` and provides to userspace as part of struct tpacket_uhdr. This timestamp is the one that wireshark reports as the time at which the packet was received.

2. Increment softnet stat:
`sd->processed` is incremented, which is representative of the number of packets that were processed on a particular core. The packets might be dropped by the kernel for various reasons later, but they were processed on a particular core.

3. packet types:
    At this point the packet is sent to all modules that want to process packets. The list of packet types that the kernel supports is defined in `include/linux/netdevice.h`, just above PTYPE_HASH_SIZE macro definition. Other than the ones described above, promiscuous packet types (processes packets irrespective of protocol) like AF_PACKET and custom packet_types added by various drivers and subsystems are all supported. Each of them fill up a packet_type structure and register it by calling `dev_add_pack()`. Based on the type and netdevice the struct is added to the respective packet_type list. `__netif_receive_skb()` based on the skb's protocol and netdevice traverses the particular list, delivering the packet by calling `packet_type->func()`.
    All registered packet_types can be seen at `/proc/net/ptype`.

  ```bash
  $ cat /proc/net/ptype
  
  Type Device      Function
  ALL           tpacket_rcv
  0800          ip_rcv
  0011          llc_rcv [llc]
  0004          llc_rcv [llc]
  0806          arp_rcv
  86dd          ipv6_rcv 
  ```

  The ptype lists are described below:

  1. `ptype_all`: It is a global variable containing promiscuous packet_types irrespective of netdevice. Each AF_PACKET socket adds a packet_type to this list. packet_rcv() is called to pass the packet to userspace.

  2. `skb->dev->ptype_all`: Per netdevice list containing promiscuous packet_types specific to the netdevice. TODO: find an example.

  3. `ptype_base`: It is a global hash table, with key as the last 16bits of `packet_type->type` and value as a list of packet_type. For example `ip_packet_type` will be added to ptype_base[00], with `ptype->func` set to ip_rcv. While traversing, based on `skb->protocol` 's last 16bits, a list is chosen and the packet is delivered to all packet_types whose type matches skb->protocol.

  4. `skb->dev->ptype_specific`: Per netdevice list containing protocol specific packet types. The packet is delivered to if the skb->protocol matches ptype_type. Mellanox for example adds a `packet_type` with type set to `ETH_P_IP`, to process all UDP packets received by the driver. See `mlx5e_test_loopback()`. The name suggests some sort of loopback test. I am not really sure how. IDK.

     One important detail is that the same packet will be sent to all applicable packet_type-s. Before delivering the skb, `skb->users` is incremented. `skb->users` is the number users that are (ONLY) reading the packet. Each module after completing necessary processing call `kfree_skb()`, which will first decrement users, and then free the skb only if skb->users hits zero. So the same skb pointer is shared by all the modules, and the last user will free the skb.

4. RX handler:
   Drivers can register a rx handler, which will be called if a packet is received on the device. The `rx_handler` can return values based on which packet processing can stop or continue. If RX_HANDLER_CONSUMED is returned, the driver has completed processing the packet and `__netif_receive_skb_core()` can stop processing further. If RX_HANDLER_PASS is returned, skb processing will continue. The other values supported and ways to register/unregister a rx handler are available in `include/linux/netdevice.h` , above enum rx_handler_result.
   For example if the driver wants to support a custom protocol header over IP, a rx handler can be registered which will process the outer header and return RX_HANDLER_PASS. Futher IP processing can continue when the packet is delivered to ip_packet_type. Note that the packet dumps collected will still contain the custom header. (It is actually better to return RX_HANDLER_CONSUMED and enqueue the packet by calling netif_receive_skb. This will allow the driver to run the packet through GRO offload engine and to distribute packet processing with RPS. Ignore this comment for now.)

5. Ingress Qdisc processing. We will look at it in a different page, after we have looking at Qdiscs and TX. Similar to RX handler, certain registered functions run on the packet and based on the return value, the processing can stop or continue. But unlike a RX handler, the functions to run are added from userspace.

The order in which the `__netif_receive_skb_core()` delivers (if applicable) the packets is:

- Promiscuous packet_type
- Ingress qdisc
- RX handler
- Protocol specific packet_type

Finally if if none of them consume the packet, the packet is dropped and netdevice stats are incremented.

```c
__netif_receive_skb_core()
{
    net_timestamp_check(!netdev_tstamp_prequeue, skb)
    {
        __net_timestamp(SKB);
    }

    __this_cpu_inc(softnet_data.processed);
    
    //Promiscuous packet_type
    list_for_each_entry_rcu(ptype, &ptype_all, list) {
        if (pt_prev)
            ret = deliver_skb(skb, pt_prev, orig_dev);
        pt_prev = ptype;
    }
    list_for_each_entry_rcu(ptype, &skb->dev->ptype_all, list) {
        if (pt_prev)
            ret = deliver_skb(skb, pt_prev, orig_dev);
        pt_prev = ptype;
    }
    
    //Ingress qdisc
    skb = sch_handle_ingress(skb, &pt_prev, &ret, orig_dev);
    
    //RX handler
    rx_handler = rcu_dereference(skb->dev->rx_handler);
    switch (rx_handler(&skb)) {
    case RX_HANDLER_CONSUMED:
        ret = NET_RX_SUCCESS;
        goto out;
    case RX_HANDLER_PASS:
        break;
    default:
        BUG();
    }
    
    //Protocol specific packet_type
    deliver_ptype_list_skb(skb, &pt_prev, orig_dev, type,
                           &ptype_base[ntohs(type) & PTYPE_HASH_MASK]);
    deliver_ptype_list_skb(skb, &pt_prev, orig_dev, type,
                           &skb->dev->ptype_specific);

} 
```
