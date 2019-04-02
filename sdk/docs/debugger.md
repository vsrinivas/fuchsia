# Debugger (zxdb)

Zxdb is a console debugger for native code compiled with DWARF symbols (C, C++
and Rust). The frontend runs on the host computer and connects to the on-device
`debug_agent`. This document describes how to set up these processes.

## Running the agent

The `debug_agent` is run on the target device along with the port number that
it should listen to for incoming client connections. Typically this command
will be run from a console after [ssh-ing](ssh.md) in to the system:

```
run fuchsia-pkg://fuchsia.com/debug_agent#meta/debug_agent.cmx --port=2345
```

## Connecting the client

The `zxdb` client program is run on the host computer. It can be connected to
the `debug_agent` via the interactive `connect` debugger command or it can
automatically connect based on a command-line flag. Both IPv4 and IPv6
addresses are supported (see [device discovery](device_discovery.md) to find
the address). The port should match the port number passed to the agent.

```
zxdb -c "[f370::5051:ff:1e53:589a%qemu]:2345"
```

### Connecting via a script

Scripts may want to automatically launch the agent and client automatically.
The script should wait for the port to be open on the target system before
launching the client. Automatic retry is not yet implemented in the client.

To clean up the debug agent gracefully when the client exits, pass the
`--quit-agent-on-exit` command-line flag to the client.

## Specifying symbol paths

The debugger expects unstripped ELF files to be available on the local host
system. Symbols on the target are not used. The location where the local build
stores symbols must be passed to the `zxdb` client.

Local symbols can be passed on the command line:

```
zxdb --symbol-path=/path-to-symbols
```

The path can be any of:

  * An individual symbolized ELF file.
  * An ids.txt file mapping build IDs to local files.
  * A directory name. If the directory is a GNU-style symbol repo (see below),
    symbols will be taken from the .build-id folder beneath it, otherwise the
    directory will be searched (non-recursively) for symbolized ELF files.

GNU-style symbol repos are directories of any layout which contain a folder at
the root called .build-id. This folder contains the symbolized binaries
indexed by the binaries' build IDs. Often these are symlinks pointing to
various locations in the folder itself.
```
Multiple `--symbol-path` parameters may be specified if there are symbols in
more than one location. All locations will be searched.

Symbol locations can also be edited interactively in the client using the
global "symbol-paths" setting (see the interactive "get" and "set" commands).
