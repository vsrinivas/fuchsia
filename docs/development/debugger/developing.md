# Developing and debugging zxdb

## Simple method

It's possible to ask `ffx debug` to launch zxdb in another debugger and/or with extra debug logs.

```posix-terminal
ffx debug connect --debugger lldb -- --debug-mode
```

This command will bring the lldb shell and you can use "run" to start the zxdb.

The "lldb" in the command can be substituted by "gdb".  However, using gdb might bring several
issues including

  * Older versions of gdb may not support all DWARF 5 standard, and some information might be
    missing such as source file listing.
  * Ctrl-C will not bring you back from zxdb to gdb. A workaround is to use `pkill -INT zxdb`
    in another window to stop the zxdb.

## Manual steps

### Client

For developers working on the debugger, you can activate the `--debug-mode` flag that will activate
many logging statements for the debugger:

```
zxdb --debug-mode
```

You can also debug the client on GDB or LLDB on your host machine.

  * Use the unstripped binary in `host_x64/exe.unstripped` to get symbols.
  * The Fuchsia build generates symbols relative to your build directory (`out/x64` or similar), so
    you must run GDB/LLDB with that as the current directory.
  * Launching zxdb from the debugger with the right flags to connect can be tricky. To debug
    initialization, copy the command-line from "ps". Otherwise, it's easiest to attach after
    starting the debugger in the normal fashion.

```posix-terminal
cd out/x64    # Substitute your build directory as needed.
sudo gdb host_x64/exe.unstripped/zxdb
... GDB startup messages ...
(gdb) attach 12345    # Use the PID of the zxdb already running.
... the program will be stopped when GDB attaches ...
(gdb) continue
```

There are tests for the debugger that run on the host. These are relevant if you're working on the
debugger client.

```posix-terminal
cd out/x64    # Substitute your build directory as needed.
host_x64/zxdb_tests
```

To run the unit tests in the debugger:

```posix-terminal
cd out/x64
cp host_x64/exe.unstripped/zxdb_tests host_x64/
gdb host_x64/zxdb_tests
```

Most tests can be debugged by omitting the copy step and debugging the
symbolized file in `exe_unstripped` directly, but some tests require data files
at a certain place relative to the test binary and these will fail.

### Debug Agent

<!-- TODO(dangyi): update the document after the debug_agent is migrated to v2. -->

Similar as with the client, the debug agent is programmed to log many debug statements when run with
the `--debug-mode` flag:

```posix-terminal
run fuchsia-pkg://fuchsia.com/debug_agent#meta/debug_agent.cmx --debug-mode --port=2345
```

The log statements are written to the system log. To view these on the host:

```posix-terminal
ffx log --severity info --filter debug_agent
```

If connecting to the agent via `ffx debug`, add `--debug-mode` to the `args` array in `//src/developer/debug/debug_agent/meta/debug_agent_channel.cmx`.

It is also possible to attach the debugger to the debugger. The preferred way to do this is to make
zxdb catch the debugger on launch using the filtering feature. This is done frequently by the
debugger team.

```none {:.devsite-disable-click-to-copy}
// Run the debugger that will attach to the "to-be-debugged" debug agent.
$ ffx debug connect

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
// * On the host:
out/<out_dir>/host_x64/zxdb --connect [<IPv6 to target>]:5000 --debug-mode

// Now you have two running instances of the debugger!
```

Note: Only one debugger can be attached to the main job in order to auto-attach to new processes.
Since you're using it for the first debugger, you won't be able to launch components with the second
one, only attach to them.

To run the debug agent tests:

```posix-terminal
fx test debug_agent_unit_tests
fx test debug_agent_integration_tests
```
