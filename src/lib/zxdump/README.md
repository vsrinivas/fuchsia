# Zircon ELF Core Dump Support

This library provides support for the Zircon incarnation of traditional "core
dump" support using the ELF file format.  The ELF core file uses a very
straightforward format to dump a flexible amount of information, but usually a
very complete dump.  In contrast to other dump formats such as "minidump", core
files tend to be large and complete, rather than compact and sufficient.  The
format allows the dump-writer some leeway in choosing how much data to include.

The library provides a flexible callback-based C++ API for controlling the
various phases of collecting data for a dump.  The library produces dumps in a
streaming fashion, with disposition of the data left to callbacks.

A simple writer using POSIX I/O is provided to plug into the callback API to
stream to a file descriptor.  This works with either seekable or non-seekable
file descriptors, seeking forward over gaps of zero padding when possible.

**TODO:** reading, jobs

## Core file format

The dump of a process is represented by an ELF file.  The ELF header's class,
byte-order (always 64-bit and little-endian for Fuchsia), and `e_machine`
fields represent the machine, and `e_type` is `ET_CORE`.

According to the standard format, `ET_CORE` files have program headers but no
section headers (not counting the `PN_XNUM` protocol for large numbers of
program headers, which uses a special section header).  Each `PT_LOAD` segment
represents a memory mapping.  One or more `PT_NOTE` segments give additional
information about the process and (optionally) its threads.

**TODO:** details to come
