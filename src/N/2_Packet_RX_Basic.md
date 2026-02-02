# N2. Packet RX path 1 : Basic

This page describes the code flow while receiving a packet. It begins with the packet just entering the core networking code, top half and bottom half processing, basic flow through the IP and UDP layers and finally the packet is enqueued into a socket queue.

Additionally hash based software packet steering across CPUs (RPS and RSS), Inter Processor Interrupts (IPI) and scheduling softirq processing is described. They can be ignored in the first read and can be revisited in later runs after gaining additional context. These sections have been marked OPTIONAL.
NAPI will be described in later pages, all NAPI APIs are ignored now.






