# N2.3 Packet Steering (RSS and RPS) [OPTIONAL]

RSS and RPS are techniques that help with scaling packet processing across multiple CPUs. They allow distribution of packet processing across CPUs, while restricting a flow to a single CPU. i.e. each flow is assigned a CPU and flows are distributed across CPUs.

### N2.3.1 Packet flow hash

Firstly To identify packet flows, a hash is computed based on the following 4-tuple.
(source address, destination address, source port, destination port).
For certain protocols that do not support ports, a tuple containing just the source and destination addresses is used to compute the hash.
The hash is computed in `__skb_get_hash()`. After computing the hash, it is updated in skb->hash.
Some drivers have the hardware to offload hash computation, which is then set by the driver before passing the packet to the core networking layer.

### N2.3.2 RSS: Receive Side Scaling

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

