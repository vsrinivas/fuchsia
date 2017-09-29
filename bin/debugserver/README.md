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
[here](https://fuchsia.googlesource.com/zircon/+/master/docs/qemu.md#Enabling-Networking-under-Qemu-x86_64-only)
for enabling network under QEMU)

**N.B.** If using QEMU make sure you are using version 2.5.0 or later.

See [this document](https://fuchsia.googlesource.com/netstack/+/master/README.md) for setting up `netstack`.

## Debugging a program

### Build gdb

Fuchsia support is not in upstream GDB yet, so the Fuchsia copy of GDB
must be used: https://fuchsia.googlesource.com/third_party/gdb.

```
$ cd fuchsia
fuchsia$ jiri import gdb https://fuchsia.googlesource.com/manifest
fuchsia$ jiri update
fuchsia$ sh scripts/gdb/build-gdb.sh
```

The gdb binary can be found in `out/toolchain/gdb-x86_64-linux/bin/gdb`.

### On the target machine running Fuchsia

First, make sure that the TCP stack is running and has acquired an IP address.
Zircon should print its ipv4 address when it boots.
GDB doesn't currently support ipv6.

**N.B.** The following step is only necessary if qemu is started with netsvc
disabled. Look for the log messages below in the system boot logs.

```
> netstack &
> ethernetif_init: opened an ethernet device
ip4_addr: 192.168.3.53 netmask: 255.255.255.0 gw: 192.168.3.1
ip6_addr[0]: FE80::5054::FF:FE12:3456
```

`192.168.3.53` is the address that we will use to connect from the host gdb
later.

Next, run the stub with your port number of choice (e.g. 7000) and a path to a
binary. The stub currently supports debugging a single inferior during its life
time. The program, with optional arguments, can be passed on the command line
to gdbserver, or can be specified from within gdb with the
`set remote exec-file /target/path/to/program` command.
Attaching to an already running process is currently not supported.

The gdbserver binary is named `debugserver`.

```
debugserver 7000 /path/to/program
```

The stub has a `--verbose=<verbosity-level>` option that can be used to
increase log verbosity. Currently only levels 1 and 2 are supported (but any
positive integer is currently accepted.

This example uses the debugger unit test program, passing "segfault" as
an argument to trigger a segfault (to help exercise the debugger).

```
debugserver --verbose=2 7000 /boot/test/debugger-test segfault
```

### On the host machine

Use the `target extended-remote` command to connect to the IP
and port number that stub acquired earlier:

```
fuchsia$ out/toolchain/gdb-x86_64-linux/bin/gdb
GNU gdb (GDB) <version>
...
Hi, this is fuchsia.py. Adding Fuchsia support.
Setting fuchsia defaults. 'help set-fuchsia-defaults' for details.
(gdb) target extended-remote 192.168.3.53:7000
Remote debugging using 192.168.3.53:7000
```

From here we can use gdb as usual:

```
(gdb) file out/build-zircon/build-zircon-pc-x86-64/utest/debugger/debugger.elf
(gdb) break main
(gdb) run
Starting program: ...
...
Breakpoint 1, main (argc=2, argv=0x10f3e20)
    at system/utest/debugger/debugger.c:530
530	{
(gdb) continue
Continuing.

Program received signal SIGSEGV, Segmentation fault.
0x00000000010f5d79 in test_segfault_leaf (n=10, p=0x10f3cc0)
    at system/utest/debugger/debugger.c:471
471	    *crashing_ptr = x[0];
(gdb) backtrace
#0  0x00000000010f5d79 in test_segfault_leaf (n=10, p=0x10f3cc0)
    at system/utest/debugger/debugger.c:471
#1  0x00000000010f5df1 in test_segfault_doit1 (p=0x10f3cc0)
    at system/utest/debugger/debugger.c:485
#2  0x00000000010f5e39 in test_segfault_doit2 (p=0x10f3cc0)
    at system/utest/debugger/debugger.c:490
#3  0x00000000010f5dcc in test_segfault_doit1 (p=<optimized out>)
    at system/utest/debugger/debugger.c:483
#4  0x00000000010f5e39 in test_segfault_doit2 (p=0x10f3d00)
    at system/utest/debugger/debugger.c:490
#5  0x00000000010f5dcc in test_segfault_doit1 (p=<optimized out>)
    at system/utest/debugger/debugger.c:483
#6  0x00000000010f5e39 in test_segfault_doit2 (p=0x10f3d40)
    at system/utest/debugger/debugger.c:490
#7  0x00000000010f5dcc in test_segfault_doit1 (p=<optimized out>)
    at system/utest/debugger/debugger.c:483
#8  0x00000000010f5e39 in test_segfault_doit2 (p=0x10f3d80)
    at system/utest/debugger/debugger.c:490
#9  0x00000000010f5dcc in test_segfault_doit1 (p=<optimized out>)
    at system/utest/debugger/debugger.c:483
#10 0x00000000010f5e23 in test_segfault ()
    at system/utest/debugger/debugger.c:499
#11 0x00000000010f5617 in main (argc=2, argv=0x10f3e20)
    at system/utest/debugger/debugger.c:540
(gdb) do stuff
```

## Same example, specifying program from gdb

On Fuchsia:

```
$ debugserver 7000
```

On Linux:

```
fuchsia$ out/toolchain/gdb-x86_64-linux/bin/gdb
(gdb) file out/build-zircon/build-zircon-pc-x86-64/utest/debugger/debugger.elf
(gdb) tar ext 192.168.3.53:7000
(gdb) set remote exec-file /boot/test/debugger-test
(gdb) r segfault
...
Program received signal SIGSEGV, Segmentation fault.
0x00000000010f5d79 in test_segfault_leaf (n=10, p=0x10f3cc0)
    at system/utest/debugger/debugger.c:471
471	    *crashing_ptr = x[0];
(gdb) # do stuff
```

## Issuing debugserver commands from gdb

Debugserver recognizes the gdb "monitor" command:

```
(gdb) monitor help
help - print this help
exit - quit debugserver
quit - quit debugserver
```

## Notes

### Logging output

gdbserver's logging output is a bit verbose at the moment, and
some output that claims to be "errors" aren't really errors per se.
In general, ignore them unless your gdb session isn't working as expected.

### Non-stop mode

At the moment Fuchsia only supports the "non-stop" mode of GDB:
When a thread stops all other threads are left running.
See https://sourceware.org/gdb/current/onlinedocs/gdb/Non_002dStop-Mode.html#Non_002dStop-Mode

### Shared library complaints

You may get complaints about unable to load symbols for shared libraries.

```
warning: Could not load shared library symbols for 3 libraries, e.g. libtest-utils.so.
Use the "info sharedlibrary" command to see the complete listing.
Do you need "set solib-search-path" or "set sysroot"?
```

In general you can ignore these.

### Limitations

The current Fuchsia GDB port is very preliminary.
Expect problems and missing features!
But you should be able to set breakpoints, run, and examine program
state when it crashes.

### When things aren't working at all

It is absolutely necessary (at the moment) that gdb be able to load
symbols for ld.so.1. If things aren't working at all, that is the first
place to look.

Do both of these files exist?

```
fuchsia$ ls -l out/sysroot/x86_64-fuchsia/debug-info/{libc.so,ld.so.1}
lrwxrwxrwx 1 dje eng       7 Dec 19 13:16 out/sysroot/x86_64-fuchsia/debug-info/ld.so.1 -> libc.so
-rwxr-x--- 1 dje eng 3435712 Dec 19 13:16 out/sysroot/x86_64-fuchsia/debug-info/libc.so
```

Does libc.so have debug information?
A check for it being > 3M in size is normally good enough.

The reason for this is due to how Fuchsia runs programs.
All programs in Fuchsia are PIE executables, and GDB needs to know
its runtime load address in order for most things to work. [There are
caveats and qualifiers of course, elided for brevity.]
When a program is started the dynamic linker is loaded into memory
but the program itself is not loaded yet. The dynamic linker will load the
program into memory later but it's not until then that the load address
of the program is known. One issue is "How much later?" There is a
special function in the dynamic linker, `dl_debug_state()`, that the
dynamic linker calls when the main executable, and any shared libraries
it uses, have been loaded into memory. GDB sets a breakpoint on this
function and when the program gets here GDB can know where the program has
been loaded. But what's the address of `dl_debug_state`? This is why
GDB needs the symbols of the dynamic linker: to determine the address
of `dl_debug_state`. [There are other ways to make this work. Maybe
in time GDB will use one of them.]

## TODO

The following is an incomplete and unprioritized list of tasks that remain to be
finished for basic debugging to work:

- Retransmission on "-" ack
- Memory access following vRun but before "c".
- sw/hw breakpoints and stepping
- vCont and friends
- Support architectures other than x86-64
- user muscle memory will make them want to type "debugserver :7000 ..."
  instead of "debugserver 7000 ...", should stay consistent
- downgrade some errors to INFO (e.g., for unsupported optional commands)
- some FXL_DCHECKs in application independent code assume the app is gdbserver,
  and may need to be replaced with non-fatal failures
- attach (waiting on suspend/resume)
- Ctrl-C from host gdb (waiting on suspend/resume)
- multi-inferior debugging
