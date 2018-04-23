# Using the debugger

## Current status

Currently the debugger isn't usable for normal debugging tasks. Symbols are not
hooked up and breakpoints don't work on ARM.

These instructions are currently applicable to people actually working on the
debugger.

## Running on the target

The debugger runs on the target device and should be compiled in any build with
Garnet. Just type "zxdb" on the shell. Symbols will not work in the target
build.

## Running remotely

#### 1. Compile the client

Any build with Garnet will give the `debug_agent` which is the target stub you
will need. But you will need to compile the client side for your host system
manually:

```sh
ninja -C out/x64 host_x64/zxdb
```

Substitute `out/x64` for your build directory as necessary. The client has only
been tested on Linux.

#### 2. Boot with networking

Boot the target system with networing support. For
[QEMU support](https://fuchsia.googlesource.com/docs/+/HEAD/getting_started.md)
you may get some prompts for extra steps required:

```sh
fx run -N -u scripts/start-dhcp-server.sh
```

#### 3. Run the debug agent

On the target system pick a port and run the debug agent:

```sh
debug_agent --port=2345
```

You will also want to note the target's IP address (run `ifconfig` _on the
target_ to see this).

#### 4. Run the client and connect

Using the build directory from above, run the client:

```sh
out/x64/host_x64/zxdb
```

And then connect to the agent using the IP address for the target, and the
same port number you used when running the agent:

```
[zxdb] connect 192.168.3.20 2345
Connected successfully.
[zxdb]
```

## Super quick start

Run a program:

```
[zxdb] run /path/to/some/program
```

Attach to a program:

```
[zxdb] ps
...process tree...

[zxdb] attach 3452
```

Type "help" for more commands, there is an extensive built-in help system.

