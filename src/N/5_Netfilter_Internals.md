# N5. (WIP) Netfilter Internals



Links:

- [intro to nftables](https://www.youtube.com/watch?v=cODU94yVxDs)
- [netfilter developer docs](https://wiki.nftables.org/wiki-nftables/index.php/Portal:DeveloperDocs)
  - [nftables internals](https://wiki.nftables.org/wiki-nftables/index.php/Portal:DeveloperDocs/nftables_internals) 
  - [sets internals](https://wiki.nftables.org/wiki-nftables/index.php/Portal:DeveloperDocs/nftables_internals)



---

These notes will mostly cover the nftable portion. The hooks into a netfilter are the same, but the way rules are created and composed is different for iptables.

We will start with a simple rule to count packets and see how this rule is actually implemented within netfilter. Next we will slowly improve and add parts to the rule to learn other features.

