# N5.1 Rule Matching



### N5.1.0 Setup table, chain and a rule

Let us first setup a simple table, a chain and rule that counts all packets whose source address is `127.0.0.1`



For our current exploration, lets add a simple rule which counts packets from 127.0.0.9

```sh
$ nft add table ip T  # a table called T
$ nft add chain T C { type filter hook prerouting priority 0\; }  # a chain C
$ nft add rule T C ip saddr 127.0.0.9 counter
```

The above chain has been added in the prerouting hook, i.e. it will run before the kernel makes a rounting decision. A kernel based on the routing table, will decide if the packet has to forwarded out of an network interface or will be consumed locally by an application.

Next to check the ruleset:

```sh
$ nft list ruleset 

table ip T {
        chain C {
                type filter hook prerouting priority filter; policy accept;
                ip daddr 127.0.0.9 counter packets 4 bytes 336
        }
}
```



Run pings to that address, and we can see the counter increase.

```sh
$ ping 127.0.0.9
```



### N5.1.1 Rule matching in NET_RX/NET_TX

Like discussed in the RX,TX path articles, netfilter processing happens in the softirq context. The pre-routing hook is hit in `ip_rcv`

```c
int ip_rcv(struct sk_buff *skb, struct net_device *dev,
           struct packet_type *pt,
	       struct net_device *orig_dev)
{
	struct net *net = dev_net(dev);

	skb = ip_rcv_core(skb, net);
	if (skb == NULL)
		return NET_RX_DROP;

	return NF_HOOK(NFPROTO_IPV4, NF_INET_PRE_ROUTING,
		       net, NULL, skb, dev, NULL,
		       ip_rcv_finish);
}

```

On entering NF_HOOK it internally calls calls nf_hook. If the nf filters return a verdict of accept, the packet is passed into the okfn (`ip_rcv_finish` in  this case ).  Take a look at all the data that is passed into the netfilter  hooks.

```c
static inline int
NF_HOOK(uint8_t pf,              //protocol family (IP in our case)
		 unsigned int hook,      //PREROUTING
		 struct net *net,        //network namespace (net_init)
		 struct sock *sk,        //will be set if sock lookup happens early on
	     struct sk_buff *skb,    //the packet
	     struct net_device *in,  //source device
	     struct net_device *out, //destination device (if routing is complete)
	     int (*okfn)(struct net *, struct sock *, struct sk_buff *))
	                             //func to run on ACCEPT
{
	int ret = nf_hook(pf, hook, net, sk, skb, in, out, okfn);
    if (ret == 1) 
		ret = okfn(net, sk, skb);
	return ret;
}
```





`nf_hook` then based on the hook and protocol family which called NF_HOOK, chooses a list of `nf_hook_entries`. This list is then traversed in `nf_hook_slow`.

```c
static inline int
nf_hook(u_int8_t pf,
        unsigned int hook, struct net *net,
        struct sock *sk, struct sk_buff *skb,
        struct net_device *indev, struct net_device *outdev,
        int (*okfn)(struct net *, struct sock *, struct sk_buff *))
{
	struct nf_hook_entries *hook_head = NULL;
	
	rcu_read_lock();
	switch (pf) {
	case NFPROTO_IPV4:
		hook_head = rcu_dereference(net->nf.hooks_ipv4[hook]);
		break;
		// ... other protocols //
		
		default:
		WARN_ON_ONCE(1);
		break;
	}
	
	if (hook_head) {
		struct nf_hook_state state;
		nf_hook_state_init(&state, hook, pf, indev, outdev,
				   sk, net, okfn);
		ret = nf_hook_slow(skb, &state, hook_head, 0);
	}
	rcu_read_unlock();

	return ret;
}
```



`nf_hook_slow` is the actual function that runs each of the hook_entry functions. If any of the hook functions return a terminating verdict (`NF_ACCEPT` or `NF_DROP`),  the loop is broken and the verdict is returned.

```c
int nf_hook_slow(struct sk_buff *skb, struct nf_hook_state *state,
		 const struct nf_hook_entries *e,
         unsigned int s)
{
	unsigned int verdict;
	int ret;

	for (; s < e->num_hook_entries; s++) {
		verdict = nf_hook_entry_hookfn(&e->hooks[s], skb, state);
		switch (verdict & NF_VERDICT_MASK) {
		case NF_ACCEPT:
			break;
		case NF_DROP:
			kfree_skb(skb);
			ret = NF_DROP_GETERR(verdict);
			if (ret == 0)
				ret = -EPERM;
			return ret;
		// ... other verdicts ///
		}
	}

	return 1;
}
```

The `hook_entries` are added in `nf_register_net_hook` , which takes a `struct nf_hook_ops` which is called while adding a rule.  In our case, for ipv4, `nft_chain_filter_ipv4_init` registers `nft_do_chain_ipv4` as the hook function.



`nft_do_chain_ipv4` simply sets pktinfo from the skb and calls `nft_do_chain`.



All chains finally enter `nft_do_chain`  which matches the packet against rules. How rules are translated into register operations will discussed in a later article. But, for now ``nft_do_chain`` is the actual function that matches rules against packets and returns a verdict

The overall stack at this point looks like:

```c
(gdb) hbreak nft_do_chain
Hardware assisted breakpoint 3 at 0xffffffff83399250: file net/netfilter/nf_tables_core.c, line 159.

(gdb) bt
#0  nft_do_chain ()
#1  nft_do_chain_ipv4 () 
#2  nf_hook_entry_hookfn ()
#3  nf_hook_slow ()
#4  nf_hook_slow_list ()
#5  NF_HOOK_LIST (pf=2 '\002',...
				  okfn=0xffffffff83487250 <ip_rcv_finish>, ...)
#6  ip_sublist_rcv ()
#7  ip_list_rcv ()
#8  __netif_receive_skb_list_ptype ()
#9  __netif_receive_skb_list_core ()
#10  __netif_receive_skb_list ()
#11 netif_receive_skb_list_internal ()
    ... napi poll ...
#18 net_rx_action () at net/core/dev.c:7201
#19 __do_softirq () at kernel/softirq.c:558
#20 do_softirq () at kernel/softirq.c:459
```

