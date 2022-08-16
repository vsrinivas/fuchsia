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

**TODO:** reading

## Core file format

The dump of a process is represented by an ELF file.  The ELF header's class,
byte-order (always 64-bit and little-endian for Fuchsia), and `e_machine`
fields represent the machine, and `e_type` is `ET_CORE`.

According to the standard format, `ET_CORE` files have program headers but no
section headers (not counting the `PN_XNUM` protocol for large numbers of
program headers, which uses a special section header).  Each `PT_LOAD` segment
represents a memory mapping.  One or more `PT_NOTE` segments give additional
information about the process and (optionally) its threads.

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

#### Per-thread notes

Additional sets of notes describe each thread in the process.  These notes are
not always included in the dump, at the discretion of the dump-writer.  The
process-wide data and memory can be collected while letting the threads
continue to run.  In that case, the data and/or threads may be mutually
inconsistent since threads changed things while the dump was being taken.
Moreover, there is no per-thread data whatsoever.  Ordinarily, the dump-writer
suspends the process and each thread before collecting any data.  Only when all
data and memory has been dumped does it allow those threads to run again.  In
this (usual) case, full information is dumped about each thread.

There is no formal grouping or separation between the notes for one thread and
the next.  All the notes for one thread appear, then all the notes for the next
thread.  This is in the order those threads were reported by the kernel,
usually chronological order of their creation.

The first note for each thread is always for `ZX_INFO_HANDLE_BASIC`; this has
the thread KOID and indicates that following notes apply to that thread.  (Note
that the `rights` field indicates the rights the dump-writer had to dump the
thread; this does not represent any handle present in the process.)  The second
note for each thread is always for `ZX_PROP_NAME`.  The set of remaining notes
and their order is unspecified and subject to change; see above.

The dump-writer normally tries to include every known per-thread note for each
thread.  Some types are not available because they aren't used on the current
machine or because the thread was already dying when the dump started, but some
might be elided just because their contents are boring.  If a known type is
omitted for a thread, it usually means there was no interesting data to report.

##### ZirconThreadInfo

ELF notes using the name `ZirconThreadInfo` contain all the types that
`zx_object_get_info` yields on a Zircon thread.  The ELF note's 32-bit type is
exactly the `zx_object_info_topic_t` value in `zx_object_get_info`.  The note's
"description" (payload) has the size and layout that corresponds to that topic.
As mentioned above, the `ZX_INFO_HANDLE_BASIC` note comes first and provides
the KOID that can be used as a unique identifier for the thread across the
whole dump.

##### ZirconThreadProperty

ELF notes using the name `ZirconThreadProperty` contain all the data that
`zx_object_get_property` yields on a Zircon thread.  The ELF note's 32-bit type
is exactly the `property` argument to `zx_object_get_proprety`.  The note's
"description" (payload) has the size and layout that corresponds to that
property.

##### ZirconThreadState

ELF notes using the name `ZirconThreadState` contain all the data that
`zx_thread_read_state` yields on a Zircon thread.  The ELF note's 32-bit type
is exactly the `zx_thread_state_topic_t` argument to `zx_thread_read_state`.
The note's "description" (payload) has the size and layout that corresponds to
that topic's `zx_thread_state_*_t` type.  The types and layouts that will
appear vary by machine.

## Job archives

As well as an individual process, a Zircon job can be dumped into a file
called a "job archive" that represents the job itself and may include dumps
for its processes and/or child jobs, at the discretion of the dump-writer.

A job archive is a standard `ar` format archive (like `.a` files for linking).
The archive's member files can be listed and extracted using the standard `ar`
tool.  It has the standard archive header and uses the standard long-name table
special member, but does not have a symbol table special member like the
archives used for static linking.

The initial portion of the job archive contains member files that describe the
job itself.  This subset alone is called the "stub archive".  After the stub
archive, there may be additional member files containing dumps for processes
or child jobs.

### Stub archive

The member files of the stub archive are analogous to the notes in an
`ET_CORE` file as described above.  Rather than using ELF note format, the
name of each file encodes its type.

#### ZirconJobInfo

Member files with name `ZirconJobInfo.%u` contain all the types that
`zx_object_get_info` yields on a Zircon job.  `%u` is the decimal
representation of the `zx_object_info_topic_t` value in `zx_object_get_info`.
The file has the size and layout that corresponds to that topic.  All
available types are usually included in the dump.

#### ZirconJobProperty

Member files with name `ZirconJobProperty.%u` contain all the types that
`zx_object_get_property` yields on a Zircon job.  `%u` is the decimal
representation of the `property` argument to `zx_object_get_property`.  The
file has the size and layout that corresponds to that property.  All available
properties are usually included in the dump.

### Process dump member files

A job archive can include the whole dumps for the processes within the job.  A
member file for a process dump is an `ET_CORE` file as described above.  The
name of a process dump member file doesn't matter, but it is usually `core.%u`
where `%u` is the decimal representation of the process KOID (aka PID).  The
definitive process KOID should be discovered from the notes inside the process
dump member file itself, not by parsing the member file name.

### Child job dump member files

A job archive can include the whole dumps for child jobs within the job.  A
member file for a child job dump is itself another job archive file.  The name
of a child job dump member file doesn't matter, but it is usually `core.%u.a`
where `%u` is the decimal representation of the job KOID.  The definitive job
KOID should be discovered from the information inside the job archive itself,
not by parsing the member file name.

### Member file ordering

Robust readers can ignore the order of member files in a job archive and
recognize the stub archive members by name and others by their contents.  This
works with job archives unpacked and repacked using `ar` or similar tools.
However, standard Zircon job archives are streamed out in a specific order.

All member files that form the stub archive appear before any process or child
job dump member files.  Usually all members in the stub archive use the long
name table and additional member files for process or child job dumps have
short names truncated to fit in the traditional member header.

The first member in the stub archive is for `ZX_INFO_HANDLE_BASIC`; this has
the job KOID.  (Note that the `rights` field indicates the rights the
dump-writer had to dump the job; this does not represent any handle present in
any dumped process.)  The second member is always for `ZX_PROP_NAME`.  The set
of remaining members and their order is unspecified and subject to change.
Dumps generally include all the information the kernel makes available.

The order of process and/or child dump member files doesn't matter, but
usually all the processes are dumped and then all the child jobs, in the order
the kernel reported them as seen in the stub archive.  The definitive type and
KOID of each process or child dump member file should be discovered from the
information inside the member file itself, not from member ordering or names.

### Flattened job archives

When a job archive includes child job dumps, this can be done in two ways.

In a hierarchical job archive, there are `ET_CORE` member files for each
process in the job and job archive member files for each child job.  Each
child job's member file is itself another hierarchical job archive that might
contain a grandchild job archive, etc.

In a flattened job archive, the archive member file for a child job is just
the stub archive that describes the job itself.  That child job's processes
appear as member files in the flattened job archive, not inside the contained
job archive for the child job.  Likewise, any grandchild jobs appear as member
files in the flattened job archive that are themselves just stub archives,
followed by the grandchild's processes and the great-grandchildren, etc.

A hierarchical job archive preserves the job hierarchy in the structure of the
files.  A flattened job archive loses that structure, requiring a reader to
reconstruct it from the process and child KOID lists in each stub archive.

The very simple way the traditional `ar` archive format works means that the
hierarchical and flattened job archives for the same job tree are exactly the
same size and have all the same contents in all the same places.  The only
difference is in the member header for a child job archive, which either says
the member extends to include the following additional dump members or that it
stops after just the stub archive so those members come after the child's stub
archive in the outer archive.

Because the whole child job archive's size must be determined in advance,
hierarchical job archives require holding all the processes in the child's
whole subtree suspended while dumping the whole child's job archive _en masse_.
A flattened job archive can always be streamed out piecemeal while only one
process at a time is held suspended long enough to dump it.  Thus streaming out
a flattened job archive will usually go more quickly than the equivalent
hierarchical job archive.  However, the hierarchical job archive also ensures
that the state shown in the dump is synchronized across all the processes in
the hierarchy since they were all kept suspended while doing all the dumping.
