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

**TODO:** threads

### Memory segments

The representation of memory in core dumps is standard across systems.  Zircon
core dumps do not deviate.

The `PT_LOAD` segments represent all of the address space of the process.  Each
uses a `p_align` value of page size, and both its `p_vaddr` and `p_offset` are
aligned to page size.  As ELF requires, `PT_LOAD` segments are in ascending
order of address (`p_vaddr`) and do not overlap.  Every gap not covered by the
`p_vaddr` and `p_memsz` of some `PT_LOAD` segment should be a hole in the
address space where nothing was mapped in the process.

Each `PT_LOAD` segment has a `p_filesz` that may be anywhere from zero up to
its `p_memsz`.  The `p_memsz` value says how much of the address space this
mapping took up in the process, and is always a multiple of page size.  The
`p_filesz` value is what leading subset of that memory is included in the dump.
It's usually a multiple of page size too, but is not required to be.  If it's
zero or less than the full `p_memsz` value, that means the dump-writer decided
to elide or truncate the contents of this memory.  That could be because it
would just read as zero (though the dump-writer could instead leave a
sparse-file "hole" for zero pages when the filesystem supports that); or
because it was memory that the writer's policy said should never be dumped,
such as device memory or shared memory; or simply memory that the writer
decided was too uninteresting or too big to include, such as read-only program
code and data attributable to mapped files.  In Zircon core dumps, the
information about mappings and VMOs in the process-wide notes (below) may shed
additional light on what some elided memory was or why it was not dumped.

### Note segments

`ET_CORE` files also have `PT_NOTE` segments providing additional information
about the process.  The details of the note formats vary widely by system,
though all use the ELF note container format.  A segment with `p_offset` and
nonzero `p_filesz` but a zero `p_vaddr` and zero `p_memsz` is recognized as a
"non-allocated" segment, which holds offline data but does not correspond to
the process address space.  This kind of segment is used in `ET_CORE` files.

In Zircon core dumps, there is a single non-allocated `PT_NOTE` segment that
appears before all the `PT_LOAD` segments (both in its order in the program
header table and in the order of its `p_offset` locating data in the file).
This contains several notes using different name (string) and type (integer)
values to represent process and thread state.  These map directly to state
reported by the Zircon kernel ABI.

#### Process-wide notes

The first series of notes describe process-wide state.

##### ZirconProcessInfo

ELF notes using the name `ZirconProcessInfo` contain all the types that
`zx_object_get_info` yields on a Zircon process.  The ELF note's 32-bit type is
exactly the `zx_object_info_topic_t` value in `zx_object_get_info`.  The note's
"description" (payload) has the size and layout that corresponds to that topic.
All available types are usually included in the dump.

##### ZirconProcessProperty

ELF notes using the name `ZirconProcessProperty` contain all the types that
`zx_object_get_property` yields on a Zircon process.  The ELF note's 32-bit
type is exactly the `property` argument to `zx_object_get_property`.  The
note's "description" (payload) has the size and layout that corresponds to that
property.  All available properties are usually included in the dump.

##### Note ordering

The first note is always for `ZX_INFO_HANDLE_BASIC`; this has the process KOID
(aka PID).  (Note that the `rights` field indicates the rights the dump-writer
had to dump the process; this does not represent any handle present in the
process.)  The second note is always for `ZX_PROP_NAME`.  The set of remaining
notes and their order is unspecified and subject to change.  Dumps generally
include all the information the kernel makes available, but a dump-writer might
be configured to omit some information or might be forced to omit some
information due to runtime errors from the system calls to collect data.
