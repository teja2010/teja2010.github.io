# N5.3 Adding tables, chains, rules

netlink messages are used to setup tables, chains, etc.



Let us first disect the netlink message that is sent by nft by running `strace` . Some parts of the netlink message that are not parsed by strace, have also been formatted:

```c
$ strace -f nft add table T2


socket(AF_NETLINK, SOCK_RAW, NETLINK_NETFILTER) = 3
sendto(3,
	{
        struct nlmsghdr {
            nlmsg_len=20,
            nlmsg_type=NFNL_SUBSYS_NFTABLES << 8 | NFT_MSG_GETGEN, //<- 0xa10
            nlmsg_flags=NLM_F_REQUEST,
            nlmsg_seq=0, nlmsg_pid=0
        },
        struct nfgenmsg{ // <- "\x00\x00\x00\x00"
            nfgen_family=AF_UNSPEC,
            version = NFNETLINK_V0,
            res_id = 0,
        }
        
    },20,
    0,{sa_family=AF_NETLINK, nl_pid=0, nl_groups=00000000},12) = 20
    
recvmsg(3,
   {
       msg_name={
           sa_family=AF_NETLINK,
           nl_pid=0,
           nl_groups=00000000
       },
       msg_namelen=12,
       msg_iov=[
           {
               iov_base={
                   struct nlmsghdr {
                       nlmsg_len=44,
                       nlmsg_type=NFNL_SUBSYS_NFTABLES << 8 | NFT_MSG_NEWGEN //<- 0xa0f
                       nlmsg_flags=0,
                       nlmsg_seq=0,
                       nlmsg_pid=214676 // pid
                   },
                   struct nfgenmsg{ // <-  "\x00\x00\x00\x10"
                            nfgen_family=AF_UNSPEC,
                            version = NFNETLINK_V0,
                            res_id = 0x1000, // nft_base_seq(net)
                   },
                   struct nlattr {
                       nla_len = 8, //<- \x08\x00
                       nla_type = NFTA_GEN_ID, // <- \x01\x00
                   },
                   data = htonl(nft_net->base_seq) "\x00\x00\x00\x10",
                   padding = "",
                   struct nlattr {
                       nla_len = 8, //<- \x08\x00
                       nla_type = NFTA_GEN_PROC_PID, // <- "\x02\x00"
                   },
                   data = htonl(pid), // <- htonl(214676) = "\x00\x03\x46\x94",
                   padding = "",
                   struct nlattr {
                       nla_len = 8, //<- \x08\x00
                       nla_type = NFTA_GEN_PROC_PID, // <- "\x03\x00"
                   },
                   data="nft\0", // <-"\x6e\x66\x74\x00"
                   padding = "",
               },
               iov_len=69631
           }
       ],
       msg_iovlen=1, msg_controllen=0, msg_flags=0}, 0) = 44

sendmsg(3,
    {
        msg_name={
            sa_family=AF_NETLINK,
            nl_pid=0,
            nl_groups=00000000
        },
        msg_namelen=12,
        msg_iov=[
            {
                iov_base=[
                    {
                        struct nlmsghdr {
                            nlmsg_len=20,
                            nlmsg_type=NFNL_MSG_BATCH_BEGIN, //<- 0x10
                            nlmsg_flags=NLM_F_REQUEST,
                            nlmsg_seq=0, 
                            nlmsg_pid=0
                        },
                        struct nfgenmsg{ // <- "\x00\x00\x0a\x00"
                            nfgen_family=AF_UNSPEC,
                            version = NFNETLINK_V0,
                            res_id = NFNL_SUBSYS_NFTABLES, /*resource id */
                        }
                    },
                    {
                        struct nlmsghdr {
                            nlmsg_len=36,
                            nlmsg_type=NFNL_SUBSYS_NFTABLES << 8 | NFT_MSG_NEWTABLE, // <- 0xa00
                            nlmsg_flags=NLM_F_REQUEST,
                            nlmsg_seq=1,
                            nlmsg_pid=0
                        },
                         struct nfgenmsg{ // <- "\x02\x00\x00\x00"
                            nfgen_family=AF_INET,
                            version = NFNETLINK_V0,
                            res_id = 0, /*resource id */
                        }
                        struct nlattr {
                            nla_len = 7, // <- "\x07\x00"
                            nla_type = NFTA_TABLE_NAME, //<- "\x01\x00"
                        },
                        data = "T2\0",  // <- "\x54\x32\x00"
                        padding = "\x00", // 4 byte alignment
                        
                        struct nlattr {
                            nla_len = 8, // <- "\x08\x00"
                            nla_type = NFTA_TABLE_FLAGS //  "\x02\x00"
                        },
                        (u32) flags = 0, // <- \x00\x00\x00\x00",
                    },
                    {
                        struct nlmsghdr {
                            nlmsg_len=20,
                            nlmsg_type=NFNL_MSG_BATCH_END,
                            nlmsg_flags=NLM_F_REQUEST, 
                            nlmsg_seq=2, 
                            nlmsg_pid=0,
                        }, 
                        struct nfgenmsg{ // <- "\x00\x00\x0a\x00"
                            nfgen_family=AF_UNSPEC,
                            version = NFNETLINK_V0,
                            res_id = NFNL_SUBSYS_NFTABLES, /*resource id */
                        }
                    }
                ], 
                iov_len=76
            }
        ], 
        msg_iovlen=1, msg_controllen=0, msg_flags=0}, 0) = 76
```









```c
Breakpoint 2, nf_tables_commit (net=0xffffffff85085080 <init_net>, skb=0xffff88800c787dc0) at net/netfilter/nf_tables_api.c:8483
8483    {
(gdb) bt
#0  nf_tables_commit
#1  nfnetlink_rcv_batch
#2  nfnetlink_rcv_skb_batch
#3  nfnetlink_rcv
#4 netlink_unicast_kernel
#5  netlink_unicast
#6  netlink_sendmsg
#7  sock_sendmsg_nosec
#8  sock_sendmsg
#9  ____sys_sendmsg
```



