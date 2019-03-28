# zxdb: Fuchsia native debugger setup and troubleshooting

This is the very detailed setup guide. Please see:

  * The [user guide](debugger_usage.md) for help on debugger commands.
  * The [zxdb codelab](http://go/zxdb-codelab) (Googlers only).

## Overview

The debugger is for C/C++ code running on Fuchsia compiled in-tree for either
CPU (ARM64 or x64). Rust kind of works but there [are
issues](https://fuchsia.atlassian.net/browse/DX-604). I don’t know how to test
Go. Please contact brettw if you’re interested in helping! Even if you don't
know how to write debugger code, just defining the proper behavior for Rust or
Go would be helpful (the team has no experience with these languages).

The debugger runs remotely only (you can't do self-hosted debug).

### Limitations

  * Be aware that our debug build is compiled with some optimizations which
    means stepping may not work the way you would want even if the debugger was
    perfect (see "Getting less optimization" below).

  * Variables in non-top stack frames aren't available as often as they could
    be.

  * “step” steps into syscalls which end up as a few assembly instructions you
    have to step through.

  * Obviously many advanced features are missing.

### Bugs

  * [Open zxdb bugs](https://fuchsia.atlassian.net/browse/DX-80?jql=project%20%3D%20DX%20AND%20component%20%3D%20zxdb%20order%20by%20lastViewed%20DESC)

  * [Report a new zxdb bug](https://fuchsia.atlassian.net/secure/CreateIssueDetails!init.jspa?pid=11718&issuetype=10006&priority=3&components=11886)

## Binary location (for SDK users)

The binary is `tools/zxdb` in the Fuchsia SDK. SDK users will have to do an
extra step to set up your symbols. See "Running out-of-tree" below for more.

## Compiling (for Fuchsia team members)

When you do a local Fuchsia build at the Garnet layer the debugger should
always be built by default. We try to keep it enabled at Peridot and Topaz
as well for developers, but changes to the build and your local build
configuration can affect this.

If you're working in a vendor layer or aren't getting the debugger when
building, you need to add `//bundles:tools` to the list of
packages to build. This example shows how to add this onto the default peridot
packages (replace with your build's default or whatever you're using):

```sh
fx set  core.x64 --with //bundles:tools
fx build
```

## Running

### Preparation: Boot with networking

Boot the target system with networking support. For QEMU you'll need to set up
a bridge interface so your target is visible (Googlers see
[go/zxdb-networking](http://goto.google.com/zxdb-networking)).

Then run:

```sh
fx run -N
```

### Simple method

You can use the fx utility to start the debug agent and connect automatically.

```sh
fx debug
```
### Manual method

In some cases you may want to run the debug agent and connect manually. To do
so, follow these steps:

#### 1. Run the debug agent on the target

On the target system pick a port and run the debug agent:

```sh
run debug_agent --port=2345
```

You will also want to note the target's IP address. Run `ifconfig` _on the
target_ to see this, or run `fx netaddr` on the host.

For QEMU, we recommend using IPv6 and link local addresses. These addresses
have to be annotated with the interface they apply to, so make sure the address
you use includes the appropriate interface (should be the name of the bridge
device).

The address should look like this:

```
fe80::5054:4d:fe63:5e7a%br0
```

#### 2. Run the client and connect

On the host system (where you do the build), run the client. Use the IP
address of the target and the port you picked above in the `connect` command.

```sh
out/x64/host_x64/zxdb
[zxdb] connect [fe80::5054:4d:fe63:5e7a%br0]:2345
```
(Substitute your build directory as-needed).

If you're connecting or running many times, there are command-line switches:

```sh
zxdb -c [fe80::5054:4d:fe63:5e7a%br0]:2345 -r /bin/cowsay
```

See `help connect` for more examples, including IPv6 syntax.

### Read the user guide

Once you're connected, the [user guide](debugger_usage.md) has detailed
instructions!

## Tips

### Getting less optimization

Fuchsia's "debug" build compiles with `-Og` which ends up being the same as
`-O1` (some optimizations). Some things will still be optimized out and
reordered that can make debugging more challenging.

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

### Diagnosing symbol problems.

The `sym-stat` command will tell you status for symbols. With no running
process, it will give stats on the different symbol locations you have
specified. If your symbols aren't found, make sure these stats match your
expectations:

```
[zxdb] sym-stat
Symbol index status

  Indexed  Source path
      950  /home/me/build/garnet/out/x64/ids.txt
        0  my_dir/my_file
```

If you see "0" in the "Indexed" column of the "Symbol index stats" that means
that the debugger could not find where your symbols are. Try the `-s` flag (see
"Running out-of-tree" above) to specify where your symbols are.

When you have a running program, sym-stat will additionally print symbol
information for each binary loaded into the process. If you're not getting
symbols, find the entry for the binary or shared library in this list. If it
says:

```
    Symbols loaded: No
```

then that means it couldn't find the symbolized binary on the local computer
for the given build ID in any of the locations listed in "Symbol index status".
You may need to add a new location with `-s`.

If instead it says something like this:

```
    Symbols loaded: Yes
    Symbol file: /home/foo/bar/...
    Source files indexed: 1
    Symbols indexed: 0
```

where "Source files indexed" and "Symbols indexed" is 0 or a very low integer,
that means that the debugger found a symbolized file but there are few or no
symbols in it. Normally this means the binary was not built with symbols
enabled or the symbols were stripped. Check your build, the compile line should
have a `-g` in it for gcc and Clang.

## Debugging the debugger and running the tests

For developers working on the debugger, you can debug the client on GDB or LLDB
on your host machine. You will want to run the unstripped binary:
`out/<yourbuild>/host_x64/exe.unstripped/zxdb`. Since this path is different
than the default, you will need to specify the location of ids.txt (in the root
build directory) with `-s` on the command line.

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
