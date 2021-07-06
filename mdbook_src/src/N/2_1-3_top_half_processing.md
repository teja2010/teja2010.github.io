# N2.1-N2.2 Top Half Processing

### N2.1 Enter the Core, top half processing

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

### N2.2 Softirqs, Softirq Scheduling [OPTIONAL]

One detail that I have ignored in the previous discussion is to specify which CPU the top and bottom half actually run.

The top half runs in the hardware interrupt (IRQ) context, which is a kernel thread which can be on any CPU. (This description is not really true, I have a separate page planned on interrupts where this will be described in more detail. Till then this way of visualizing it is not really wrong). Say it runs on CPU0, i.e. netif_rx() is called on CPU0. The packet will be enqueued onto CPU0's input_packet_queue. The kernel thread will then schedule softirq processing on CPU0 and exit. The bottom half will also run on the same CPU. This section describes how the bottom half is scheduled by the top half and how softirq begins.

Just Google what hard interrupts (IRQ) and soft interrupts (softirq) are for some background. Hardware interrupts (IRQs) will stop all processing. For interrupts that take very long to run, some work is done in IRQ and the rest is done in a softirq. Packet processing is one such task that takes very long to complete so NET_RX is the corresponding soft interrupt which takes care of packet processing.

Linux currently has the following softirqs declared in `include/linux/interrupt.h`

```c
enum
{
    HI_SOFTIRQ=0,
    TIMER_SOFTIRQ,
    NET_TX_SOFTIRQ,
    NET_RX_SOFTIRQ,
    BLOCK_SOFTIRQ,
    IRQ_POLL_SOFTIRQ,
    TASKLET_SOFTIRQ,
    SCHED_SOFTIRQ,
    HRTIMER_SOFTIRQ, /* Unused */
    RCU_SOFTIRQ,

    NR_SOFTIRQS

}; 
```



The names are mostly indicative of the subsystem each softirq serves. During kernel initialization, a function is registered for each of these softirqs. When softirq processing is needed, this function is called.
For example after core networking init is done, `net_dev_init()` registers `net_rx_action` and `net_tx_action` as the functions corresponding to `NET_RX` and `NET_TX`.

```c
  open_softirq(NET_TX_SOFTIRQ, net_tx_action);
  open_softirq(NET_RX_SOFTIRQ, net_rx_action); 
```


A softirq is scheduled by calling `raise_softirq()`, which internally disables irqs and calls `__raise_softirq_irqoff()`. This function sets a bit corresponding to the softirq in the percpu bitmask `irq_stat.__softirq_pending`. The bitmask is used to track all pending softirqs on the CPU. This operation must be done with all interrupts are disabled on the CPU. Without interrupts disabled, the same bitmask can be overwritten by another interrupt handler, undoing the current change.

Grepping for `ksoftirqd` in ps, should show multiple threads, one for each CPU. These are threads spawned during init to process pending softirqs on each of the CPUs. Periodically the scheduler will allow ksoftirqd to run, and if any of the bits are set, it's registered function is called.

During packet RX, `net_rx_action()` is called.

The CFS scheduler makes sure that all threads get their fair share of CPU time. So ksoftirqd and an application thread will both get their fair share. But, during softirq processing, ksoftirqd disables all irqs and the scheduler has no way of interrupting the thread. Hence all registered functions are have checks to prevent the ksoftirqd from high-jacking the CPU for too long.
In this case, a buggy `net_rx_action()` function may be able to push packets into the socket queue but the application never will never get a chance to actually read the packets.

To conclude, `netif_rx()` calls `__raise_softirq_irqoff(NET_RX_SOFTIRQ)` to schedule softirq processing on the current CPU. The ksoftirqd running on the current CPU will check the bitmask, since `NET_RX_SOFTIRQ` is pending will call `net_rx_action()`

#### N2.2.1 Run Softirq Now

Other than softirq being scheduled by the scheduler, it is sometimes necessary to kick start processing. For example when a sudden burst of packets arrive, due to delays in softirq processing, packets might be dropped. In such cases, when a burst is detected, kick-starting packet processing is helpful. In the above example, calling `netif_rx_ni()` will kick start packet processing, in addition to enqueuing the packet .

### N2.3 Packet Steering (RSS and RPS) [OPTIONAL]

RSS and RPS are techniques that help with scaling packet processing across multiple CPUs. They allow distribution of packet processing across CPUs, while restricting a flow to a single CPU. i.e. each flow is assigned a CPU and flows are distributed across CPUs.

#### N2.3.1 Packet flow hash

Firstly To identify packet flows, a hash is computed based on the following 4-tuple.
(source address, destination address, source port, destination port).
For certain protocols that do not support ports, a tuple containing just the source and destination addresses is used to compute the hash.
The hash is computed in `__skb_get_hash()`. After computing the hash, it is updated in skb->hash.
Some drivers have the hardware to offload hash computation, which is then set by the driver before passing the packet to the core networking layer.

#### N2.3.2 RSS: Receive Side Scaling

RSS acheives packet steering by configuring receive queues (usually one for each CPU), and by configuring seperate interrupts for each queue and pinning the interrupts to the specific CPU. On receiving a packet, based on it's hash the packet is put in the right queue and the corresponding interrupt is raised.

### N2.3.3 RPS: Receive Packet Steering

RPS is RSS in software. While pushing the packet into the core network through `netif_rx()` or `netif_receive_skb()`, a CPU is chosen for the packet based on the `skb->hash`. The packet is then enqueued into the target CPU's input_packet_queue. Since the operation must have all interrupts disabled, a softirq cannot be directly scheduled on different core. So an Inter Processor Interrupt is used to schedule softirq processing on the other core.

After RSS decides to put the packet on a remote core, in the `rps_ipi_queued()` function, the target CPU's softnet struct is added to the current core's `sd->rps_ipi_next` which is a list to softnet structs for which an IPI has to be sent. During the current core's softirq processing, all the accumulated IPIs are sent to those cores by traversing the rps_ipi_next list.

IPIs are actually sent by scheduling a job on a remote core by  calling `smp_call_function_single_async()` during NET_RX processing.

```c
netif_rx_internal(skb)
{
    cpu = get_rps_cpu(skb->dev, skb, &rflow);
    enqueue_to_backlog(skb, cpu)
    {
        sd = &per_cpu(softnet_data, cpu);    //get remote cpu sd
        __skb_queue_tail(&sd->input_pkt_queue, skb); //enqueue
        rps_ipi_queued(sd);        //add sd to rps_ipi_next
    }
} 
```


Kernel documentation describes these methods and also provides instructions to configure them.
