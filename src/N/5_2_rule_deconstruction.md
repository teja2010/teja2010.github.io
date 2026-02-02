# N5.2 Rule deconstruction



This article looks at how each rule is constructed and how a VM is emulated within netfilter to enforce the rules. Running `nft -d netlink`  shows the instructions which the rule is converted into:

```sh
$ nft -d netlink add rule T C ip daddr 127.0.0.9 counter
ip 
  [ payload load 4b @ network header + 16 => reg 1 ]
  [ cmp eq reg 1 0x0900007f ]
  [ counter pkts 0 bytes 0 ]
```

Going through each instruction expression:

`[ payload load 4b @ network header + 16 => reg 1 ]`  read 4 bytes from the network header at an offset of 16 and store it in register 1 ,

`[ cmp eq reg 1 0x0900007f ]` compare register 1 with `0x0900007f` ( `127.0.0.9` in network order) ,

`[ counter pkts 0 bytes 0 ]`  increment counter.

### N5.2.1 nft_rule

`struct nft_rule` is constructed for each rule. The expressions described  above are added to it

```c
struct nft_rule {
	struct list_head        list;    // to add rules into a list
	u64                     handle:42,       // handle to refer to the rule (see nft -a list ruleset)
							genmask:2,       // generation
							dlen:12,         // length of data
							udata:1;         
	unsigned char           data[]           // register data
};
```

The expressions are stored in `nft_rule->data`. Each expression is made up of a pointer to the ops structure and register specific data.

```c
(gdb) p ((struct nft_expr *)&rule->data[0] )->ops
$23 = (const struct nft_expr_ops *) 0xffffffff84683300 <nft_payload_fast_ops>
(gdb) p ((struct nft_expr *)&rule->data[0] )->ops->size
$24 = 16
(gdb) p ((struct nft_expr *)&rule->data[16] )->ops      # 0 + 16
$25 = (const struct nft_expr_ops *) 0xffffffff846824a0 <nft_cmp_fast_ops>
(gdb) p ((struct nft_expr *)&rule->data[16] )->ops->size
$26 = 24
(gdb) p ((struct nft_expr *)&rule->data[40] )->ops      # 16 + 24
$27 = (const struct nft_expr_ops *) 0xffffffff846881a0 <nft_counter_ops>
(gdb) p ((struct nft_expr *)&rule->data[40] )->ops->size
$28 = 16
(gdb) p rule->dlen                                  # 56 = 16 + 24 + 16 
$30 = 56
```



The first expression is to load payload into a register and the expression is stored in a `struct nft_payload` .

```c
(gdb) p ((struct nft_expr *)&rule->data[0] )->ops 
$43 = (const struct nft_expr_ops *) 0xffffffff84683300 <nft_payload_fast_ops>
(gdb) p *(struct nft_payload*)((struct nft_expr *)&rule->data[0] )->data
$44 = {base = NFT_PAYLOAD_NETWORK_HEADER,  // base header
       offset = 16 '\020',         // offset from header start
       dreg = 4 '\004',            // dest register
       len = 4 '\004',             // length of the expression (4 x u8)
      }

// [ payload load 4b @ network header + 16 => reg 1 ]
```

The eval function `nft_payload_eval` maps base to a header, reads the header offset and then calls `skb_copy_bits` to read bits into a register.



The next expression is to compare it to `127.0.0.9`

```c
(gdb) p ((struct nft_expr *)&rule->data[16] )->ops 
$48 = (const struct nft_expr_ops *) 0xffffffff846824a0 <nft_cmp_fast_ops>
(gdb) p/x *(struct nft_cmp_fast_expr*)((struct nft_expr *)&rule->data[16] )->data
$49 = {data = 0x900007f,       // 127.0.0.9
       mask = 0xffffffff,      // mask
       sreg = 0x4,             // source register
       inv = 0x0,              // inverse, for not equal  
       len = 0x20,             // len of the expression = 4 + 4 + 1 + 1 
      }
// [ cmp eq reg 1 0x0900007f ]
```



Finally the last expression is to increment a counter ( per-cpu data )

```c
(gdb) p ((struct nft_expr *)&rule->data[40] )->ops
$53 = (const struct nft_expr_ops *) 0xffffffff846881a0 <nft_counter_ops>
(gdb) p *(struct nft_counter_percpu_priv*)((struct nft_expr *)&rule->data[40])->data
$54 = {counter = 0x607fdda05ff0 }
// its percpu data

(gdb) p this_cpu
$84 = (struct nft_counter *) 0xffffe8ffffc05ff0
(gdb) p *this_cpu
$85 = {bytes = 504, packets = 6}

// [ counter pkts 5 bytes 504 ]
```