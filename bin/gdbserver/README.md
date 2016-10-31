# gdbserver

A GDB stub for Fuchsia that supports the
[GDB Remote Serial Protocol](https://sourceware.org/gdb/onlinedocs/gdb/Overview.html).

Refer to [this page](https://sourceware.org/gdb/onlinedocs/gdb/index.html) for
further documentation on debugging with GDB.

## Setting up the environment

The stub currently communicates with GDB over a TCP connection using BSD sockets
and thus requires a working Fuchsia network stack and a network connection
between the target and host environments. Follow the general network set up
instructions (click
[here](https://fuchsia.googlesource.com/magenta/+/master/docs/getting_started.md#Enabling-Networking-under-Qemu-x86_64-only)
for enabling network under QEMU)

**N.B.** If using QEMU make sure you are using version 2.5.0 or later.

See [this document](https://docs.google.com/a/google.com/document/d/1wKQbLgxKBsdlXX9iPSw4MHnTYN4Lx2xjzyoWyth-uy4/edit?usp=sharing) for setting up `netstack`.

## Debugging a program

### On the target machine running Fuchsia

First, make sure that the TCP stack is running and has acquired an IP address:

```
> netstack &
> ethernetif_init: opened an ethernet device
ip4_addr: 192.168.3.53 netmask: 255.255.255.0 gw: 192.168.3.1
ip6_addr[0]: FE80::5054::FF:FE12:3456
```

`192.168.3.53` is the address that we will use to connect from the host gdb
later.

Next, run the stub with your port number of choice (e.g. 7000) and a path to a
binary (the stub currently supports debugging a single inferior during its life
time, which needs to be passed in its command-line. Attaching to an already
running process is currently not supported):

```
debugserver 7000 /path/to/program
```

The stub has a `--debug=<debug-level>` option that can be used to increase log
verbosity. Currently only levels 1 and 2 are supported (but any positive integer
is currently accepted):

```
debugserver --debug=2 7000 /path/to/program
```

### On the host machine

Run gdb and configure it with certain parameters. The stub currently only
supports the non-stop execution mode and we need to configure gdb for it to work
correctly. We then use the `target extended-remote` command to connect to the IP
and port number that stub acquired earlier:

```
$ gdb
GNU gdb (GDB) <version>
...
(gdb) set pagination off
(gdb) set non-stop on
(gdb) set target-async on
(gdb) set architecture i386:x86-64
The target architecture is assumed to be i386:x86-64
(gdb) target extended-remote 192.168.3.53:7000
Remote debugging using 192.168.3.53:7000
```
From here we can use gdb as usual:

```
(gdb) # set some breakpoints
(gdb) run
Starting program:

Program received signal SIGSEGV, Segmentation fault.
0x00000000010f0069 in ?? ()
(gdb) # do stuff
```

TODO(armansito): Add instructions for using the `file` command for loading
symbols once that works.

## TODO

The following is an incomplete and unprioritized list of tasks that remain to be
finished for basic debugging to work:

- Retransmission on "-" ack
- Memory access following vRun but before "c".
- Support vRun with arguments so that the inferior can be provided from gdb
  using `set remote exec-file`
- sw/hw breakpoints and stepping
- vCont and friends
- Support architectures other than x86-64
