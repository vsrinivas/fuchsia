# Using the debugger

## Overview

Currently the debugger is usable for certain tasks by members of the
development team.

The debugger is for C/C++ code running on Fuchsia compiled in-tree for either
CPU (ARM64 or x64). Rust almost works if you compile with symbols (which we
don’t). I don’t know how to test Go. Please contact brettw if you’re interested
in helping with these.

The debugger runs remotely only (you can't do self-hosted debug).

### Limitations

  * Be aware that our debug build is compiled with some optimizations which
    means stepping may not work the way you would want even if the debugger was
    perfect (see "Getting less optimization" below).

  * Variables in non-top stack frames aren't available as often as they could
    be.

  * “step” steps into syscalls which end up as a few assembly instructions you
    have to step through.

### Bugs

  * [Open zxdb bugs](https://fuchsia.atlassian.net/browse/DX-80?jql=project%20%3D%20DX%20AND%20component%20%3D%20zxdb%20order%20by%20lastViewed%20DESC)

  * [Report a new zxdb bug](https://fuchsia.atlassian.net/secure/CreateIssueDetails!init.jspa?pid=11718&issuetype=10006&priority=3&components=11886)

## Compiling

When you do a local Fuchsia build at the Garnet layer the debugger should
always be built by default. We try to keep it enabled at Peridot and Topaz
as well for developers, but changes to the build and your local build
configuration can affect this.

If you're working in a vendor layer or aren't getting the debugger when
building, you need to add `garnet/packages/products/devtools` to the list of
packages to build. This example shows how to add this onto the default peridot
packages (replace with your build's default or whatever you're using):

```sh
fx set x64 --packages="peridot/packages/default,garnet/packages/products/devtools"
fx build
```

## Running

### 1. Boot with networking

Boot the target system with networking support. For
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

If you're connecting or running many times, there are command-line switches:

```sh
zxdb -c 192.168.3.53:2345 -r /system/bin/coway
```

See `help connect` for more examples, including IPv6 syntax.

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

### 5. Run and step

Type "help" for commands, there is an extensive built-in help system. The key
commands are:

   * **run:** Runs a program.
   * **continue:** Continues a paused thread.
   * **next:** Advance to next line.
   * **step:** Advance, stepping into function calls.
   * **finish:** Runs the current function to the end.

Some examples:

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

### 6. Print stuff.

The main querying commands are:

   * **print:** Print an expressions.
   * **f:** Print a short backtrace of stack frames.
   * **bt:** Print a backtrace with function parameters.
   * **locals:** Print all local variables.

Some examples:

```
[zxdb] bt
▶  0 main() • ps.c:282
      IP=0x1a77a1f9b991, BP=0x6db4c69eff0, SP=0x6db4c69efd0
      argc = 1
      argv = (char**) 0x49d00d252eac
   1 start_main() • __libc_start_main.c:49
      IP=0x598098780d2f, BP=0x6db4c69eff0, SP=0x6db4c69efe0
      p = <no type>

[zxdb] locals
arg = <invalid pointer>
i = 5
options = {also_show_threads = false, only_show_jobs = false}
ret = 0
status = 2

[zxdb] print ret
0

[zxdb] print options.also_show_threads
false
```

## Tips

### Getting less optimization

Fuchsia's "debug" build compiles with `-Og` which does some optimizations but
tries to be debugger-friendly about it. Some things will still be optimized
out and reordered that can make debugging more challenging.

If you're encountering optimization problems you can do a local build change to
override the debug flag for your target only. In the target's definition (in
the `BUILD.gn` file) add this code:

```python
if (is_debug) {
  # Force no optimization in debug builds.
  configs -= [ "//build/config:debug" ]
  cflags = [ "-O0" ]
}
```

It will apply only to .cc files in that target. We recommend not checking this
code in. If you find yourself needing this a lot, please speak up. We can
consider adding another globally build optimization level.

### Running out-of-tree

The debugger is optimized to run in-tree (you compiled the debugger from the
same tree as you compiled your system from, and are running them both
in-place). But you can run with kernels or user programs compiled elsewhere
with some extra steps.

Be aware that we aren't yet treating the protocol as frozen. Ideally the
debugger will be from the same build as the operating system itself (more
precisely, it needs to match the debug\_agent). But the protocol does not
change very often so there is some flexibility.

When you run out-of-tree, you will need to tell zxdb where your symbols are
on the local development box (Linux or Mac). Having symbols in the binary
you pushed to the target device doesn't help. Use the `-s` command-line flag
to tell zxdb about new symbol locations:

```sh
zxdb -s path/to/my_binary -s some/other_location
```

The `-s` flag accepts three possible things:

   * Directory names. Zxdb will index all build IDs of elf files in this
     directory.

   * File names ending in ".txt". Zxdb will treat this as a "ids.txt" file
     mapping build IDs to binaries (see below).

   * Any other file name will be treated as an ELF file with symbols.

The Fuchsia build outputs a file called "ids.txt" that lists build IDs and
binary names produced by the build process. By default zxdb will look relative
to its own binary name "../ids.txt" which matches the in-tree location. You
can specify different or additional ids.txt files using `-s`.

#### Diagnosing symbol problems.

The `sym-stat` command will tell you status for symbols. With no running
process, it will give stats on the different symbol locations you have
specified. If your symbols aren't getting found, make sure these stats match
your expectations:

```sh
[zxdb] sym-stat

Symbol index status

  Indexed  Source path
      950  /home/me/build/garnet/out/x64/ids.txt
        0  my_dir/my_file
```

When you have a running program, sym-stat will additionally print symbol
information for each binary loaded into the process.

## Running the tests

There are tests for the debugger that run on the host. These are relevant
if you're working on the debugger client.

```sh
fx run-host-tests zxdb_tests
```
or directly with
```sh
out/x64/host_tests/zxdb_tests
```

The debug agent tests are in
```
/pkgfs/packages/debug_agent_tests/0/test/debug_agent_tests
```
