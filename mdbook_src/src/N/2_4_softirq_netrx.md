# N2.4 Softirq processing NET_RX

Softirq processing was scheduled by the bottom half, and NET_RX begins. All of NET_RX processing is done in `net_rx_action()`. The logic is to process packets until one of the following events occurs:
1. The packet queue is empty. In this case NET_RX softirq stops.
2. NET_RX has been running for longer than `netdev_budget_usecs`, whose default value is 2 milliseconds.
3. NET_RX has processed more than `netdev_budget` (fixed value of 300) packets. (We will revist this constraint while looking at NAPI)

In cases 2 and 3, there still might be packets to process, in which case NET_RX will schedule itself before exiting, (i.e. set the NET_RX_SOFTIRQ bit before exiting), so it can process some more packets in another session. In cases 2 and 3 NET_RX processing is almost at it's limits. To indicate this `sd->time_squeeze` is incremented, so that a few parameters can be tuned. We will revisit this while discussing NAPI.

Softirq processing is done with elevated privilages, which can easily cause it to high-jack the complete CPU. The above constraints are to make sure that softirq processing allows the applications run. If the softirq were to high-jack the CPU, the user application would never run, and the end user would see applications not responding.

The actual function that dequeues packets from `input_pkt_queue` and begins processing them is `process_backlog()`. After dequeueing the packet it calls `__netif_receive_skb()` which pushes the packet up into the protocol stacks.

For now ignore the napi part of `net_rx_action()`, it calls `napi_poll()` which will call the registered poll function `n->poll()`. The poll function is set to process_backlog. For now believe me even if it does not make much sense. It will make sense one we look at the NAPI framework.

```c
net_rx_action()
{
    unsigned long time_limit = jiffies +
                               usecs_to_jiffies(netdev_budget_usecs);
    int budget = netdev_budget;

    budget -= napi_poll(n, &repoll)
    {
        work = n->poll(n, weight) // same as process_backlog
        process_backlog(n, weight)
        {
            skb_queue_splice_tail_init(&sd->input_pkt_queue,
                                       &sd->process_queue);
            while ((skb = __skb_dequeue(&sd->process_queue)))
                __netif_receive_skb(skb);
        }
    }
    // time and budget constraints
    if (unlikely(budget <= 0 ||
                 time_after_eq(jiffies, time_limit))) {
        sd->time_squeeze++;
        break;
    }
} 
```

