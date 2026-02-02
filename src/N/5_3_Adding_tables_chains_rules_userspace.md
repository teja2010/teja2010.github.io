# N5.3 Adding tables, chains, rules (userspace)

netlink messages are used to setup tables, chains, etc from userspace.

Let us dissect the netlink message that is sent by `nft` by running `strace` .  Some parts of the netlink message that are not parsed by `strace`, have also been formatted here.

```sh
$ strace -f nft add table T2
```



`nft` creates a netlink socket and first sends a request to get rule-set generation `NFT_MSG_GETGEN`. In response, we receive the generation, which in this case is 3.  

```c
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
                   struct nfgenmsg{ // <-  "\x00\x00\x00\x03"
                            nfgen_family=AF_UNSPEC,
                            version = NFNETLINK_V0,
                            (be16) res_id = 0x0003, // nft_base_seq(net)
                   },
                   struct nlattr {
                       nla_len = 8, //<- \x08\x00
                       nla_type = NFTA_GEN_ID, // <- \x01\x00
                   },
                   data = htonl(nft_net->base_seq) "\x00\x00\x00\x03",
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
```

netfilter stores the generation number in `struct nftables_pernet` , which is data that is maintained per network namespace.

```c
struct nftables_pernet *nft_net = nft_pernet(net);

struct nftables_pernet {
    struct list_head	tables; // list of tables
    struct list_head    commit_list; // list of transactions that are pending
    ...
    struct mutex        commit_mutex;
    unsigned int        base_seq; // generation number
};
```



Next, nft constructs and sends a netlink message to setup the table. netlink messages have sections each with a netlink message header and it's attributes.  `nlmsghdr->nlmsg_type` lets the kernel handle the section accordingly. In the below message we have the following sections:

1. `NFNL_MSG_BATCH_BEGIN`: begin a batch of changes to the ruleset
2. `NFT_MSG_NEWTABLE` : create a new table, with attributes:
   1. `NFTA_TABLE_NAME`:  ``"T2\0"``
   2. `NFTA_TABLE_FLAGS`: 0
3. `NFNL_MSG_BATCH_END`: end of batch. Now commit these changes (which happens atomically as we will see in a later section).

```c
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



Though I wont show strace output, to setup a chain will have a netlink message with `NFT_MSG_NEWCHAIN`  and attributes `NFTA_CHAIN_TABLE`,  ` NFTA_CHAIN_NAME` , `NFTA_CHAIN_HOOK` and`NFTA_CHAIN_POLICY`. 

And to setup a rule nft will send a message with `NFT_MSG_NEWRULE` and attributes `NFTA_RULE_TABLE`, `NFTA_RULE_CHAIN`  and most importantly `NFTA_RULE_EXPRESSIONS` which is shown below.

Note: use `--string-limit` so strace dumps the complete msg data.

````sh
strace -f --string-limit=1024  -o one.txt nft add rule T C ip daddr 127.0.0.9 counter
````



```c
sendmsg(3,
   {msg_name=
       {sa_family=AF_NETLINK,
        nl_pid=0,
        nl_groups=00000000},
    msg_namelen=12,
    msg_iov=[
      {iov_base=[
        {
            struct nlmsghdr {
                nlmsg_len=20,
                nlmsg_type=NFNL_MSG_BATCH_BEGIN, // <- 0x10,
                nlmsg_flags=NLM_F_REQUEST,
                nlmsg_seq=0,
                nlmsg_pid=0},
            struct nfgenmsg { // <- "\x00\x00\x0a\x00"
                nfgen_family=AF_UNSPEC,
                version = NFNETLINK_V0,
                res_id = NFNL_SUBSYS_NFTABLES, /*resource id */
            }
        },
        {
            struct nlmsghdr {
                nlmsg_len=156,
                nlmsg_type= NFNL_SUBSYS_NFTABLES << 8 | NFT_MSG_NEWRULE, // <- 0xa06
                nlmsg_flags=NLM_F_REQUEST|0xc00,
                nlmsg_seq=1,
                nlmsg_pid=0
            },
            struct nfgenmsg { // <- "\x02\x00\x00\x00"
                nfgen_family=AF_INET,
                version = NFNETLINK_V0,
                res_id = 0,
            },
            struct nlattr {
                nla_len = 6,       // "\x06\x00"
                nla_type = NFTA_RULE_TABLE, // <- "\x01\x00"
            },                       
            data =  "T\0", //   "\x54\x00
            padding = "\x00\x00",
            struct nlattr {
                nla_len = 6, // <- \x06\x00
                nla_type = NFTA_RULE_CHAIN, // <- "\x02\x00"
            },
            data = "C\0", // \x43\x00
            padding = "\x00\x00",
            struct nlattr {
                nla_len = 120, //<-  \x78\x00,
                nla_type = NLA_F_NESTED | NFTA_RULE_EXPRESSIONS, //<-\x04\x80
            }

            {
                // 1. payload load 4b @ network header + 16 => reg 1
                struct nlattr {
                    nla_len = 52, //<- "\x34\x00"
                    nla_type = NLA_F_NESTED | NFTA_LIST_ELEM,  //<- \x01\x80
                }
                struct nlattr {
                    nla_len = 12, //<- \x0c\x00
                    nla_type = NFTA_EXPR_NAME, // <- \x01\x00
                }
                data = "payload\0",  // <- "\x70\x61\x79\x6c\x6f\x61\x64\x00",
                padding = "",
                struct nlattr {
                    nla_len = 36, // \x24\x00
                    nla_type = NLA_F_NESTED | NFTA_EXPR_DATA, //<- "\x02\x80"
                }
                struct nlattr {
                    nla_len = 8, // \x08\x00
                    nla_type = NFTA_PAYLOAD_DREG // <- \x01\x00
                }
                data = 1, //<-  "\x00\x00\x00\x01"
                payload = "",
                struct nlattr {
                    nla_len = 8, //<- \x08\x00
                    nla_type = NFTA_PAYLOAD_BASE,//<- \x02\x00
                }
                data = NFT_PAYLOAD_NETWORK_HEADER, //<-\x00\x00\x00\x01
                padding = "",
                struct nlattr {
                    nla_len = 8, //\x08\x00
                    nla_type = NFTA_PAYLOAD_OFFSET ,//\x03\x00
                }
                data = 16, // \x00\x00\x00\x0f,
                padding = "",
                struct nlattr {
                    nla_len = 8, //\x08\x00
                    nla_type = NFTA_PAYLOAD_LEN // \x04\x00
                }
                data = 4, //"\x00\x00\x00\x04"
                padding = "",
            }
            {
                //2. cmp eq reg 1 0x0900007f
                struct nlattr {
                    nla_len = 44, // <-\x2c\x00
                    nla_type = NLA_F_NESTED | NFTA_LIST_ELEM, //<- \x01\x80
                }
                struct nlattr {
                    nla_len = 8, // \x08\x00
                    nla_type = NFTA_EXPR_NAME //\x01\x00
                }
                data = "cmp\0", //"\x63\x6d\x70\x00"
                payload = "",
                struct nlattr {
                    nla_len = 32, // <-\x20\x00
                    nla_type = NLA_F_NESTED | NFTA_EXPR_DATA // \x02\x80
                }
                struct nlattr {
                    nla_len = 8, //\x08\x00
                    nla_type = NFTA_CMP_SREG // \x01\x00
                }
                data = 1, // \x00\x00\x00\x01
                padding = "",
                struct nlattr {
                    nla_len = 8, //\x08\x00,
                    nla_type = NFTA_CMP_OP, //\x02\x00
                }
                data =NFT_CMP_EQ, // = 0 <- "\x00\x00\x00\x00",
                padding = "",
                struct nlattr {
                    nla_len = 12, //\x0c\x00
                    nla_type = NLA_F_NESTED | NFTA_CMP_DATA, //\x03\x80
                }
                struct nlattr {
                    nla_len = 8, //\x08\x00
                    nla_type = NFTA_DATA_VALUE, // \x01\x00
                }
                data = 0x0900007f, //"\x7f\x00\x00\x09"
                padding = "",

            }
            {
                // 3. counter pkts 0 bytes 0 
                struct nlattr {
                    nla_len = 20, //\x14\x00
                    nla_type = NLA_F_NESTED | NFTA_LIST_ELEM, //\x01\x80
                }
                struct nlattr {
                    nla_len = 12, //\x0c\x00
                    nla_type = NFTA_EXPR_NAME, // \x01\x00
                }
                data = "counter\0", // "\x63\x6f\x75\x6e\x74\x65\x72\x00"
                padding = "",
                struct nlattr {
                    nla_len = 4 , // \x04\x00
                    nla_type = NLA_F_NESTED | NFTA_EXPR_DATA, //\x02\x80
                    // nested but len is 4 => no additional data 
                }
            }
        },
        {
            {
                nlmsg_len=20,
                nlmsg_type=NFNL_MSG_BATCH_END, // <- 0x11,
                nlmsg_flags=NLM_F_REQUEST,
                nlmsg_seq=2,
                nlmsg_pid=0},
            struct nfgenmsg { // <- "\x00\x00\x0a\x00"
                nfgen_family=AF_UNSPEC,
                version = NFNETLINK_V0,
                res_id = NFNL_SUBSYS_NFTABLES, /*resource id */
            }
        }
    ], iov_len=196}
  ], msg_iovlen=1, msg_controllen=0, msg_flags=0}, 0) = 1
```







