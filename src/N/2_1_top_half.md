# N2.1 Enter the Core, top half processing

We assume that the driver has already picked up the packet data and has created a skb. (We will look at how drivers create skbs in the page describing NAPI). This packet needs to be sent up to the core. The kernel provides two simple function calls to do this.

```
netif_rx()
netif_rx_ni() 
```

netif_rx() does two things,

1. Enqueue the packet into a queue which contains packets that need processing.
The kernel maintains a softnet_data structure for each CPU. It is the core structure that facilitates network processing. Each softnet_data struct contains a "input_pkt_queue" into which packets that need to be processed will be enqueued. This queue is protected by a spinlock that is part of the queue (calls to rps_lock() and rps_unlock() are to lock/unlock the spinlock). The input_pkt_queue is of type sk_buff_head, which is used within by kernel to represent skb lists.
Before enqueuing, if the queue length is more than `netdev_max_backlog` (whose default length is 1000), the packet is dropped. This value can be modified by changing `/proc/sys/net/core/netdev_max_backlog`.
For each each packet drop sd->dropped is incremented. Certain numbers are maintained by softnet_data, I'll add a page describing the struct. TODO

2. After successfully enqueueing the packet, netif_rx schedules softirq processing if it has not already been scheduled. The next section describes how softirq is scheduled.

Parts of the code have been added below. All the core networking functions are described in `net/core/dev.c`.

```c
netif_rx()
{
    netif_rx_internal()
    {
        enqueue_to_backlog()
        {
            // checks on queue length
            rps_lock(sd);
            __skb_queue_tail(&sd->input_pkt_queue, skb);
            rps_unlock(sd);

            ____napi_schedule(sd, &sd->backlog)
            {
                // if not scheduled already schedule softirq
                __raise_softirq_irqoff(NET_RX_SOFTIRQ);
            }
        }
    }

}
```



netif_rx() and netif_rx_ni() are very similar except the later additionally begins softirq processing immediately, which will be explained in subsequent section.

This ends the top half processing, bottom half was scheduled, which will undertake rest of the packet processing.


