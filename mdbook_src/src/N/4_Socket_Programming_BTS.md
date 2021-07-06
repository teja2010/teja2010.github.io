# N4. (WIP) Socket Programming BTS

### THIS IS A WORK IN PROGRESS, Parts of it may be incomplete or incorrect.

----



​    This page describes the magic that happens in the kernel behind the scenes    (BTS) while running a server and client exchanging data over TCP. A small    introduction with the basics of TCP has been added, a few optional sections    describing the internal structure of networking sockets, socket memory accounting,    time wait sockets have been added. The TCP subsystem is not described, it    requires a dedicated article, this article only mentions the basic functions    where necessary.
​    Hereon the application sending the data is the server and one receiving it    is the client. Ofcourse, both the applications can be server and client by    sending and receiving simultaneously.  

### N4.1 Basics of TCP

​    Please skip this section if you have a fair idea how basic TCP operates.  

​    TCP provides reliable, ordered and error-checked delivery of octets.    The server divides the data into smaller segments and assigns each of them a    sequence number and sends it out. The client sends back an acknowledgement    for each sequence received. Reliability is achieved by tracking acknowledgements    and retransmitting segments if necessary. The receiver re-assembles the packets    using the sequence numbers so the application receives it in the right order.    Finally checksum is used to verify the integrity of the data. The application    actually knows nothing of how the packets are sent and received, the kernel    works all the TCP magic in the background. (Which is why this is a Behind    The Scenes Article).  

​    Before TCP begins transmitting the data, the server and client connect to    each other and exchange a few parameters. The server begins by binding to a    particular port. The client sends a request to the server, a SYN (short for    synchronize) packet. (A tcp packet with the SYN flag set in the header is    called a SYN packet).    The server responds with a SYN packet which also    acknowledges the packet sent by the client, i.e. SYN-ACK packet (ACK is short    for acknowledgement). The client on receiving the SYN-ACK acknowledges the    SYN sent by the server with an ACK. The exchange of these three packets    ( SYN, SYN-ACK and ACK) establishes a connection between the server and    the client. The connection on both the sides is uniquely identified by    the following four tuple:
​    (saddr, daddr, sport, dport) which are short for
​    ( source IP address, destination IP address, source    port, destination port) respectively.  

​    Connection termination also happens with the exchange of packets between    the server and client. A FIN is sent by the initiator, to which the peer    responds with a FIN-ACK (acknowledging the initiator's FIN). The connection    closes with the initiator acknowledging the FIN-ACK.  

```
        SERVER                                            CLIENT

        fd1 = socket()
        listen(fd1, N)
        fd3 = accept(fd1) {
                                                        fd2 = socket()
                                                        connect(fd2) {
                                <<---- SYN ------
                                ---- SYN-ACK --->>      }
        }                        <<---- ACK ------
        send(fd3, DATA)
                                ----- DATA --->>
                                <<--- DATA -----
                                                        recv(fd2, DATA);
                                                        close(fd2);
                                <<---- FIN ------
        close(fd3);                ---- FIN-ACK -->>
                                <<---- ACK ------
  
```

### N4.2 socket(int domain, int type, int protocol

​    Both applications create networking sockets using the socket() system call.    The arguments are socket family, socket type and protocol type. The    [man page](http://www.man7.org/linux/man-pages/man2/socket.2.html)    describes possible values the arguments take. AF_INET and AF_INET6 are to    create a IPv4 and IPv6 sockets respectively. Some of other options are to    create UNIX sockets for Inter process communication, NETLINK sockets for    monitoring kernel events and communicating with kernel modules,    AF_PACKET sockets to capture packets,    AF_PPPOX sockets for creating PPP tunnels, etc.  

​    There are two major Socket types:

1. SOCK_STREAM: used to send/recv octet streams.    For example TCP socket is a STREAM sock. Stream sockets guarantee reliable    in order delivery after a two way connection is established.    It does not preserve message boundaries. i.e. if the server    writes 40 bytes first and then 60 bytes, the client may receive all the    100 bytes in one shot, never knowing that the server wrote it in two parts.    Usually both sides agree upon a fixed boundary that is used to detect message    boundaries.    For example HTTP, which operates over TCP uses "\r\n" (CRLF: Carriage Return    Line Feed) as a boundary. See the  [wiki](https://en.wikipedia.org/wiki/Hypertext_Transfer_Protocol#Message_format) page describing the HTTP Message format.
2. SOCK_DGRAM: Datagram are the exact opposite of SOCK_STREAM, they are    connectionless, delivery is unreliable. But message boundaries are preserved,    i.e. in the prev example, the client would receive two messages 40 and 60    bytes long (if they were not dropped on the way). An example is UDP.  

​    Protocol field, usually zero, is used if there are multiple protocols for a    specific (family, protocol) set. For example both SCTP and TCP both offer    SOCK_STREAM services within AF_INET family, UDP and ICMP offer DGRAM services    within the AF_INET family. TCP is the default option, so a socket call with    family AF_INET, type STREAM_SOCK and protocol set to zero will initialize a    TCP socket.    Providing IPPROTO_SCTP instead of zero will create a SCTP socket.    Explicitly setting IPPROTO_TCP will also create a TCP.  

​    The socket() system call internally calls `__sys_socket()`, which has two parts    to it, firstly it creates a socket and a networking socket (struct sock) and    initializes them.    The second part is to create provide a fd to the application as a handle to    the socket.    `__sys_socket()` first calls    sock_create(), which internally calls `__sock_create()`. `__sock_create()` checks    the input values. It then checks if the application has permissions needed    to create the socket. For example packet sockets can be created only by    applications with admin privileges. Simple sockets like TCP & UDP dont need    any special permissions. Next it allocates a socket. Based on the family,    the corresponding create function is called. All protocol families are registered    during initialization by calling sock_register(). They can be accessed via    the global variable net_families[]. In this case,    family AF_INET has the structure inet_family_ops registered, and the create    function inet_create() is called.  

​    inet_create() searches among the registered struct proto which corresponds to    the protocol input. On finding the protocol sk_alloc() is called to create    the corresponding protocol sock, and the registered protocol init is called,    in this tcp_v4_init_sock(). The structure of the sock is described below,    an optional section. The socket is a BSD socket, while the sock is a networking    socket which handles all the protocol functionality and stores the corresponding    data. For example, in TCP the tcp sock maintains queues to track packets that    have been sent but not yet acknowledged by the peer. Once sock init is done,    control returns to __sys_socket().  

​    __sys_socket() then assigns a fd to the socket that was created, and returns    this to the application. Any interaction with the network sock will go through    the socket. System calls will call the socket call, using the fd the    corresponding sock will be found, after which the corresponding    protocol function will be invoked. sock->sk points to the sk and sk->sk_socket    points to sock. This way knowing one, we can reach the other.  

```c
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
  
```

​    The flow after calling a socket call usually follows the following pattern:

1. syscall entry, socket lookup based on fd. 
2. check if the process has the necessary security permissions
3. call the corresponding function from sock->ops
4. get sock from the socket, call the function from sk->ops

### N4.3 socket, sock, inet_sock, inet_connection_sock and tcp_sock (OPTIONAL)

​    TODO:    Why two parts: socket and sock ? is a socket without sock possible ?  

### N4.4 bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)

​    TCP bind is called by server, which provides services on a well known port,    to which the client connects to. The client may also bind, but usually the    client's port is not of importance, so a bind() call is seldom made.  

​    The usual flow, enter syscall, find the socket from fd, call the bind    function from the ops, which is inet_bind(). Then, the get the sk from    the sock, call sk->sk_prot->get_port(). In case of tcp it points to    inet_csk_get_port(). The get_port() takes two arguments, sk and port num, and    returns 0 if that port was available and was assigned to sk, and returns 1    if binding the port was not possible.  

```c
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
  
```

​    The TCP subsystem maintains a hash table with a list of hashbuckets corresponding    to each hash. Each bucket contains a list of sk which are bound to a port.    First, it checks if a bucket exists for the port requested. If it does not,    a new one is added and the sk is added ot the bucket. If the bucket exists,    and if the certain conditions are satisfied (see the next paragraph), the    sk is added to the bucket, and the bind is successful.  

```c
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
		//calc hash and find the right bucket head

		spin_lock_bh(&head->lock);
		inet_bind_bucket_for_each(tb, &head->chain)	//search in the bucket
			if (tb->port == port) //exact bucket found
				goto tb_found;

		// if nothing is found, this is the first sock to use the port
		tb = inet_bind_bucket_create(hinfo->bind_bucket_cachep,
					     net, head, port);

	tb_found:
		if (!hlist_empty(&tb->owners)) {	// bucket has sk, i.e. port is being used
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
  
```

​    Multiple sockets can be bound to a single port, both TCP and UDP use it to    allow mutliple processes to share a port. All applications that want to reuse    the port should us the reuseport socket option (SO_REUSEPORT) to allow sharing    the port. See man page socket(7), about the use of SO_REUSEPORT, where possible    use cases are also described.
​    TCP (in Linux) has a unique way of allowing multiple sockets to share a port,    this has been added as a comment in "include/net/inet_hashtables.h", which  have added below. This logic is implemented in inet_csk_bind_conflict().  

```c
/* There are a few simple rules, which allow for local port reuse by
 * an application.  In essence:
 *
 *	1) Sockets bound to different interfaces may share a local port.
 *	   Failing that, goto test 2.
 *	2) If all sockets have sk->sk_reuse set, and none of them are in
 *	   TCP_LISTEN state, the port may be shared.
 *	   Failing that, goto test 3.
 *	3) If all sockets are bound to a specific inet_sk(sk)->rcv_saddr local
 *	   address, and none of them are the same, the port may be
 *	   shared.
 *	   Failing this, the port cannot be shared.
 *
 * The interesting point, is test #2.  This is what an FTP server does
 * all day.  To optimize this case we use a specific flag bit defined
 * below.  As we add sockets to a bind bucket list, we perform a
 * check of: (newsk->sk_reuse && (newsk->sk_state != TCP_LISTEN))
 * As long as all sockets added to a bind bucket pass this test,
 * the flag bit will be set.
 * The resulting situation is that tcp_v[46]_verify_bind() can just check
 * for this flag bit, if it is set and the socket trying to bind has
 * sk->sk_reuse set, we don't even have to walk the owners list at all,
 * we return that it is ok to bind this socket to the requested local port.
 *
 * Sounds like a lot of work, but it is worth it.  In a more naive
 * implementation (ie. current FreeBSD etc.) the entire list of ports
 * must be walked for each data port opened by an ftp server.  Needless
 * to say, this does not scale at all.  With a couple thousand FTP
 * users logged onto your box, isn't it nice to know that new data
 * ports are created in O(1) time?  I thought so. ;-)	-DaveM
 */
```

### N4.5 [Server\] `listen(int sockfd, int backlog)`

​    After opening a socket and binding to a port, the server calls the listen()    call. It is signal to the kernel that the application is now ready to accept    connections. The kernel initializes the necessary data structures, so SYN    packets can be received. This will be explained in the accept call. The code    flow is the standard one, finally calling sk->sk_prot->listen(), the    registered function is inet_listen().  

​    If the sock state is not TCP_LISTEN (i.e. this is the first listen() call),    sk_max_ack_backlog is set to the value supplied by the user. The sk->sk_ack_backlog    is a value that tracks the current value. The way these variables is used will    be seen before the accept call. For now the value is used to limit the number    of connections that have not been accepted by the application. The sk state    is moved to TCP_LISTEN, i.e. waiting to accept new connections. The    sk->sk_port->get_port() is called again (it was called while binding). This is    to make sure that two TCP_LISTEN sockets with the same port are not allowed    (case 2). While binding, the TCP port can be a server port or a client port.    Two client sockets reusing the port is acceptable. But a server socket and a    client socket on the same port cannot be allowed. So, though the bind() call has    not failed, because of clashing ports, the listen call can fail. The error    returned in that case is EADDRINUSE. Once the get_port call succeeds (returns 0),    inet->inet_sport is set. sk->sk_prot->hash() call is called, which similar    to get_port adds this sk into a hash table, which is maintained exclusively    for listen sockets. The function __inet_hash() is similar to inet_csk_get_port()    and hence is not explained.  

```c
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
					// again inet_csk_get_port(), check with tables.
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
  
```

​    At this point, the server is ready to accept new connections. The client has    to initiate the connection by calling connect().  

### N4.6 [Client\] `connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)`

​    The client calls connect() to initiate the connection, the sockaddr struct    providing the server's ip address and port. struct sockaddr is a common struct    which is passed in socket calls. The application and the kernel typecasts it    into another structure to fill/extract data. In case of IPv4 sockets,    it is set to struct sockaddr_in, which contains the IPv4 address and port.    In case of UNIX sockets, it is struct sockaddr_un.    Proper padding is added in each of them so all of them are of the same size.  

​    The usual flow, find the sock from fd, call sock->ops->connect() which is    inet_stream_connect(). Which internally calls __inet_stream_connect() after    holding sock->sk lock. The sock state till now was SS_UNCONNECTED,    sk->sk_prot->connect() is called, which points to tcp_v4_connect(). tcp_v4_connect()    sends out a TCP SYN packet. The sock state is moved to SS_CONNECTING state.    The connect call is blocked till a SYN-ACK is received from the server in    response. Based on the sk->sk_sndtimeo value, inet_wait_for_connect() is called.    If the non-blocking option is set, the connect call will not wait for the SYN-ACK,    instead will return immediately. The application can work on something else,    while TCP sets up the connection. In the default case, it will be blocked till    a SYN-ACK returns.  

```c
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
  
```

​    tcp_v4_connect() sends out a SYN to the server and updates the sk state.    Sending out a packet (usually) has these three steps:

1. Create a skb, fill up the headers.
2. Find a route to send it out
3. Call the function to hand it over to the ip layer.

​     tcp_v4_connect first moves the sk state to TCP_SYN_SENT. If something else    fails, the socket is closed, state moved back to TCP_CLOSE. Next a route to    the server is found and this route is set in the sk. Once a connect call is    called, the sk will only communicate over this route, no point in trying to    route the packet each time. If an interface is brought down, all TCP connections    which use a route through the interface will get closed. After setting the route    in the sk, tcp_connect is called, which allocates the skb, and transmits it.    A timer is started to retry sending SYN packets till a SYN-ACK is sent back.  

```c
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
  
```

​    At the end of all this, the connect call is blocked, it will wake up to either    find that a    SYN-ACK was rcvd in which case the connect call succeeds, returns 0 to the user,    the application can proceed and begin sending data.    Or the connect call will wake up after multiple SYN packets    were sent and connection is closed. The connect call will return -1 and    errno is set to ETIMEDOUT.  

### N4.7 TCP packet rcv

​    TCP's handler while receiving packets is tcp_v4_rcv(). It looks up the hashtables    maintained for bound sockets and listening sockets. If sk is not found, a reset    is sent back. If the sk->sk_state is not TCP_TIME_WAIT, further processing    is done in tcp_v4_do_rcv(). tcp_rcv_established() is called if the sk is a    established sk, for all other states tcp_rcv_state_process() is called.    We will begin with either tcp_rcv_established() or tcp_rcv_state_process()    hereon to describe the packet flow.  

​    Another thing to keep in mind is that TCP packet processing mentioned above    happens in the NET_RX softirq context. Once the processing is done, the userspace    process, which is waiting on a system call, has to be signalled so it can    continue processing the data.  

```c
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
		//handle time wait. explained in the last section in this page

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
  
```

### N4.8 [Server\] Recv SYN

​    On receiving a SYN, only if the sk->sk_state TCP_LISTEN, tcp_v4_conn_request()    is called. A struct tcp_request_sock is allocated and initialized. It is added    to the hash maps and a timer is started to retransmit SYN-ACKs. Finally    a SYN-ACK is sent out. The reqsk represents incoming sockets, and the number    of such req socks is limited by sk_max_ack_backlog, which is set in the    listen() call.  

```c
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
		{
			req = reqsk_alloc();
			ireq->ireq_state = TCP_NEW_SYN_RECV;
		}

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

	}
}
  
```

### N4.9 [Client\] Recv SYN-ACK

​    Fairly straight forward, the sock is found, tcp header data added into the    sock. The sock is moved to the TCP_ESTABLISHED state and the process waiting    in the connect() call.  

```
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
					// wakeup the process waiting on connect()
				}
			}
		}
		tcp_send_ack(sk);
		return -1;
	}
}
  
```

### M4.10 [Client\] connect() [continued]

​    The process waiting on connect system call wakes up, and if the sock state    is not TCP_CLOSE, the connect was successful. The socket state is moved    to SS_CONNECTED.  

```
		inet_wait_for_connect(sk, timeo, writebias);
		
		/* Connection was closed by RST, timeout, ICMP error
		 * or another process disconnected us.
		 */
		if (sk->sk_state == TCP_CLOSE)
			goto sock_error;

		sock->state = SS_CONNECTED;
	} // inet_stream_connect
} // __sys_connect
  
```

### N4.11 [Server\] Recv ACK

  On receiving the ACK in response to the sent SYN-ACK, initial socket lookup  will return the request_sock created earlier which is in the TCP_NEW_SYN_RECV  state. This sock is not a full blown sock which can handle a TCP connection.  The kernel now creates another tcp_sock which will replace this sock.  

  The syn_recv_sock() function registered within inet connection ops is called,  which is set to tcp_v4_syn_recv_sock(). Firstly if the accept queue is full,  the new packet is dropped. Next a new sock is created in inet_csk_clone_lock(),  data from inet request sock is copied into newsk and its state is set to  TCP_SYN_RECV. Next tcp specific data is copied from the listen socket into the  new sock. __inet_inherit_port() edits the hashbuckets to make sure that any  socket lookups will fetch the newsk and not reqsk. Finally the reqsk is added  into the accept queue, and req->sk points to the newly created sock.  

  Finally the skb (ACK) is processed holding the new sock. The new sock is called  the child sock as well. Since the new sock 's state is in TCP_SYN_RECV and a  valid ACK was received, the child sock moves to TCP_ESTABLISHED state.  

  Since the process waits on accept() call on the listen fd to accept new  connections, the process is woken up by calling sk_data_ready().  

```c
int tcp_v4_rcv(struct sk_buff *skb)
{
	sk = __inet_lookup_skb(&tcp_hashinfo, skb, __tcp_hdrlen(th), th->source,
				   th->dest, sdif, &refcounted);
	if (sk->sk_state == TCP_NEW_SYN_RECV) {
		struct request_sock *req = inet_reqsk(sk);
		struct sock *nsk;

		sk = req->rsk_listener;
		nsk = tcp_check_req(sk, skb, req, false, &req_stolen);
		{
			child = inet_csk(sk)->icsk_af_ops->syn_recv_sock(sk, skb, req);
			//tcp_v4_syn_recv_sock()
			{
				if (sk_acceptq_is_full(sk))
					goto exit_overflow;

				newsk = tcp_create_openreq_child(sk, req, skb);
				{
					struct sock *newsk = inet_csk_clone_lock(sk, req, GFP_ATOMIC);
					{
						inet_sk_set_state(newsk, TCP_SYN_RECV);
						// copy data from inet_rsk(req) into newsk
					}
					// copy data from req into newsk

					newtp = tcp_sk(newsk);
					oldtp = tcp_sk(sk);
					//init newtp ; copy data from oldtp into newtp

					return newsk;
				}

				__inet_inherit_port(sk, newsk);
				return newsk;
			}
			return inet_csk_complete_hashdance(sk, child, req, own_req);
			{
				inet_csk_reqsk_queue_drop(sk, req);
				reqsk_queue_removed(&inet_csk(sk)->icsk_accept_queue, req);
				inet_csk_reqsk_queue_add(sk, req, child)
				{
					struct request_sock_queue *queue = &inet_csk(sk)->icsk_accept_queue;
					req->sk = child;
					queue->rskq_accept_tail->dl_next = req;
				}
			}
		}

		tcp_child_process(sk, nsk, skb);
		{
			int state = child->sk_state;
			ret = tcp_rcv_state_process(child, skb);
			{
				switch (child->sk_state) {
					case TCP_SYN_RECV:
						tcp_set_state(child, TCP_ESTABLISHED);
				}
			}
			/* Wakeup parent, send SIGIO */
			if (state == TCP_SYN_RECV && child->sk_state != state)
				parent->sk_data_ready(parent);
		}
	}
}
  
```

### N4.12 [Server\] accept()

  The accept call takes the listenfd and returns a fd for the established  connection. This fd will be used to transfer data, while the listen fd wil  continue accepting connections.  

  A socket is allocated and an unused fd is assigned to it. Next inet_accept  is called, which calls inet_csk_accept. Here the icsk_accept_queue is checked,  if empty the process sleeps waiting for a connection. Once a connection is  established, an entry is dequeued  from the icsk_accept_queue. This  request sock req points to the full tcp sock which is returned. Also reqsk_put()  is called, so if the refcount is zero the socket will be freed.  

  The new sock is connected to the socket allocated earlier and the socket is  moved to SS_CONNECTED state. Now the user can send/recv data using this fd.  

```c
int __sys_accept4(int fd, struct sockaddr __user *upeer_sockaddr,
		  int __user *upeer_addrlen, int flags)
{
	sock = sockfd_lookup_light(fd, &err, &fput_needed);

	newsock = sock_alloc();
	newsock->type = sock->type;
	newsock->ops = sock->ops;

	newfd = get_unused_fd_flags(flags);
	newfile = sock_alloc_file(newsock);

	err = sock->ops->accept(sock, newsock, sock->file->f_flags, false);
	//inet_accept()
	{
		struct sock *sk1 = sock->sk;
		int err = -EINVAL;
		struct sock *sk2 = sk1->sk_prot->accept(sk1, flags, &err, kern);
		//inet_csk_accept()
		{
			struct request_sock_queue *queue = &icsk->icsk_accept_queue;
			struct request_sock *req;

			if (reqsk_queue_empty(queue)) {
				long timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);
				inet_csk_wait_for_connect(sk, timeo);
				{
					prepare_to_wait_exclusive(sk_sleep(sk), &wait,
							TASK_INTERRUPTIBLE);
					// waits till sk_data_ready() is called
				}
			}
			req = reqsk_queue_remove(queue, sk);
			{
				struct request_sock_queue *queue = &inet_csk(sk)->icsk_accept_queue;
				req = queue->rskq_accept_head;
			}
			newsk = req->sk;
			reqsk_put(req);

			return newsk;
		}

		sock_graft(sk2, newsock);
		newsock->state = SS_CONNECTED;
	}

	fd_install(newfd, newfile);
	return newfd;
}
  
```

### N4.13 send() and recv()

  Sending a message is similar to the way it is described in  [Packet TX path 1 : Basic](./3_Packet_TX_Basic.md). Just pasting the  call flow below.  

```
	__sys_sendmsg(int fd, struct user_msghdr __user *msg)
	 ___sys_sendmsg(sock, msg, &msg_sys, flags, NULL, 0);
	sock_sendmsg(sock, msg_sys);
	int sock_sendmsg_nosec(sock, msg)
	inet_sendmsg(sock, msg, size);
	tcp_sendmsg(sk, msg, size);
	tcp_sendmsg_locked(sk, msg, size);
	tcp_push_one(sk, mss_now);
	tcp_write_xmit(sk, mss_now, TCP_NAGLE_PUSH, 1, sk->sk_allocation);
	tcp_transmit_skb(sk, skb, 1, gfp);
	__tcp_transmit_skb(sk, skb, clone_it, gfp_mask, tcp_sk(sk)->rcv_nxt);
	icsk->icsk_af_ops->queue_xmit(sk, skb, &inet->cork.fl);
  
```

  On receiving a packet, similar to the description in  [Packet RX path 1 : Basic](2_Packet_RX_Basic.md), the packet is added  to the socket queue and the kernel wakes up the process, if the process is  waiting on the recv() system call.  Just pasting the flow of packet till sock enqueue below.  

```
	void tcp_rcv_established(struct sock *sk, struct sk_buff *skb)
	tcp_queue_rcv(sk, skb, tcp_header_len, &fragstolen);
	{
		__skb_queue_tail(&sk->sk_receive_queue, skb);
		tcp_data_ready(sk);	// wake up the process
	}
  
```

  The recv() system call is simple, if a packet is available in the sk_receive_queue  the data is copied into the buffer else the process sleeps waiting for a packet.  The tcp subsystem makes sure that the packets are added in the right order using  sequence numbers.  

```
	long __sys_recvmsg(int fd, struct user_msghdr __user *msg, unsigned int flags)
	{
		sock = sockfd_lookup_light(fd, &err, &fput_needed);
		err = ___sys_recvmsg(sock, msg, &msg_sys, flags, 0);
		sock_recvmsg_nosec(sock, msg_sys, flags);
		{
			return sock->ops->recvmsg(sock, msg, msg_data_left(msg), flags);
			//inet_recvmsg()
			{
				err = sk->sk_prot->recvmsg(sk, msg, size, flags & MSG_DONTWAIT,
							   flags & ~MSG_DONTWAIT, &addr_len);
				//tcp_recvmsg()
				{
					timeo = sock_rcvtimeo(sk, nonblock);
					sk_wait_data(sk, &timeo, last);

					skb_copy_datagram_msg(skb, offset, msg, used);
					//copy data into msg
					sk_eat_skb(sk, skb);
				}
			}
		}
	}
  
```

### N4.14 close() or the process died: Ungraceful shutdown

If a process wants to abort the connection or in case the process crashes or  is killed without closing it's open tcp socket, a RST is sent to the peer closing  the connection. We will describe the close() system call below.  

  On calling close() on a established tcp_sock, all packets that are waiting in  the receive queue are dropped, all packets in send queue are pushed out and  a RST is sent to close the TCP connection. The sock is moved to TCP_CLOSE state  and sock is freed.  

```c
	SYSCALL_DEFINE1(close, unsigned int, fd)
	{
		int retval = __close_fd(current->files, fd);
		{
			return filp_close(file, files);
			{
				fput(filp);
				{
					schedule_delayed_work(&delayed_fput_work, 1);
					//schedule delayed_fput()
				}
			}

			__put_unused_fd(files, fd);	// fd can be reused
		}
	}

	static void delayed_fput(struct work_struct *unused)
	{
		struct file *f, *t;
		__fput(f);
		{
			file->f_op->release(inode, file);
			//inet_release()
			{
				struct sock *sk = sock->sk;
				sock->sk = NULL;
				sk->sk_prot->close(sk, timeout);
				{
					if (sk->sk_state == TCP_LISTEN) {
						tcp_set_state(sk, TCP_CLOSE);

						/* Special case. */
						inet_csk_listen_stop(sk);

						goto adjudge_to_death;
					}
					while ((skb = __skb_dequeue(&sk->sk_receive_queue)) != NULL) {
						//dequeue and free all skbs
						__kfree_skb(skb);
					}
					tcp_set_state(sk, TCP_CLOSE);
					tcp_send_active_reset(sk, sk->sk_allocation);

				adjudge_to_death:
					state = sk->sk_state;
					sock_hold(sk);
					sock_orphan(sk);
					sock_put(sk);
				}

			}
		}
	}
  
```

  One minor thing to note is that if the process reads all data enqueued in the  receive queue and then calls close(), the kernel will close the connection  gracefully by sending out a FIN, as described in the next section.  

### N4.15 shutdown()

  Shutdown is the graceful way of closing, where the initiator sends out a FIN  and the peer acknowledges it by sending a FIN-ACK, and finally the initiator  sends a ACK. Similar to the way connection establishment happens, using FIN  instead of SYN. Both peers close their sockets.  

```c
	int __sys_shutdown(int fd, int how)
	{
		sock = sockfd_lookup_light(fd, &err, &fput_needed);
		err = sock->ops->shutdown(sock, how);
		//inet_shutdown()
		{
			sk->sk_prot->shutdown(sk, how);
			//tcp_shutdown()
			{
				if (tcp_close_state(sk))
					tcp_send_fin(sk);
			}
		}
	}
  
```

  The initiator moves to the TCP_FIN_WAIT1 state and on receiving a FIN-ACK  eventually closes the socket.
  The socket is eventually reused for another connection. A delayed FIN, that  was meant for the previous connection may arrive causing the connection to  close. To prevent this, before closing the TCP sockets moves to a TIME WAIT  state, accepting any packets from the peer. After a sufficiently long time  the socket is closed.  
