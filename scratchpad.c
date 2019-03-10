
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


	netif_rx_internal(skb) {
		cpu = get_rps_cpu(skb->dev, skb, &rflow);
		enqueue_to_backlog(skb, cpu) {
			sd = &per_cpu(softnet_data, cpu);	//get remote cpu sd
			__skb_queue_tail(&sd->input_pkt_queue, skb); //enqueue
			rps_ipi_queued(sd);		//add sd to rps_ipi_next
		}
	}

	net_rx_action() {
		unsigned long time_limit = jiffies +
			usecs_to_jiffies(netdev_budget_usecs);
		int budget = netdev_budget;

		budget -= napi_poll(n, &repoll) {
			work = n->poll(n, weight) // same as process_backlog
			process_backlog(n, weight) {
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

	__netif_receive_skb_core() {

		net_timestamp_check(!netdev_tstamp_prequeue, skb) {
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

	$ cat /proc/net/ptype
		Type Device      Function
		ALL           tpacket_rcv
		0800          ip_rcv
		0011          llc_rcv [llc]
		0004          llc_rcv [llc]
		0806          arp_rcv
		86dd          ipv6_rcv


	ip_rcv_core() {
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

		            transport layer (TCP/UDP)

	     ip_local_deliver_finish

		      INPUT                      OUTPUT
		        |                           |
		ROUTING DECISION --- FORWARDING --- +
		        |                           |
		    PREROUTING                 POSTROUTING
		        |                           |
	
		     ip_rcv

		             CORE NETWORKING

sock_def_readable()

	__udp4_lib_rcv() {
		uh   = udp_hdr(skb);
		if (udp4_csum_init(skb, uh, proto)) //pseudo ip csum
			goto csum_error;

		sk = __udp4_lib_lookup_skb(skb, uh->source, uh->dest, udptable);
		if (sk)
			return udp_unicast_rcv_skb(sk, skb, uh){
				ret = udp_queue_rcv_skb(sk, skb);
			}
	}


	udp_queue_rcv_skb(sk, skb) {
		struct udp_sock *up = udp_sk(sk);

		encap_rcv = READ_ONCE(up->encap_rcv);
		if (encap_rcv) {
			if (udp_lib_checksum_complete(skb))
				goto csum_error;
			ret = encap_rcv(sk, skb);
		}

		udp_lib_checksum_complete(skb);
		return __udp_queue_rcv_skb(sk, skb) {
			rc = __udp_enqueue_schedule_skb(sk, skb) {

				rmem = atomic_read(&sk->sk_rmem_alloc);
				if (rmem > sk->sk_rcvbuf)
					goto drop;

				__skb_queue_tail(&sk->sk_receive_queue, skb);
				sk->sk_data_ready(sk);
				// == sock_def_readable()
			}
		}
	}

