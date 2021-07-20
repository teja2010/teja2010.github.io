# N1 Setup Qemu

These notes are based on the following videos:

1. [Running an external custom kernel in Fedora 32 under QEMU: Kernel Debugging Part 1](https://www.youtube.com/watch?v=OdybsP9cQNA) I am more used to Ubuntu, so I chose it instead.
2. [GDB on the Linux Kernel](https://www.youtube.com/watch?v=unizGCcZg3Y). 

The script below is the same as the one described in these videos. 

**Please watch both the videos before continuing further.**

----



Like described in the article on UML Setup, I like to learn by running a VM and attaching it via GDB. KVM and QEMU are the newer and well supported VM solutions. This page is a tutorial on how to launch a debug instance in QEMU and attach to it using GDB.

Install QEMU (check Qemu download page for distribution specific instructions). 

### N1.1 Setup & build a kernel with debug symbols

Clone the kernel from kernel git repo. 

```shell
git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git linux
cd linux 
```

Next run "make menuconfig" to modify a few configurations. On running the command a ncurses interface to enable/disable options will open. Use the arrow keys to move between options and 'Y' and 'N' keys to include/exclude options.
First enable CONFIG_DEBUG_KERNEL. It is located under "Kernel hacking" as "Kernel debugging".
Next disable RANDOMIZE_MEMORY_PHYSICAL_PADDING. It is located under "Processor type and features" as "Randomize the kernel memory sections".
After making the changes save the changes and exit the menuconfig interface. A lot of youtube videos explain the same in detail, check them if confused. To reset any changes delete the ".config" file and start over by running "make defconfig". Finally run make.
It will take some to compile the kernel. Meanwhile move to the next step and install a VM in qemu. 

    make defconfig
    make menuconfig     # enable CONFIG_DEBUG_KERNEL,
                        # disable RANDOMIZE_MEMORY_PHYSICAL_PADDING
    
    make -j1            # replace 1 with the number of CPUs that make
                        # should use to build the kernel.  



### N1.2 Create an image for QEMU

Move out of the linux directory and create an image for QEMU.

 `qemu-img create kernel-dev.img 20G` 

Next download ubuntu's server iso image from [here](https://ubuntu.com/download/server). It is a ~1GB file which can be used for a Linux server. You can alternatively install the desktop version if you are more comfortable with a GUI. Move the downloaded iso file into the same directory.
Finally save the script below as start.sh.

```bash
#!/bin/bash

#startup.sh

KERNEL="linux/arch/x86_64/boot/bzImage"
RAM=1G
CPUS=2
DISK="kernel-dev.img"

if [ $# -eq 1 ]
then
	qemu-system-x86_64 \
		-enable-kvm \
		-smp $CPUS \
		-drive file=$DISK,format=raw,index=0,media=disk \
		-m $RAM \
		-serial stdio \
		-drive file=$1,index=2,media=cdrom  ## comment to run vanilla install
		# use this option to boot using a cd iso
else
	qemu-system-x86_64 \
		-enable-kvm \
		-smp $CPUS \
		-drive file=$DISK,format=raw,index=0,media=disk \
		-m $RAM \
		-serial stdio \
		-kernel $KERNEL \
		-initrd initrd.img \
		-S -s \
		-cpu host \
		-append "root=/dev/mapper/ubuntu--vg-ubuntu--lv ro nokaslr" \
		-net user,hostfwd=tcp::5555-:22 -net nic \
		# use this option to run debug kernel
		# see the video 1 on how to pull the initrd.img
		# the "root=/dev/ ..." command needs to pulled from grub.cfg (see video 1)
fi
```

The above script when given an ISO file passes it as an CD to the QEMU instance. This way we can install ubuntu into "kernel-dev.img". 

If no arguments are provided it tries to run the OS installed on kernel-dev.img. This way we can use the script to start the VM after we have completed installing ubuntu. At this point the directory structure should look like this:

```
  .
  ├── kernel-dev.img
  ├── linux
  ├── ubuntu-21.04-live-server-amd64.iso
  └── startup.sh 
```



First to install ubuntu run: (if superuser privileges needed, run with sudo) 

```
./startup.sh ubuntu-21.04-live-server-amd64.iso
```

Go through all the steps and install ubuntu. A lot of YouTube videos show the  complete process. Use them as a reference if necessary.  

Once the installation is complete, comment out  the line which provides the cdrom option to boot into an vanilla ubuntu install.  

```
./startup.sh ubuntu-21.04-live-server-amd64.iso  #cdrom line commented
```

Now wait for the kernel compilation to complete. Then run the script without the arguments to boot into the kernel we built.

```
./startup.sh
```

The boot will wait for GDB to connect. On a separate terminal run:

```bash
gdb linux/vmlinux
```

Within the gdb prompt then run "target remote :1234" to connect to QEMU. The `bt` command should then show some stack within QEMU.
Run "hbreak start_kernel" to add a hardware breakpoint at start_kernel() and then run "continue". The VM would then begin booting and will stop in the start_kernel function.

Add other hardware breakpoints (since QEMU uses hardware acceleration for Virtualization, normal SW breakpoints will not work) and start tinkering.
The network options are similar to those of UML. Thses options have been commented in the above script. 

### N1.3 Network setup

By default the script provides an interface via which the VM can access both internet and the host machine (over ssh). This is the SLIRP networking mode. Follow the link [here](https://wiki.qemu.org/Documentation/Networking#User_Networking_.28SLIRP.29) to read more.

The interface will be created. If the interface does not have an address, run dhclient on the interface so it is assigned an address. Next install an sshserver, if not installed during ubuntu installation, so we can access the guest over ssh.

```bash
sudo dhclient eth0  # provide the right interface name.
sudo apt update     # needed to update apt cache.
sudo apt install openssh-server
```

Finally to login into the guest machine, run:

```
ssh -p 5555 localhost 
```

