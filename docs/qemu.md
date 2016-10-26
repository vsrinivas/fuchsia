# Qemu

## Build Qemu

If you don't want to install in /usr/local (the default), which will require you
to be root, add --prefix=/path/to/install  (perhaps $HOME/qemu) and then you'll
need to add /path/to/install/bin to your PATH.

```
cd $SRC
git clone https://fuchsia.googlesource.com/third_party/qemu
cd qemu
./configure --target-list=arm-softmmu,aarch64-softmmu,x86_64-softmmu
make -j32
sudo make install
```

## Run Magenta under Qemu

```
# for aarch64
./scripts/run-magenta-arm64

# for x86-64
./scripts/run-magenta-x86-64
```

The -h flag will list a number of options, including things like -b to rebuild first
if necessary and -g to run with a graphical framebuffer.

To exit qemu, enter Ctrl-a x. Use Ctrl-a h to see other commands.

## Enabling Networking under Qemu (x86-64 only)

The run-magenta-x86-64 script, when given the -N argument will attempt to create
a network interface using the Linux tun/tap network device named "qemu".  Qemu
does not need to be run with any special privileges for this, but you need to
create a persistent tun/tap device ahead of time (which does require you be root):

On Linux:

```
sudo apt-get install uml-utilities
sudo tunctl -u $USER -t qemu
sudo ifconfig qemu up
```

This is sufficient to enable link local IPv6 (as the loglistener tool uses).

On macOS:

macOS does not support tun/tap devices out of the box, however there is a widely
used set of kernel extensions called tuntaposx which can be downloaded
[here](http://tuntaposx.sourceforge.net/download.xhtml). Once the installer
completes, the extensions will create up to 16 tun/tap devices. The
run-magenta-x86-64 script uses /dev/tap0.

```
sudo chown $USER /dev/tap0

# Run magenta in QEMU, which will open /dev/tap0
./scripts/run-magenta-x86-64 -N

# (In a different window) bring up tap0 with a link local IPv6 address
sudo ifconfig tap0 inet6 fc00::/7 up
```

**NOTE**: One caveat with tuntaposx is that the network interface will
automatically go down when QEMU exits and closes the network device. So the
network interface needs to be brought back up each time QEMU is restarted.

## Debugging the kernel with GDB

### Sample session

Here is a sample session to get you started.

In the shell you're running Qemu in:

```
shell1$ ./scripts/run-magenta-x86-64 -- -s -S
[... some Qemu start up text ...]
```

And then in the shell you're running GDB in:
[Commands here are fully spelled out, but remember most can be abbreviated.]

```
shell2$ gdb build-magenta-pc-x86-64/magenta.elf
(gdb) target extended-remote :1234
Remote debugging using :1234
0x000000000000fff0 in ?? ()
(gdb) # Don't try to do too much at this point.
(gdb) # GDB can't handle architecture switching in one session,
(gdb) # and at this point the architecture is 16-bit x86.
(gdb) break lk_main
Breakpoint 1 at 0xffffffff8010cb58: file kernel/top/main.c, line 59.
(gdb) continue
Continuing.

Breakpoint 1, lk_main (arg0=1, arg1=18446744071568293116, arg2=0, arg3=0)
    at kernel/top/main.c:59
59	{
(gdb) continue
```

At this point Magenta boots and back in shell1 you'll be at the Magenta
prompt.

```
mxsh>
```

If you Ctrl-C in shell2 at this point you can get back to GDB.

```
(gdb) # Having just done "continue"
^C
Program received signal SIGINT, Interrupt.
arch_idle () at kernel/arch/x86/64/ops.S:32
32	    ret
(gdb) info threads
  Id   Target Id         Frame
  4    Thread 4 (CPU#3 [halted ]) arch_idle () at kernel/arch/x86/64/ops.S:32
  3    Thread 3 (CPU#2 [halted ]) arch_idle () at kernel/arch/x86/64/ops.S:32
  2    Thread 2 (CPU#1 [halted ]) arch_idle () at kernel/arch/x86/64/ops.S:32
* 1    Thread 1 (CPU#0 [halted ]) arch_idle () at kernel/arch/x86/64/ops.S:32
```

Qemu reports one thread to GDB for each CPU.

### The magenta.elf-gdb.py script

The scripts/magenta.elf-gdb.py script is automagically loaded by gdb.
It provides several things:

- Pretty-printers for magenta objects (alas none at the moment).

- Several magenta specific commands, all with a "magenta" prefix. To see them:
```
(gdb) help info magenta
(gdb) help set magenta
(gdb) help show magenta
```

- Enhanced unwinder support for automagic unwinding through kernel faults.

Heads up: This script isn't always updated as magenta changes.

### Terminating the session

To terminate Qemu you can send commands to Qemu from GDB:

```
(gdb) monitor quit
(gdb) quit
```

### Interacting with Qemu from Gdb

To see the list of Qemu commands you can execute from GDB:

```
(gdb) monitor help
```
