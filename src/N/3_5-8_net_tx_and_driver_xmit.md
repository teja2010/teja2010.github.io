# N3.5-3.8 NET_TX and driver xmit

### N3.5 Core networking

​      The current section will cover the simplest case of sending out a packet.      NET_TX softirq will not be scheduled, in most cases packets will be sent      out this way.    

​      Every real network device on creation has atleast one TX queue (var "txq").      By real I mean actual physical devices: ethernet or wifi interfaces.      Virtual network devices like loopback, tun interfaces, etc are "queueless",      i.e. they have a txq and a default Queue Discipline (`qdisc`), but a function is not added to      enqueue packets.

Run "tc qdisc show dev lo", and it should show "noqueue" as the only queue. Other real devices have queues with  specific properties, as shown below eth0 has a `fq_codel` queue. I'll add a separate page for qdiscs, for now ignore them.    

```bash
$ tc qdisc show dev lo
qdisc noqueue 0: root refcnt 2

$ tc qdisc show dev eth0
qdisc fq_codel 0: root refcnt 2 limit 10240p flows 1024 quantum 1514 target 5.0ms
interval 100.0ms memory_limit 32Mb ecn 
```

A small optional section on how loopback's xmit works is added next. For now assume our device has a queue.

 `__dev_queue_xmit()` finds the tx queue and qdisc and if a enqueue function      is present, calls `__dev_xmit_skb()` which calls the function to enqueue the      skb into the `qdisc`. At this point the skb is in the queue. We move  forward without any skb pointer held. After enqueueing the skb,      `__qdisc_run()` is called to process packets (if possible) that have been      enqueued. If no other process needs the cpu and less than 64 packets      have been processed in the current context, `__qdisc_run()` will continue      processing packets. `__qdisc_run` calls `qdisc_restart()` internally, which      dequeues skbs from the queue and calls `sch_direct_xmit()`, which calls      `dev_hard_start_xmit()` and it finally calls `xmit_one()`. `xmit_one()` will transmit one skb from the queue. 

```c
__dev_queue_xmit()
{
    struct netdev_queue *txq;
    struct Qdisc *q;

    txq = netdev_pick_tx(dev, skb, sb_dev);
    q = rcu_dereference_bh(txq->qdisc);
    rc = __dev_xmit_skb(skb, q, dev, txq)
    {
        rc = q->enqueue(skb, q, &to_free) & NET_XMIT_MASK;
        __qdisc_run(q)
        {
            //while constraints allow
            qdisc_restart(q, &packets)
            {
                skb = dequeue_skb(q);
                sch_direct_xmit(skb);
            }
        }
        qdisc_run_end(q);
    }
} 
```



### N3.6 NET_TX (OPTIONAL)

Ironic that the article on NET_TX has this section marked as OPTIONAL. But      in simple scenarios, NET_TX softirq is almost never raised. After enqueueing      the packet,  `__qdisc_run()` cannot process packets because if one of these conditions is not met:

1. no other process is waiting for this CPU
2. the current process has enqueued less than 64 packets.

Then a NET_TX is scheduled to run. `__netif_schedule()` will raise a      NET_TX softirq if it was not already triggered on the CPU. `net_tx_action()`, the      registered function will run `__qdisc_run()` on the qdisc, after which the      flow is same as the case without a softirq raised.  

Though we have not covered NAPI yet, `net_tx_action()` is a dual of `net_rx_action()`,  a root `qdisc` is a dual of a `napi` structure, and `xmit_one()` is a dual of `__netif_receive_skb()`, with very similar logic but in opposite directions.

### N3.7 driver xmit_one()

​      `xmit_one()` is the final function, which like `__netif_receive_skb()` shares      the packet with all registered promiscuous packet_types, i.e. the      global list `ptype_all` and per netdevice list `dev->ptype_all`. This is done      within `dev_queue_xmit_nit()` function. After this, `xmit_one` calls      `netdev_start_xmit()` which internally calls `__netdev_start_xmit()` to      hand over the packet to the driver by calling `ops->ndo_start_xmit()`      (ndo stands for NetDevice Ops). A `net_device_ops` struct is registered      during netdevice creation, where this function pointer is set by the driver. The driver will then send the packet out via the physical interface.    

```c
sch_direct_xmit() -> dev_hard_start_xmit() -> xmit_one()
{
    dev_queue_xmit_nit();
    //deliver skb to promisc packet types

    netdev_start_xmit()
    {
        const struct net_device_ops *ops = dev->netdev_ops;
        return ops->ndo_start_xmit(skb, dev);
    }
} 
```

### N3.8 loopback xmit (OPTIONAL)

​      Like described earlier, each device during creation registers certain      functions using the `net_device_ops` structure. Loopback registers      `loopback_xmit` as the function. On sending a packet to loopback, when      `ops->ndo_start_xmit` is called, the packet enters `loopback_xmit()`. It is      a very simple function, which increments stats and calls `netif_rx_ni()`      to begin the RX path of the packet. The end of TX coincides with the start of RX in this function. 

​      The loopback device is described in `drivers/net/loopback.c` .    

```c
loopback_xmit()
{
    skb_tx_timestamp(skb);
    skb_orphan(skb);    // remove all links to the skb.
    skb->protocol = eth_type_trans(skb, dev);    // set protocol

    netif_rx(skb);    //RX!!
} 
```