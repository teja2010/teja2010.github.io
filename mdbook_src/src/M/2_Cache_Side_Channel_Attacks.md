# Cache Side Channel Attacks

By design caches are transparent, but processes can create contention resulting in timing variations.    CPU data cache timing attacks are to exploit cache timing to expose cache's internal state. 

Caches are one place where time variations can be exposed.  Caches are built to bridge the gap between main memory and Processor  registers. Since it takes fewer cycles to read from cache than from main memory, recording time differences makes reading from cache detectable.

### Caches Recap:

- Caches are designed to exploit spatial locality in memory access
- For each read, check if the data is present in one of the  caches. If present, read from the cache or load from lower memory. LRU  to evict old entries. 
- Cold cache misses are when the cache is empty, happens during program load.
- Data can only be added to certain regions in the cache.  Multiple address lines in lower memory map to a subset in the cache.  Conflict miss happens when data that map to the same region are being  loaded, causing unnecessary cache evictions.
- If cache size is larger than the working set of memory, we face capacity misses.
- Write through cache: each write is immediately written to lower levels. Simple logic, but increases bus traffic
- Write-back cache: write to cache, defer updating lower levels till it is evicted from the cache.
- Cache Coherence: The same data can be present on multiple  caches, if one of the processors makes a change to common data, the  processor needs to make sure that cached data is consistent.
- A Cache Coherence protocol makes sure of cache coherence  among caches. Cache coherence protocols described below are snooping  protocols, i.e. changes to cache traffic is visible to all cores.
- Write-invalidate: On detecting a write to another cached location, invalidate local copy, forcing a read from main memory.
- Write-update: On detecting a write, update local copy.
- Any cache coherence protocol increases bus traffic. 
- Article on cache coherence : [https://fgiesen.wordpress.com/2014/07/07/cache-coherency/](https://fgiesen.wordpress.com/2014/07/07/cache-coherency/) 
- Notes on CPU caches: [https://www.cse.iitb.ac.in/~mythili/os/notes/notes-cache.txt](https://www.cse.iitb.ac.in/~mythili/os/notes/notes-cache.txt)
  - has a clean explanation of MESI cache coherence protocol.
- Code patterns to improve cache performance:          
  - Build loops, which operate on the same data. Merge loops if necessary
  - Within a data structure, make sure critical (read/write heavy) elements are cache aligned and at the top.
  - Reduce branches so code can be prefetched.
  - Atomic variables are allowed to be present only in one  cache. Use of atomic variables (and hence locks and synchronization  mechanisms) on multiple cores increases cache coherence traffic. 
  - Instead try to use per CPU data structures.

### Flush+Reload Paradigm.      

- Exploits Cache behavior to leak information about victim process.
- Flush a line, wait for some time, measure access time. If  the victim has accessed the memory, access time will be in cache and  access will be fast. Else will be slow.
- Repeat to identify victim's memory regions.
- Slides Link: [https://cs.adelaide.edu.au/~yval/CHES16/](https://cs.adelaide.edu.au/~yval/CHES16/)

### Spectre 1: Bounds Check Bypass:

- Modern CPUs can speculatively execute instructions in a branch, before a branch true execution starts. 

- ```
  int some_function(int user_input)
  {
      int x = 0;
      int flush_reload_buffer[256];
      if (check user_input) {  // on checking user_input, undo changes to x
          x = secret_array[user_input];  // this is executed. before check.
                                         // contents leak into the cache.
      }
      y = flush_reload_buffer[x];
      leak = measure_access_time_of_all_elements(flush_reload_buffer);
  } 
  ```

- The CPU based on historic data, tries to predict branches and  executes instructions in the branch. Later it checks the branch check,  if the instructions it ran were incorrect, it undoes all the changes.  These changes are transparent for the user. But the instructions in the  branch load data into the cache. This data can then be identified by  cache timing.
- This can be used to read data from the kernel via eBPF JIT.  Users can provide eBPF  bytecode which the kernel runs at certain kernel hooks. The bytecode is translated into machine code using a JIT engine.
- HW Solution is to clean up cache while undoing changes. 

### Spectre 2: Branch target injection:

- Independent execution of the same code, can influence branch predictions. Earlier people worked on inferring  details about the  victim based on attacker's branch prediction. This attack instead forces victim's branch prediction due to attacker's choice of branch  execution.
- Most branch predictors maintain a table of branches to jump  addresses. The last 12 bits are used to identify the branches and  addresses.
- Since Only the last 12 bits are used, the attacker fills the table with branch and jump entries. The CPU while executing the victim  code enters the branch and loads the secret into the cache, which is  then read by the attacker. Note that the victim might itself have never  entered the branch, but the CPU speculatively executes the branch.
- This can be used to read the host's memory from a KVM guest.
- HW Solution: Update microcode so OS can control branch prediction table. 
- SW Retpoline Solution: Retpoline = return + trampoline. An  indirect branch that is not subject to speculative execution. Instead of a "jmp" use a call and return. [Google Blog.](https://support.google.com/faqs/answer/7625886)

### Spectre 3: Rogue data cache load

- This attack exploits the fact that the CPU executes instructions in parallel, sometimes out of order to achieve parallelism.
- ```
  mov rax, [Somekerneladdress]
  mov rbx, [someusermodeaddress] 
  ```

- In the above example, the first instruction causes an  exception. But if the CPU runs both of them in parallel, the second one  can load user data into the cache before the exception occurs. Using  Flush+Reload data address can be identified.

### Meltdown:

- This attack specifically tries to read kernel data from  userspace. It does this by trying to load a kernel address. The CPU loads the address and then raises an exception. This loads the data into the  cache which is then read using Flush+Reload. Similar to Spectre v3 but  the implementation is specific to Intel.

### RIDL:      

- Other parts of the CPU from which similar data can leak. [https://mdsattacks.com/](https://mdsattacks.com/)
- [https://mdsattacks.com/slides/slides.html](https://mdsattacks.com/slides/slides.html)

### Mitigations

KAISER: an implementation to hide kernel data from userspace. [Link](https://lwn.net/Articles/738975/)

Meltdown and Spectre cause a performance Impact of 8 to 20% to IO intensive workloads, <5 % impact on CPU intensive. ([Source](https://access.redhat.com/articles/3307751))

On my AMD Ryzen 5 PRO 3500U Laptop:

```
$ grep . /sys/devices/system/cpu/vulnerabilities/*
itlb_multihit:Not affected
l1tf:Not affected
mds:Not affected
meltdown:Not affected
spec_store_bypass:Mitigation: Speculative Store Bypass disabled via prctl and seccomp
spectre_v1:Mitigation: usercopy/swapgs barriers and __user pointer sanitization
spectre_v2:Mitigation: Full AMD retpoline, IBPB: conditional, STIBP: disabled, RSB filling
tsx_async_abort:Not affected 
```

- Google zero blog [link](https://googleprojectzero.blogspot.com/2018/01/reading-privileged-memory-with-side.html) .