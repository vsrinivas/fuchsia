# zxdb: Fuchsia native debugger setup and troubleshooting

## Overview

The debugger is for C, C++, and Rust code running on Fuchsia for either 64-bit
ARM or 64-bit x86 architectures.

This is the very detailed setup guide. Please see:

  * The [user guide](debugger_usage.md) for help on debugger commands.

The debugger runs remotely only (you can't do self-hosted debug).

### Bugs

  * [Open zxdb bugs](https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=5040)

  * [Report a new zxdb bug](https://bugs.fuchsia.dev/p/fuchsia/issues/entry?components=DeveloperExperience%3Ezxdb)

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

Boot the target system with networking support:

  * Hardware devices: use the device instructions.
  * AEMU: `fx emu -N`
  * QEMU: `fx qemu -N`

(If using x64 with an emulator on a Linux host, we also recommend the "-k" flag
which will make it run faster).

To manually validate network connectivity run `fx shell` or `fx netaddr`.

### Simple method

You can use the fx utility to start the debug agent and connect automatically.

For most build configurations, the debug agent will be in the "universe" (i.e.
"available to use") but not in the base build so won't be on the system before
boot. You will need to run:

```sh
fx serve
```

to make the debug agent's package avilable for serving to the system. Otherwise
you will get the message "Timed out trying to find the Debug Agent".

Once the server is running, launch the debugger in another terminal window:

```sh
fx debug
```

To manually validate packages can be loaded, run "ls" from within the Fuchsia
shell (for most setups this requires "fx serve" to be successfully serving
packages).

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
If running in-tree, `fx netaddr` will tell you this address.

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

  * The `status` command will give you a summary of the current state of the
    debugger.

  * See `help connect` for more examples, including IPv6 syntax.

### Read the user guide

Once you're connected, the [user guide](debugger_usage.md) has detailed
instructions!

## Tips

### Running out-of-tree

You can run with kernels or user programs compiled elsewhere with some extra
steps. We hope this will become easier over time.

Be aware that we aren't yet treating the protocol as frozen. Ideally the
debugger will be from the same build as the operating system itself (more
precisely, it needs to match the debug\_agent). But the protocol does not
change very often so there is some flexibility.

When you run out-of-tree, you will need to tell zxdb where your symbols and
source code are on the local development box (Linux or Mac). Zxdb can not use
symbols in the binary that you pushedf to the Fuchsia target device.

See [Diagnosing symbol problems](#diagnosing-symbol-problems).

#### Set the symbol location

To specify new symbol locations for zxdb, use the `-s` command-line flag:

```sh
zxdb -s path/to/my_binary -s some/other_location
```

Or add it to the `symbol_paths` list option in the interactive UI:

```
[zxdb] set symbol-paths += /my/new/symbol/path
```

It's best if your build makes a ".build-id" directory. You then pass the parent
directory as a symbol dir. For example, the Fuchsia build itself makes a
".build-id" directory inside the build directory. For example, if your build
directory is `out/x64`:

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

#### Set the source code location {#set-source-code-location}

The Fuchsia build generates symbols relative to the build directory so relative
paths look like `../../src/my_component/file.cc`).

If your files are not being found with the default build directories, you will
need to provide a build directory to locate the files. This build directory does
not need have been used to build, it just needs to produce correct absolute paths
when concatenated with the relative paths from the symbol file.

You can add additional build directories on the command line:

```sh
zxdb -b /home/me/fuchsia/out/x64
```

Or interactively from within the debugger:

```
[zxdb] set build-dirs += /home/me/fuchsia/out/x64
```

If debugger is finding the wrong file, you can replace the entire build
directory list by omitting the `+=`:

```
[zxdb] set build-dirs /home/me/fuchsia/out/x64
```

If your build produces DWARF symbols with absolute file paths the files must be
in that location on the local system. Absolute file paths in the symbols are not
affected by the build search path. Clang users should use the
`-fdebug-prefix-map` which will also help with build hermeticity.

### Diagnosing symbol problems

#### Can't find symbols

The `sym-stat` command will tell you status for symbols. With no running
process, it will give information on the different symbol locations you have
specified. If your symbols aren't found, make sure this matches your
expectations:

```
[zxdb] sym-stat
Symbol index status

  Indexed  Source path
 (folder)  /home/me/.build-id
 (folder)  /home/me/build/out/x64
        0  my_dir/my_file
```

If you see "0" in the "Indexed" column of the "Symbol index status" that means
that the debugger could not find where your symbols are. Try the `-s` flag (see
"Running out-of-tree" above) to specify where your symbols are.

Symbol sources using the ".build-id" hierarchy will list "(folder)" for the
indexed symbols since this type of source does not need to be indexed. To check
if your hierarchy includes a given build ID, go to ".build-id" inside it, then
to the folder with the first to characters of the build ID to see if there is a
matching file.

When you have a running program, `sym-stat` will additionally print symbol
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

#### Mismatched source lines

Sometimes the source file listings may not match the code. The most common
reason is that the build is out-of-date and no longer matches the source. The
debugger will check that the symbol file modification time is newer than the
source file, but it will only print the warning the first time the file is
displayed. Check for this warning if you suspect a problem.

Some people have multiple checkouts. If it's finding a file in the wrong one,
override the `build-dirs` option as described above in [Set the source code
location](#set-source-code-location).

To display the file name of the file it found from `list`, use the `-f` option:

```
[zxdb] list -f
/home/me/fuchsia/out/x64/../../src/foo/bar.cc
 ... <source code> ...
```

You can also set the `show-file-paths` option. This will increase file path
information:

  * It will show the full resolved path in source listings as in `list -f`.
  * It will show the full path instead of just the file name in other
    places such as backtraces.

```
[zxdb] set show-file-paths true
```

You may notice a mismatch when setting a breakpoint on a specific line where
the displayed breakpoint location doesn't match the line number you typed. In
most cases, this is because this symbols did not identifty any code on the
specified line so the debugger used the next line. It can happen even in
unoptimized builds, and is most common for variable declarations.

```
[zxdb] b file.cc:138
Breakpoint 1 (Software) @ file.cc:138
   138   int my_value = 0;          <- Breakpoint was requested here.
 ◉ 139   DoSomething(&my_value);    <- But ended up here.
   140   if (my_value > 0) {
```

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
[zxdb] attach debug_agent

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

Note: Only one debugger can be attached to the main job in order to auto-attach
to new processes. Since you're using it for the first debugger, you won't be
able to launch components with the second one, only attach to them.

To run the debug agent tests:

```
fx run-test debug_agent_tests
```

## Other Languages

C, C++, and Rust are supported. Go is not supported but may work to some degree
if you compile with DWARF symbols (please file bugs if you try). Dart and
JavaScript will not work because they're interpreted languages that do not
generate compiled code with DWARF symbols.
