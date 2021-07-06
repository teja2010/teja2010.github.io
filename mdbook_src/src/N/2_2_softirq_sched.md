# N2.2 Softirqs, Softirq Scheduling [OPTIONAL]

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


