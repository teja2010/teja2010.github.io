
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

	SERVER						CLIENT

	fd1 = socket()
	listen(fd1, N)
	fd3 = accept(fd1) {
							fd2 = socket()
							connect(fd2) {
				<<---- SYN ------
				---- SYN-ACK --->>	}
	}			<<---- ACK ------
	send(fd3, DATA)
				----- DATA --->>
				<<--- DATA -----
							recv(fd2, DATA);
							close(fd2);
				<<---- FIN ------
	close(fd3);		---- FIN-ACK -->>
				<<---- ACK ------




__sys_socket() -> sock_create()
{
	sock_create() -> __sock_create()
	{
		sock = sock_alloc();
		sock->type = type;
		pf = rcu_dereference(net_families[family]);
		err = pf->create(net, sock, protocol, kern);
		{
			struct inet_protosw *answer;
			struct proto *answer_prot;
			list_for_each_entry_rcu(answer, &inetsw[sock->type], list) {
				//find the right protocol.
				if (protocol == answer->protocol) {
					break;
				}
			}

			sock->ops = answer->ops;	// &inet_stream_ops
			answer_prot = answer->prot;	// tcp_prot
			sk = sk_alloc(net, PF_INET, GFP_KERNEL, answer_prot, kern);

			sock_init_data(sock, sk);
			sk->sk_protocol	   = protocol;

			sk_refcnt_debug_inc(sk);

			err = sk->sk_prot->init(sk);

		}

	}

	return sock_map_fd(sock, flags & (O_CLOEXEC | O_NONBLOCK));
	// find a unused fd and bind it to the socket

}



	int __sys_bind(fd, sockaddr, int)
	{
		sock = sockfd_lookup_light(fd, &err, &fput_needed);
		
		sock->ops->bind(sock, sockaddr, addrlen);	// inet_bind()
		{
			struct sock sk = sock->sk
			return __inet_bind(sk, uaddr, addr_len)
			{
				struct inet_sock *inet = inet_sk(sk);
				lock_sock(sk);	//lock sock

				snum = ntohs(addr->sin_port);
				if (sk->sk_prot->get_port(sk, snum))
				//	inet_csk_get_port()
				{
					err = -EADDRINUSE;
					return err;
				}
				inet->inet_sport = htons(inet->inet_num);
				// set src port

				release_sock(sk);	//unlock
			}
		}
	}

	/* check tables if the port is free */
	inet_csk_get_port(sk, port)
	{
		struct inet_hashinfo *hinfo = sk->sk_prot->h.hashinfo;	// hash tables
		struct inet_bind_hashbucket *head;			// a bucket
		struct inet_bind_bucket *tb = NULL;
		int ret = 1;

		if (!port) {
			head = inet_csk_find_open_port(sk, &tb, &port);
			// port is zero, assign a unused port
		}

		head = &hinfo->bhash[inet_bhashfn(net, port,
						  hinfo->bhash_size)];
		//calc hash and find the right bucket

		spin_lock_bh(&head->lock);
		inet_bind_bucket_for_each(tb, &head->chain)	//search in the bucket
			if (tb->port == port)
				goto tb_found;

		// if nothing is found, this is the first sock to use the port
		tb = inet_bind_bucket_create(hinfo->bind_bucket_cachep,
					     net, head, port);

	tb_found:
		if (!hlist_empty(&tb->owners)) {	// port is being used
			if (sk->sk_reuse == SK_FORCE_REUSE)
				goto success;

			// see the explanation above inet_bind_bucket def by DaveM,
			// which expains the below function's checks
			if (inet_csk_bind_conflict(sk, tb, true, true))
				goto fail_unlock;
		}
	success:
		if (!inet_csk(sk)->icsk_bind_hash)
			inet_bind_hash(sk, tb, port)
			{
				inet_sk(sk)->inet_num = snum;
				sk_add_bind_node(sk, &tb->owners);
				// add sk to bucket

				inet_csk(sk)->icsk_bind_hash = tb; // update pointer
			}
		ret = 0;

	fail_unlock;
		spin_unlock_bh(&head->lock);
		return ret;
	}

 *	1) Sockets bound to different interfaces may share a local port.
 *	   Failing that, goto test 2.
 *	2) If all sockets have sk->sk_reuse set, and none of them are in
 *	   TCP_LISTEN state, the port may be shared.
 *	   Failing that, goto test 3.
 *	3) If all sockets are bound to a specific inet_sk(sk)->rcv_saddr local
 *	   address, and none of them are the same, the port may be
 *	   shared.
 *	   Failing this, the port cannot be shared.

int __sys_listen(int fd, int backlog)
{
	sock = sockfd_lookup_light(fd, &err, &fput_needed);

	if ((unsigned int)backlog > somaxconn)
		backlog = somaxconn;
	err = sock->ops->listen(sock, backlog);
	// inet_listen()
	{
		struct sock *sk = sock->sk;
		old_state = sk->sk_state;
		if (old_state != TCP_LISTEN) {
			err = inet_csk_listen_start(sk, backlog);
			{
				reqsk_queue_alloc(&icsk->icsk_accept_queue);

				sk->sk_max_ack_backlog = backlog;
				sk->sk_ack_backlog = 0;
				inet_sk_state_store(sk, TCP_LISTEN);
				if (!sk->sk_prot->get_port(sk, inet->inet_num)) {
					// again inet_csk_get_port(), check with tables. Why?
					inet->inet_sport = htons(inet->inet_num);

					sk_dst_reset(sk);
					err = sk->sk_prot->hash(sk);
					//__inet_hash()

					if (likely(!err))
						return 0;
				}
			}
		}
		sk->sk_max_ack_backlog = backlog;
	}
}

int __sys_connect(int fd, struct sockaddr __user *uservaddr, int addrlen)
{
	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	err = sock->ops->connect(sock, (struct sockaddr *)&address, addrlen,
				 sock->file->f_flags);
	// inet_stream_connect()
	{
		err = __inet_stream_connect(sock, uaddr, addr_len, flags, 0);
		switch (sock->state) {
		case SS_UNCONNECTED:
			err = sk->sk_prot->connect(sk, uaddr, addr_len);
			// tcp_v4_connect()
			sock->state = SS_CONNECTING;
		}

		timeo = sock_sndtimeo(sk, flags & O_NONBLOCK);
		// how long, sock has to wait for the response.

		inet_wait_for_connect(sk, timeo, writebias); //sleep for atmost timeo jiffies

		//... to be continued


int tcp_v4_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	tcp_set_state(sk, TCP_SYN_SENT);

	rt = ip_route_connect(fl4, nexthop, inet->inet_saddr,
			      RT_CONN_FLAGS(sk), sk->sk_bound_dev_if,
			      IPPROTO_TCP,
			      orig_sport, orig_dport, sk);
	sk_setup_caps(sk, &rt->dst);
	{
		sk_dst_set(sk, dst);	// set the route
	}

	err = tcp_connect(sk)
	{
		buff = sk_stream_alloc_skb(sk, 0, sk->sk_allocation, true);

		tcp_rbtree_insert(&sk->tcp_rtx_queue, buff);	// to retransmit later

		// send the skb
		return tcp_transmit_skb() {
			//build the tcp header
			th->source		= inet->inet_sport;
			th->dest		= inet->inet_dport;
			th->seq			= htonl(tcb->seq);
			th->ack_seq		= htonl(rcv_nxt);

			err = icsk->icsk_af_ops->queue_xmit(sk, skb, &inet->cork.fl);
			//ip_queue_xmit().
		}

		/* Timer for retransmitting the SYN until a SYN-ACK is rcvd. */
		inet_csk_reset_xmit_timer(sk, ICSK_TIME_RETRANS,
					  inet_csk(sk)->icsk_rto, TCP_RTO_MAX);
		// See tcp_retransmit_timer(), which will be called on timeout.
		// it retransmits the skb returned by tcp_rtx_queue_head() on timeout.
	}
}


int tcp_v4_rcv(struct sk_buff *skb)
{
	th = (const struct tcphdr *)skb->data;

	sk = __inet_lookup_skb(&tcp_hashinfo, skb, __tcp_hdrlen(th), th->source,
			       th->dest, sdif, &refcounted);
	{
		sk = __inet_lookup_established(net, hashinfo, saddr, sport,
				daddr, hnum, dif, sdif);
		/* look up established sk table */

		return __inet_lookup_listener(net, hashinfo, skb, doff, saddr,
				sport, daddr, hnum, dif, sdif);
		/* look up listening sk table */
	}

	if(sk->sk_state == TCP_TIME_WAIT)
		//handle time wait. explained in the section in this page

	tcp_v4_do_rcv(sk, skb);
	{
		if (sk->sk_state == TCP_ESTABLISHED) { /* Fast path */
			tcp_rcv_established(sk, skb);
			return 0;
		}

		/* To handle all states except TCP_ESTABLISHED & TIME_WAIT */
		tcp_rcv_state_process(sk, skb);
	}
}


int tcp_rcv_state_process(struct sock *sk, struct sk_buff *skb)
{
	//case TCP_LISTEN:
	acceptable = icsk->icsk_af_ops->conn_request(sk, skb) >= 0;
	//tcp_v4_conn_request()
	{
		struct request_sock *req;
		if (sk_acceptq_is_full(sk)) {
			NET_INC_STATS(sock_net(sk), LINUX_MIB_LISTENOVERFLOWS);
			goto drop;
		}
		req = inet_reqsk_alloc(rsk_ops, sk, !want_cookie);
		// allocate a tcp_request_sock

		tcp_openreq_init(req, &tmp_opt, skb, sk);
		af_ops->init_req(req, sk, skb);
		// tcp_v4_init_req() : init req, copy

		inet_csk_reqsk_queue_hash_add(sk, req,
				tcp_timeout_init((struct sock *)req));
		{
			reqsk_queue_hash_req(req, timeout);
			{
				timer_setup(&req->rsk_timer, reqsk_timer_handler,
						TIMER_PINNED);
				mod_timer(&req->rsk_timer, jiffies + timeout);

				inet_ehash_insert(req_to_sk(req), NULL);
			}

			inet_csk_reqsk_queue_added(sk);
			// increment icsk_accept_queue length
		}

		af_ops->send_synack(sk, dst, &fl, req, &foc,
				    !want_cookie ? TCP_SYNACK_NORMAL :
						   TCP_SYNACK_COOKIE);
		{
			skb = tcp_make_synack(sk, dst, req, foc, synack_type);
			err = ip_build_and_send_pkt(skb, sk, ireq->ir_loc_addr,
					ireq->ir_rmt_addr,
					rcu_dereference(ireq->ireq_opt));
		}

		//req sk state ?
	}

}

int tcp_rcv_state_process(struct sock *sk, struct sk_buff *skb)
{
	//case TCP_SYN_SENT:
	queued = tcp_rcv_synsent_state_process(sk, skb, th);
	{
		tp->rcv_nxt = TCP_SKB_CB(skb)->seq + 1;
		tp->rcv_wup = TCP_SKB_CB(skb)->seq + 1;
		tcp_finish_connect(sk, skb);
		{
			tcp_set_state(sk, TCP_ESTABLISHED);

			if (!sock_flag(sk, SOCK_DEAD)) {
				sk->sk_state_change(sk); //sock_def_wakeup()
				{
					wake_up_interruptible_all(&wq->wait);
					// wakeup the socket waiting on connect()
				}
			}
		}
		tcp_send_ack(sk);
		return -1;
	}
}


		inet_wait_for_connect(sk, timeo, writebias);
		
		/* Connection was closed by RST, timeout, ICMP error
		 * or another process disconnected us.
		 */
		if (sk->sk_state == TCP_CLOSE)
			goto sock_error;

		sock->state = SS_CONNECTED;
	} // inet_stream_connect
} // __sys_connect














