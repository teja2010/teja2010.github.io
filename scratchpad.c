
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

	SYSCALL_DEFINE3(sendmsg, int, fd, struct user_msghdr __user *, msg, unsigned int, flags)
	{
		return __sys_sendmsg(fd, msg, flags) {
			sock = sockfd_lookup_light(fd, &err, &fput_needed);

			err = ___sys_sendmsg(sock, msg, &msg_sys, flags) {

				//copy msg (usr mem) into msg_sys (kernel mem)
				err = copy_msghdr_from_user(msg_sys, msg);
				sock_sendmsg(sock, msg_sys) {
					int err = security_socket_sendmsg();
					if(err)
						return;

					sock_sendmsg_nosec(sock, msg_sys) {
						sock->ops->sendmsg(sock, msg_sys);
					}
				}
			}
		}
	}

int inet_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
	inet_autobind(sk)
	DECLARE_SOCKADDR(struct sockaddr_in *, usin, msg->msg_name);
	daddr = usin->sin_addr.s_addr;
	dport = usin->sin_port;

	rt = (struct rtable *)sk_dst_check(sk, 0);
	if (!rt) {
		flowi4_init_output();
		// pass daadr, dport...
		rt = ip_route_output_flow(net, fl4, sk);

		sk_dst_set(sk, dst_clone(&rt->dst));
		//next time sendmsg is called, sk_dst_check() will return the rt
	}

	saddr = fl4->saddr; // route lookup complete, saddr is known

	skb = ip_make_skb(sk, fl4, getfrag, msg, ulen,
			  sizeof(struct udphdr), &ipc, &rt,
			  &cork, msg->msg_flags);
	err = udp_send_skb(skb, fl4, &cork);
}

	a_function_call()
	{
		if( condition ) {
		}
	}

struct sk_buff *ip_make_skb()
{
	struct sk_buff_head queue;
	__skb_queue_head_init(&queue);

	err = __ip_append_data()
	{
		hh_len = LL_RESERVED_SPACE(rt->dst.dev);
		//hard header length: maximum space the driver will need to
		//fill in headers below the network header. i.e.
		//link local header (e.g. ethernet hdr for ethernet drivers)

		fragheaderlen = sizeof(struct iphdr) + (opt ? opt->optlen : 0);
		// opt is NULL, fragheaderlen is equal to sizeof(struct iphdr)

		datalen = length + fraggap; //fraggap is zero
		// length = udphdr len + payload length
		fraglen = datalen + fragheaderlen;

		alloclen = fraglen;
		skb = sock_alloc_send_skb(sk,
				alloclen + hh_len + 15,
				(flags & MSG_DONTWAIT), &err);
		// allocate space for (ip, udp headers & payload) plus
		// hard header len plus 15 (some tail room.)

		skb_reserve(skb, hh_len);  // -------------------------- Step 1
		data = skb_put(skb, fraglen + exthdrlen - pagedlen);
		// exthdrlen & pagedlen are zero.  --------------------- Step 2
		skb_set_network_header(skb, exthdrlen);
		skb->transport_header = (skb->network_header +
				fragheaderlen); // --------------------- Step 3

		data += fragheaderlen + exthdrlen; // ------------------ Step 4
		// move pointer to where payload starts
		copy = datalen - transhdrlen - fraggap - pagedlen;
		// amount of payload data that needs to be copied.
		// datalen = payload len + udp header len.
		// transhdrlen = udp header len

		getfrag(from, data + transhdrlen, offset, copy, fraggap, skb);
		// getfrag is set to ip_generic_getfrag()
		{	//copy and update csum
			csum_and_copy_from_iter_full(to, len, &csum, &msg->msg_iter);
			skb->csum = csum_block_add(skb->csum, csum, odd);
		}

		length -= copy + transhdrlen; // copied length is subtracted

		skb->sk = sk;
		__skb_queue_tail(queue, skb);

		refcount_add(wmem_alloc_delta, &sk->sk_wmem_alloc);
	}

	return __ip_make_skb(sk, fl4, &queue, cork)
	{
		skb = __skb_dequeue(queue);
		__skb_pull(skb, skb_network_offset(skb)); // ----------- Step 5

		iph = ip_hdr(skb);
		iph->version = 4;
		iph->ihl = 5;
		iph->tos = (cork->tos != -1) ? cork->tos : inet->tos;
		iph->frag_off = df;
		iph->ttl = ttl;
		iph->protocol = sk->sk_protocol;
		ip_copy_addrs(iph, fl4); // copy addresses from flow info
	}
}

int udp_send_skb()
{
	uh = udp_hdr(skb);
	uh->source = inet->inet_sport;
	uh->dest = fl4->fl4_dport;
	uh->len = htons(len);
	uh->check = 0;

	csum = udp_csum(skb);
	err = ip_send_skb(sock_net(sk), skb)
	{
		//ip_local_out calls
		__ip_local_out()
		{
			skb->protocol = htons(ETH_P_IP);
			//enter netfilter OUTPUT chain
		}
	}
}
	static int ip_finish_output2()
	{
		neigh = __ipv4_neigh_lookup_noref(dev, nexthop);
		neigh_output(neigh, skb)
		{
			hh_alen = HH_DATA_ALIGN(hh_len); //align hard header
			memcpy(skb->data - hh_alen, hh->hh_data,
					hh_alen);
		}
		__skb_push(skb, hh_len); // add the hh header
		return dev_queue_xmit(skb);
	}

		            transport layer (TCP/UDP)

					   __ip_local_out()

		      INPUT                      OUTPUT
		        |                           |
		ROUTING DECISION --- FORWARDING --- +
		        |                           |
		    PREROUTING                 POSTROUTING
		        |                           |
	
					   ip_finish_output()

		             CORE NETWORKING

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
	loopback_xmit()
	{
		skb_tx_timestamp(skb);
		skb_orphan(skb);    // remove all links to the skb.
		skb->protocol = eth_type_trans(skb, dev);	// set protocol

		netif_rx(skb);
	}
