# Using the debugger

## Overview

Currently the debugger is usable for certain tasks by members of the
development team.

The debugger is for C/C++ code running on Fuchsia compiled in-tree for either
CPU (ARM64 or x64). Rust almost works if you compile with symbols (which we
don’t). I don’t know how to test Go. Please contact brettw if you’re interested
in helping with these.

The debugger runs remotely only (you can't do self-hosted debug).

### Major limitations

  * **There is no “print” command.** It doesn’t know anything about function
    parameters, variables, or types. It is impossible to look at program state
    beyond registers (“regs” command) and memory dumps (“x” and “stack”
    commands). This means you’re either debugging in assembly or based on the
    flow of your program (“how did I get here and where is it going”).

  * **There is no “next” command.** This is annoying but can be worked around
    since “step” and “finish” both work, or you can do things like “u 43” to
    get to a certain line.

  * Be aware that our debug build is compiled with some optimizations which
    means stepping may not work the way you would want even if the debugger was
    perfect.

  * “step” steps into syscalls which end up as a few assembly instructions you
    have to step through.

## Running

### 1. Boot with networking

Boot the target system with networing support. For
[QEMU support](https://fuchsia.googlesource.com/docs/+/HEAD/getting_started.md)
you may get some prompts for extra steps required:

```sh
fx run -N -u scripts/start-dhcp-server.sh
```

### 2. Run the debug agent on the target

You will also want to note the target's IP address (run `ifconfig` _on the
target_ to see this).

On the target system pick a port and run the debug agent:

```sh
debug_agent --port=2345
```

### 3. Run the client and connect

On the host system (where you do the build), run the client. Use the IP
address of the target and the port you picked above in the `connect` command.

```sh
out/x64/host_x64/zxdb
[zxdb] connect 192.168.3.20:2345
```
(Substitute your build directory as-needed).

If you're launching many times, there is also a command-line switch `zxdb
--connect=192.168.3.53:2345`.

### 4. Run or attach to a program

From within the debugger:

```
[zxdb] run /path/to/some/program
```

or

```
[zxdb] ps
...process tree...

[zxdb] attach 3452
```

### 5. Do more stuff.

Type "help" for commands, there is an extensive built-in help system. Some
examples of stuff you can do:

```
[zxdb] b main
Breakpoint 1 on Global, Enabled, stop=All, @ main
Pending: No matches for location, it will be pending library loads.

[zxdb] r /foo/bar/myapp
Process 1 Running koid=7537 /foo/bar/myapp
Thread 1 stopped on breakpoint 1 at main() • ps.c:257
   255 }
   256
 ▶ 257 int main(int argc, char** argv) {
   258     bool with_threads = false;
   259     for (int i = 1; i < argc; ++i) {

[zxdb] s
   257 int main(int argc, char** argv) {
   258     bool with_threads = false;
 ▶ 259     for (int i = 1; i < argc; ++i) {
   260         const char* arg = argv[i];
   261         if (!strcmp(arg, "--help")) {

[zxdb] l
...source code...

[zxdb] f
▶  0  main() • ps.c:259
   1  start_main() • __libc_start_main.c:49
   2  0x0

[zxdb] c
Exited with code 0: Process 1 Not running /foo/bar/myapp

[zxdb] q
```

## Running the tests

There are tests for the debugger that run on the host. These are relavant
if you're working on the debugger client.

```sh
out/x64/host_x64/zxdb_tests
```

The debug agent tests are in
```
/pkgfs/packages/debug_agent_tests/0/test/debug_agent_tests
```
