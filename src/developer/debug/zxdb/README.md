# Zxdb

This is the code for the frontend of the Fuchsia debugger. This frontend runs
on the developer's host computer (Linux or Mac) and talks via IPC (code in
[../ipc](../ipc)) to the debug agent (code in [../debug_agent](../debug_agent))
running on the Fuchsia target.

### User documentation

Please see the debugger [setup](../../../../garnet/docs/debugger.md) and
[usage](../../../../garnet/docs/debugger_usage.md) documentation.

### Subdirectories

In order from the lowest conceptual level to the highest. Dependencies always
point "up" in this list.

  * `common`: Lower-level utilities used by multiple other layers. Can not
    depend on any other part of the debugger.

  * `symbols`: The symbol library. This wraps LLVM's DWARF parser and provides
    symbol indexing, an object model, and helper utilities for dealing with
    symbols.

  * `expr`: The expression evaluation library. This provides a parser for
    C++-like expressions and an execution environment for these expressions
    using the symbol library.

  * `client`: Conceptually this is a library for writing a debugger UI. It
    provides functions for the lower-level commands like "step" and "next" and
    an object model for dealing with processes, threads, etc. But this library
    provides no user-interface.

  * `console`: Frontend for the client that provides a console UI on Linux and
    Mac.
