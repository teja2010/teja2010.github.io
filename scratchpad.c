
                                             Application
 ------------------------------------------^-------------------
|     SOCKET LAYER                         |             |     |
 ------------------------------------------^-------------v------
|     PROTOCOL STACKS (IP, UDP, ...)       |             |     |
 ------------------------------------------^-------------v-----
|     CORE NETWORKING                      | RX       TX |     |
 ------------------------------------------^-------------v-----
|          DRIVER CODE                     |             |     |
 --------------------------------------------------------v-----
                                               Network















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
