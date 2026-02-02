# N1. Setup UML (older)

SKIP THIS IF YOU WERE ABLE TO SETUP QEMU. This page is here only for completeness, but is not necessary to experiment with a kernel.

----



I prefer to learn/tinker with linux using UML. UML is User mode linux, which is a simple VM. It emulates a uniprocessor linux system, and can run even on machines with very old hardware (like my laptop). Check the UML homepage for additional details. This page is just to a simple tutorial on how to build and run it. I have also added sections on how to attach it to GDB for easy debugging and a section on how to setup basic networking between multiple UMLs which you can skip in the first read. 

### N0.1 Clone and build the kernel

Clone the kernel, and build it.
Make defconfig sets up the default kernel config. The kernel config is a set of kernel features which will either be compiled into the kernel or will be compiled as modules. The architecture for which we are configuring is the UM (user mode) architechture. While building the config, you may be prompted to choose a configuration. Press enter to choose the default configuration.
Once the configuration is done, a ".config" file will be populated with the chosen options.
Begin compiling the code. Based on your machine's CPU capabities, replace 1 with the number of parallel compilation jobs you want make to run. Please remain patient as the very first compilation will take some time.

```bash
git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git linux
cd linux
make defconfig ARCH=um
make -j1 ARCH=um 
```

A new file "linux" will be created. This is the UML executable which runs as an application on the host machine. It needs a few other files and arguments to run, which will be explained in the next section.

### N0.1.1 Kernel config [OPTIONAL]

The kernel src includes a neat way to configure the kernel. Run make menuconfig to configure various options. A ncurses interface should start up, with the instructions provided in the top. Pressing Y includes a config and N excludes a config. After configuring the kernel, save and exit the config. The .config file will get updated with the necessary configuration.
To build the uml binary with debug symbols, edit KBUILD_HOSTCFLAGS in the Makefile. Just add a '-g' option at the end. The kernel can then be rebuilt with the new configuration.

```
make menuconfig ARCH=um
make -j1 ARCH=um 
```

Makefile diff:

```
 HOSTCC       = gcc
 HOSTCXX      = g++
-KBUILD_HOSTCFLAGS   := -Wall -Wmissing-prototypes -Wstrict-prototypes -O2 \
+KBUILD_HOSTCFLAGS   := -Wall -Wmissing-prototypes -Wstrict-prototypes -O2 -g \
                -fomit-frame-pointer -std=gnu89 $(HOST_LFS_CFLAGS) \
                $(HOSTCFLAGS)
 KBUILD_HOSTCXXFLAGS := -O2 $(HOST_LFS_CFLAGS) $(HOSTCXXFLAGS) 
```

### N0.2 Rootfs setup

 We have a compiled kernel but we still need a init script and other userspace  programs. We need a rootfs to emulate the disk. There are a lot of blogs  which describe how to build a rootfs. We will download a debian rootfs that is  provided by google. 
Run the following command to download the rootfs and uncompress it. On  uncompressing it a 1GiB net_test.rootfs.20150203 should be available.  

```
wget -nv https://dl.google.com/dl/android/net_test.rootfs.20150203.xz
unxz net_test.rootfs.20150203.xz 
```

Note: If the above download does not work, check the [Google nettest script](https://android.googlesource.com/kernel/tests/+/master/net/test/run_net_test.sh#83) where the rootfs link can be found. Google uses    UML to run nettests on the kernels OEMs ship to check for possible bugs.    
We now have everything needed to run the UML.  

.2.1 Adding files/programs to rootfs [OPTIONAL]

The rootfs now contains certain programs and a init script which can be used to boot into the UML. We may need to install programs for our testing or need to move files between UML and the host OS. By mounting it into a directory the rootfs contents can be accessed.

```
mkdir temp
sudo mount net_test.rootfs.20150203 temp
```

Copying from/to the directory is equivalent to copying files from/to the UML. In the below example I am copying my tmux configuration files into the rootfs. When I boot into UML, I will find the config file in the home directory. (Super user privilages are needed to add/remove files from the rootfs). 

```
sudo cp ~/.tmux.conf temp/home/ 
```

By chroot-ing into the mounted directory, any necessary programs can be installed. On running chroot, you will able to edit the rootfs with super user privilages. I am installing tmux in the below code and then exiting the chroot shell.

```
sudo chroot temp
apt-get update
apt-get install tmux
exit  
```

Once all the changes are done, unmount the rootfs.  

```
sudo umount temp
```



#### 0.2.1.1 apt-get is failing

Note: apt-get update may fail printing the following error:

```
Err http://ftp.jp.debian.org wheezy Release.gpg
Connection failed
```

 

If the /etc/hosts file contains an address for a particular hostname, the  system will not do an additional DNS lookup. In this case, the address  corresponding to ftp.jp.debian.org is incorrect, causing apt-get to fail.  Run the command "dig ftp.jp.debian.org" in the host machine (not within chroot) to get the right address. Update the IP address to "133.5.166.3".  Finally `/etc/hosts` should contain the following entries:


```
127.0.0.1       localhost
::1             localhost ip6-localhost ip6-loopback
fe00::0         ip6-localnet
ff00::0         ip6-mcastprefix
ff02::1         ip6-allnodes
ff02::2         ip6-allrouters

133.5.166.3 ftp.jp.debian.org  
```

#### 0.2.1.2 apt-get is still failing !!!

OK, dont worry, download my rootfs from my github repo [here](https://github.com/teja2010/teja2010.github.io/tree/master/old/extra).

#### 0.3 Take it for a spin

Finally if all this works out you are ready to start a UML instance.    Just run the following command, where you provide path to rootfs as the value    against "ubda", and 256MiB as the RAM. DO NOT forget the "M" in 256M, else    UML will try to boot with just to 256Bytes of RAM, and fail :). I usually    provide 256MiB RAM, you can go as low as 100 or 50MiB.  

```
./linux ubda=net_test.rootfs.20150203 mem=256M 
```

You will see the VM booting, printing dmesg, bringing up the various kernel    susbsystems.    Finally when a promt to enter the password appears, enter root as the    password. Play around with the tiny VM. When the fun ends, run "halt" to    shutdown UML.  

#### 0.4 Attach GDB

It is fun to add breakpoints and view specific code in GDB. To do this,    we have to first find the process id (PID) of the main UML process.    Run the following command to find the UML pid. The output should show multiple PIDs


```bash
$ ps aux | grep ubda

0 t teja     27089 12160  2  80   0 - 17629 ptrace 16:52 pts/6    00:00:18 ./linux ubda=../rootfs_pool/net_test.rootfs.20150203 mem=50M
1 S teja     27094 27089  0  80   0 - 17629 read_e 16:52 pts/6    00:00:00 ./linux ubda=../rootfs_pool/net_test.rootfs.20150203 mem=50M
1 S teja     27095 27089  0  80   0 - 17629 poll_s 16:52 pts/6    00:00:00 ./linux ubda=../rootfs_pool/net_test.rootfs.20150203 mem=50M
1 S teja     27096 27089  0  80   0 - 17629 poll_s 16:52 pts/6    00:00:00 ./linux ubda=../rootfs_pool/net_test.rootfs.20150203 mem=50M
1 t teja     27097 27089  0  80   0 -  5206 ptrace 16:52 pts/6    00:00:00 ./linux ubda=../rootfs_pool/net_test.rootfs.20150203 mem=50M
1 t teja     27288 27089  0  80   0 -  5339 ptrace 16:52 pts/6    00:00:00 ./linux ubda=../rootfs_pool/net_test.rootfs.20150203 mem=50M
1 t teja     27352 27089  0  80   0 -  4990 ptrace 16:52 pts/6    00:00:00 ./linux ubda=../rootfs_pool/net_test.rootfs.20150203 mem=50M
1 t teja     27353 27089  0  80   0 -  5294 ptrace 16:52 pts/6    00:00:00 ./linux ubda=../rootfs_pool/net_test.rootfs.20150203 mem=50M
1 t teja     29405 27089  0  80   0 -  5003 ptrace 16:53 pts/6    00:00:00 ./linux ubda=../rootfs_pool/net_test.rootfs.20150203 mem=50M
1 t teja     29408 27089  0  80   0 -  5390 ptrace 16:53 pts/6    00:00:00 ./linux ubda=../rootfs_pool/net_test.rootfs.20150203 mem=50M
1 t teja     29410 27089  1  80   0 -  5717 ptrace 16:53 pts/6    00:00:08 ./linux ubda=../rootfs_pool/net_test.rootfs.20150203 mem=50M
0 S teja     30278  3682  0  80   0 -  5500 pipe_w 17:05 pts/1    00:00:00 grep --color=auto ubda 
```

The third column is the process id and the fourth column is the parent PID.  In the above output PID 27089 starts and then spawns the other threads. Attach gdb to the the main parent thread, which is 27089 in the above example.  

```
sudo gdb -p 27089
```

GDB will read the symbols from linux and attach itself. Play around, check    the backtrace, etc. All globals are now accessable.  

### 0.5 A private UML subnet

In most cases we want to play around with two UMLs connected to each other    and ignorant of the rest of the world. In such cases, the simplest way is to    connect them using the mcast transport.
Make copies of the rootfs, for each UML instance. In this case have two    copies ready, and run them with one additional argument "eth0". This will    add an additional eth0 interface. We set eth0 to mcast. i.e. the eth0    interfaces in both the instances are connected over mcast.  

```
./linux ubda=net_test.rootfs.20150203_1 mem=256M eth0=mcast
./linux ubda=net_test.rootfs.20150203_2 mem=256M eth0=mcast 
```

Assign addresses to the eth0 interfaces, and they are ready. You try pinging    the other UML. Command to assign address is:  

```
ip address add 192.168.1.1/24 dev eth0  
```

The full mcast readme is    [here](http://user-mode-linux.sourceforge.net/old/text/mcast.txt).    It contains details to create more complex mcast networks.

On assigning mcast to a eth device, each UML instance opens sockets which    listen to multicast traffic. If a multicast address is not assigned (like    above), all the instances listen to 239.192.168.1 . All packets are received    and then filtered out based on destination MAC address. The packets    can even be seen in packetdumps collected in the host OS.    Quoting from the above readme, "It's basically like an ethernet    where all machines have promiscuous mode turned on". Bad for performance,    but very easy to setup.  

