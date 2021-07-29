# N5.4 Adding tables, chains, rules (kernel)

Netlink messages are handled in  nfnetlink_rcv_batch()

```c
(gdb) bt
#0  nfnetlink_rcv_batch
#1  nfnetlink_rcv_skb_batch
#2  nfnetlink_rcv
#3 netlink_unicast_kernel
#4  netlink_unicast
#5  netlink_sendmsg
#6  sock_sendmsg_nosec
#7  sock_sendmsg
#8  ____sys_sendmsg
```

An overview of how the data is handled in `nfnetlink_rcv_batch`:

1. Hold the `nftables_pernet->commit mutex`
2. Add each change as a transaction into `nftables_pernet->commit_list`.
3. Commit changes in `nf_tables_commit`
4. Release lock `nftables_pernet->commit mutex`

```c
static void nfnetlink_rcv_batch(struct sk_buff *skb, struct nlmsghdr *nlh, 
                                u16 subsys_id, u32 genid)
{
    struct net *net = sock_net(skb->sk); // same namespace as the process
    
    // subsys_id = NFNL_SUBSYS_NFTABLES
    ss = nfnl_dereference_protected(subsys_id);
    /* ss = struct nfnetlink_subsystem nf_tables_subsys {
    		.cb				= nf_tables_cb,
    		.commit			= nf_tables_commit,
    		.valid_genid	= nf_tables_valid_genid,
    	} */
    
    nc = nfnetlink_find_client(type, ss);
    
    ss->valid_genid(net, genid)
    {
        
        struct nftables_pernet *nft_net = nft_pernet(net);
        mutex_lock(&nft_net->commit_mutex);
    }
    
    while (skb->len >= nlmsg_total_size(0)) {
        type = nlh->nlmsg_type;
        if (type == NFNL_MSG_BATCH_BEGIN) {
            /* Malformed: Batch begin twice */
            status |= NFNL_BATCH_FAILURE;
            goto done;
        } else if (type == NFNL_MSG_BATCH_END) {
            status |= NFNL_BATCH_DONE;
            goto done;
        } else if (type < NLMSG_MIN_TYPE) {
            err = -EINVAL;
            goto ack;
        }
        
        nc = nfnetlink_find_client(type, ss); // call back
        // nc is picked from nf_tables_cb
        {
            return &ss->cb[cb_id]
        }
        
        err = nc->call(skb, &info, (const struct nlattr **)cda);
        // create a table and add transaction to commit_list
    }

    err = ss->commit(net, oskb);
    // nf_tables_commit(struct net *net, struct sk_buff *skb)
    {
     	list_for_each_entry_safe(trans, next, &nft_net->commit_list, list) {
            // based  on the transaction type, commit it
        }
        nf_tables_commit_release(net)
        {
            // some clean-up and finally unlock mutex
            mutex_unlock(&nft_net->commit_mutex);
        }
    }
    if (err) {
        ss->abort(net, oskb, NFNL_ABORT_NONE);
    }

}
```











