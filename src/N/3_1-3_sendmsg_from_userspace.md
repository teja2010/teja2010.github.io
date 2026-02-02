# N3.1-3.2 sendmsg() from userspace

### 3.1 sendmsg()

After opening a UDP socket the application gets a fd as a handle for the  underlying kernel socket. The application sends data into the socket by calling `send()` or `sendmsg()` or `sendto()`, all of which will send out UDP data.    

​      On calling the `sendmsg()` system call, the syscall trap will save the application's process context and switch to running the kernel code. Kernel code runs in the application context, i.e. any traces that print the pid of the process will return the application's pid. If you do not know this, believe me for now, signals & syscalls are explained in a  separate page. The kernel registers functions that are run for each system call. The registered function's name is `__do_sys_` + `syscall` name. In this case the function is `__do_sys_sendmsg()`, which internally      calls `__sys_sendmsg()`. The arguments passed to the call are the `fd`, the  `msghdr` struct and flags.    

The first step is to get the kernel socket from the fd. Each fd the application  holds is a handle to a kernel socket. The socket can be for an open file, a  UDP socket, UNIX socket, etc. The socket is usually represented  with the var `sock`. `___sys_sendmsg` is called with the sock, msghdr and      flags. It simply checks if the arguments are valid, copies the msghdr      (allocated in userspace) into kernel memory and then calls `sock_sendmsg()`.    

sock_sendmsg() checks if the application is allowed to proceed. Linux      has kernel modules like SELinux and AppArmour which audit each system      call and based on the configured rules allow or reject the system call.      If `security_socket_sendmsg()` does not return any errors, `sock_sendmsg_nosec()`      is called, which internally calls the `sock->ops->sendmsg()`.      socket ops are registered during socket creation based on the protocol family (`AF_INET`, `AF_INET6`, `AF_UNIX`) and socket type (`SOCK_DGRAM`, `SOCK_STREAM`).      Since a udp socket is a SOCK_DGRAM socket of AF_INET family the ops      registered are `inet_dgram_ops`, defined in `net/ipv4/af_inet.c`. And      `sock->ops->sendmsg()` is `inet_sendmsg()`.    

```c
SYSCALL_DEFINE3(sendmsg, int, fd, struct user_msghdr __user *, msg,
                unsigned int, flags)
{
    return __sys_sendmsg(fd, msg, flags) {
        sock = sockfd_lookup_light(fd, &err, &fput_needed);

        err = ___sys_sendmsg(sock, msg, &msg_sys, flags) {

            //copy msg (usr mem) into msg_sys (kernel mem)
            err = copy_msghdr_from_user(msg_sys, msg, NULL, &iov);
            sock_sendmsg(sock, msg_sys) {
                int err = security_socket_sendmsg();
                if(err)
                    return;

                sock_sendmsg_nosec(sock, msg_sys) {
                    sock->ops->sendmsg(sock, msg_sys);
                             //inet_sendmsg();
                }
            }
        }
    }
} 
```

### 2.2 UDP & IP sendmsg

At this point we will begin using the networking struct sock (different from a `socket`), represented usually with the var `sk`. Each socket will either have a valid sock or a file. In our case a `sock->sk` will contain a valid sock. We check if socket needs to be bound to a ephemeral port, and then call      `sk->sk_prot->sendmsg()`. During socket creation, the sock is added to the socket, and protocol handlers are registered to the sock. In this case,      for a UDP socket, `sk_prot` is set to `udp_prot` (defined in `net/ipv4/udp.c`).      And `sk_prot->sendmsg` is set to `udp_sendmsg()`. The arguments      have not changed, we will pass sk and msghdr.    

​      Till this point we have not begun constructing the packet, the focus was more on socket options. `udp_sendmsg` will first extract the destination address      (usually variable `daddr`) and dest port (usually  `dport`), from  the `msghdr->msg_name`. The source port is extracted from the sock. This information is passed to find a route for the packet. The first time a      packet is sent out of a sock, the route has to be found by going through      the routing tables. This route result is saved in `sk->sk_dst_cache`,      which is used for packets that are sent later. At this point the packet's      source address is extracted from the route. All the details about the      packet's flow are saved in `struct flowi4`, which are saddr, daddr, sport,      dport, protocol, tos (type of service), sock mark, UID (user identifier),      etc. We now have all the information, the addresses, ports and certain      information to fill in the IP header with. We can begin filling in the      packet. `ip_make_skb()` will create the skb, and the skb will be sent out by calling `udp_send_skb()`.    

```c
int inet_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
    inet_autobind(sk)
    DECLARE_SOCKADDR(struct sockaddr_in *, usin, msg->msg_name);

    daddr = usin->sin_addr.s_addr; // get daddr from msghdr
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

    skb = ip_make_skb(); // create skb(s)
    err = udp_send_skb(skb, fl4, &cork); // send it to ip layer
} 
```