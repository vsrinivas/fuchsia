# zxdb: Fuchsia native debugger setup and troubleshooting

## Overview

The debugger is for C/C++ code running on Fuchsia compiled in-tree for either
CPU (ARM64 or x64). The state of other languages (like Rust) can be seen
[here](#other-languages).

This is the very detailed setup guide. Please see:

  * The [user guide](debugger_usage.md) for help on debugger commands.

The debugger runs remotely only (you can't do self-hosted debug).

### Limitations

  * Variables in non-top stack frames aren't available as often as they could
    be.

  * Obviously many advanced features are missing.

### Bugs (Googlers only)

  * [Open zxdb bugs](https://fuchsia.atlassian.net/browse/DX-80?jql=project%20%3D%20DX%20AND%20component%20%3D%20zxdb%20order%20by%20lastViewed%20DESC)

  * [Report a new zxdb bug](https://fuchsia.atlassian.net/secure/CreateIssueDetails!init.jspa?pid=11718&issuetype=10006&priority=3&components=11886)

## Binary location (for SDK users)

The binary is `tools/zxdb` in the Fuchsia SDK. SDK users will have to do an
extra step to set up your symbols. See "Running out-of-tree" below for more.

## Compiling (for Fuchsia team members)

A Fuchsia "core" build includes (as of this writing) the necessary targets
for the debugger. So this build configuration is sufficient:

```sh
fx --dir=out/x64 set core.x64
```

If you're compiling with another product, you may not get it by default. If you
don't have the debugger in your build, add `//bundles:tools` to your
"universe", either with:

```
fx <normal_stuff_you_use> --with //bundles:tools
```

Or you can edit your GN args directly by editing `<build_dir>/args.gn` and
adding to the bottom:

```
universe_package_labels += [ "//bundles:tools" ]
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

For most build configurations, the debug agent will be in the "universe" (i.e.
"available to use") but not in the base build so won't be on the system before
boot. You will need to run:

```sh
fx serve
```

to make the debug agent's package avilable for serving to the system. Otherwise
you will get the message "Timed out trying to find the Debug Agent". Once the
server is running, launch the debugger in another terminal window:

```sh
fx debug
```
### Manual method

In some cases you may want to run the debug agent and connect manually. To do
so, follow these steps:

#### 1. Run the debug agent on the target

On the target system pick a port and run the debug agent:

```sh
run fuchsia-pkg://fuchsia.com/debug_agent#meta/debug_agent.cmx --port=2345
```

If you get an error "Cannot create child process: ... failed to resolve ..."
it means the debug agent can't be loaded. You may need to run `fx serve` or its
equivalent in your environment to make it available.

You will want to note the target's IP address. Run `ifconfig` _on the target_
to see this, or run `fx netaddr` on the host.

#### 2. Run the client and connect

On the host system (where you do the build), run the client. Use the IP
address of the target and the port you picked above in the `connect` command.

For QEMU, we recommend using IPv6 and link local addresses. These addresses
have to be annotated with the interface they apply to, so make sure the address
you use includes the appropriate interface (should be the name of the bridge
device).

The address should look like `fe80::5054:4d:fe63:5e7a%br0`

```sh
fx zxdb

or

out/<out_dir>/host_x64/zxdb

[zxdb] connect [fe80::5054:4d:fe63:5e7a%br0]:2345
```
(Substitute your build directory as-needed).

If you're connecting or running many times, there are command-line switches:

```sh
zxdb -c [fe80::5054:4d:fe63:5e7a%br0]:2345
```

See `help connect` for more examples, including IPv6 syntax.

### Read the user guide

Once you're connected, the [user guide](debugger_usage.md) has detailed
instructions!

## Tips

### Running out-of-tree

You can run with kernels or user programs compiled elsewhere with some extra
steps.

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

It's best if you build make a ".build-id" directory. You then pass the parent
directory as a symbol dir. For example, the Fuchsia build itself makes a
".build-id" directory inside the build directory. You would run (assuming your
build directory is "x64") with:

```sh
out/x64/host_x64/zxdb -s out/x64
```

Some builds produce a file called "ids.txt" that lists build IDs and local
paths to the corresponding binaries. This is the second-best option.

If you don't have that, you can just list the name of the file you're debugging
directly. You can pass multiple "-s" flags to list multiple symbol locations.

The `-s` flag accepts three possible things:

   * Directory names. If the given directory contains a ".build-id"
     subdirectory that will be used. Otherwise all ELF files in the given
     directory will be indexed.

   * File names ending in ".txt". Zxdb will treat this as a "ids.txt" file
     mapping build IDs to binaries.

   * Any other file name will be treated as an ELF file with symbols.

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

Symbol sources using the ".build-id" hierarchy will list "(folder)" for the
indexed symbols since this type of source does not need to be indexed. To check
if your hierarchy includes a given build ID, go to ".build-id" inside it, then
to the folder with the first to characters of the build ID to see if there is a
matching file.

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
enabled or the symbols were stripped. Check your build, you should be passing
the path to the unstripped binary and the original compile line should have a
`-g` in it to get symbols.

## Debugging the debugger and running the tests

### Client

For developers working on the debugger, you can activate the `--debug-mode` flag
that will activate many logging statements for the debugger:

```
zxdb --debug-mode
```

You can also debug the client on GDB or LLDB on your host machine. You will want
to run the unstripped binary: `out/<yourbuild>/host_x64/exe.unstripped/zxdb`.
Since this path is different than the default, you will need to specify the
location of ids.txt (in the root build directory) with `-s` on the command line.

There are tests for the debugger that run on the host. These are relevant
if you're working on the debugger client.

```sh
fx run-host-tests zxdb_tests
```
or directly with
```sh
out/x64/host_tests/zxdb_tests
```

### Debug Agent

Similar as with the client, the debug agent is programmed to log many debug
statements when run with the `--debug-mode` flag:

```
run fuchsia-pkg://fuchsia.com/debug_agent#meta/debug_agent.cmx --debug-mode
```

It is also possible to attach the debugger to the debugger. The preferred way to
do this is to make zxdb catch the debugger on launch using the filtering
feature. This is done frequently by the debugger team. See the
[user guide](debugger_usage.md) for more details:

```
// Run the debugger that will attach to the "to-be-debugged" debug agent.
fx debug

// * Within zxdb.
[zxdb] set filters debug_agent

// Launch another debug agent manually
// * Within the target (requires another port).
run fuchsia-pkg://fuchsia.com/debug_agent#meta/debug_agent.cmx --port=5000 --debug-mode

// * Within the first zxdb:
Attached Process 1 [Running] koid=12345 debug_agent.cmx
  The process is currently in an initializing state. You can set pending
  breakpoints (symbols haven't been loaded yet) and "continue".
[zxdb] continue

// Now there is a running debug agent that is attached by the first zxdb run.
// You can also attach to it using another client (notice the port):
fx zxdb --connect [<IPv6 to target>]:5000 --debug-mode

// Now you have two running instances of the debugger!
```
NOTE: Only one debugger can be attached to the main job in order to auto-attach
to new processes. Since you're using it for the first debugger, you won't be
able to launch components with the second one, only attach to them.

The debug agent tests are in
```
/pkgfs/packages/debug_agent_tests/0/test/debug_agent_tests
```

To run them:
```
fx run-tests debug_agent_tests
```

## Other Languages

Rust mostly works but there [are issues](https://fuchsia.atlassian.net/browse/DX-604).
Go currently is currently not supported.

Please contact brettw@ if you’re interested in helping! Even if you don't know
how to write debugger code, just defining the proper behavior for Rust or Go
would be helpful (the team has no experience with these languages).
