# fidlcat: Monitor and debug your fidl calls

## Overview

fidlcat is a tool that allows users to monitor FIDL connections.  Currently, it
can attach to or launch a process on a Fuchsia device, and will report its FIDL
traffic.

## Running it

To run fidlcat in-tree, you first build it, which you can do the following way:

```sh
fx set <whatever> --with //bundles:tools
fx build
```

If you want to add it to your existing gn args, you can do so by adding this
stanza to the bottom of your <build_dir>/args.gn.

```
universe_package_labels += [ "//bundles:tools" ]
```

To run fidlcat, you must boot with networking enabled.

For QEMU networking support, you need to setup your system with a TUN/TAP
interface.  Then, run:

```sh
fx run -N
```

In a separate console, you need to ensure your target is able to fetch updates:

```sh
fx serve
```

You should then be able to use fidlcat to monitor FIDL messages from processes
on the target.  If you run the `ps` command in the shell, you can get a pid you
want to monitor, and run:

```sh
fx fidlcat --remote-pid <pid>
```

Alternatively, you can launch a component directly using its URL:

```sh
fx fidlcat run fuchsia-pkg://fuchsia.com/echo_client_rust#meta/echo_client_rust.cmx
```

Note that fidlcat needs two sources of information to work:

 * First, it needs the symbols for the executable.  In practice, if you are
   running in-tree, the symbols should be provided to fidlcat automatically.
   Otherwise, you can provide fidlcat a symbol path, which can be a text file
   that maps build ids to debug symbols, an explicit ELF file path, or a
   directory it will scan for ELF files and index.  This can be provided to
   fidlcat with the `-s` flag.

 * Second, it needs the intermediate representation for the FIDL it ingests, so
   it can produce readable output.  If you are running in-tree, the IR should be
   provided to fidlcat automatically.  Otherwise, you can provide fidlcat an IR
   path, which can be an explicit IR file path, a directory it will scan for IR
   files, or an argument file containing explicit paths.  This can be provided
   to fidlcat with the `--fidl-ir-path` flag.  The argument files need to be
   prepended with a `@` character: `--fidl-ir-path @argfile`.

Finally, if you are running fidlcat without the fx tool, the debug agent needs
to be running on the target.  Connect to the target and run:

```sh
run fuchsia-pkg://fuchsia.com/debug_agent#meta/debug_agent.cmx --port=8080
```

And, when you run fidlcat on the host, make sure you connect to that agent:

```sh
tools/fidlcat --connect [$(fx netaddr --fuchsia)]:8080 <other args>
```
