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

[TOC]

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

## ZirconSystem.json

The dump-writer may choose to include system-wide information in dumps.  This
is information collected on the system at the time the dump is taken that may
be specific to the particular hardware or instance of the system but is not
specific to any single process or job on the system.  It can be included in a
job archive, or in an `ET_CORE` file, or both.  When system-wide information is
included in a job archive, then any child job or process dumps within the
archive might contain a copy of the same information, or they might omit it
since it is always the same across the whole job hierarchy.

The system-wide information is encoded in UTF-8 JSON text.  In a job archive,
it's found in a member file called `ZirconSystem.json`.  In an `ET_CORE` file,
it's found in the ELF note with name `ZirconSystem.json` and `n_type` of zero.

The JSON schema is subject to future extension, but it's a JSON object
(i.e. key/value dictionary) with a simple mapping to the `zx::system` kernel
interfaces, e.g. `"version_string"` maps to a JSON string value that
`zx_system_get_version_string()` returned, and `"num_cpus"` maps to a JSON
integer value that `zx_system_get_num_cpus()` returned.

## Reader API

The `zxdump` C++ library provides an API for reading dumps as well as one for
creating them.  As described above, dumps can contain all the kinds of
information the Zircon kernel reports about processes, threads, and jobs,
using the kernel API's own formats.  So the library interface for reading
information from core dumps and job archives has striking parallels with the
Zircon system call interface.  In fact, most of the interface is what a
read-only subset of the Zircon API might look like in a new style of C++
language binding.  However, this is an API available on all host platforms as
well as on Fuchsia.

### `zxdump::TaskHolder`

The [`<lib/zxdump/task.h>`](include/lib/zxdump/task.h) header describes this
API in detail.  The `zxdump::TaskHolder` object is the root container used to
represent dump data in memory.  As the name implies, it holds a set of related
"task" objects, that is `zxdump::Job`, `zxdump::Process`, and `zxdump::Thread`
objects.  The holder can be fed dump files, either `ET_CORE` files with single
process dumps or job archives that can contain multiple dumps.  This is done
with the `Insert` method to "insert" a dump into the holder by file descriptor.

### Job, Process, & Thread Objects

Each Zircon kernel object read from dumps is represented by a C++ object.  All
these objects are owned by the `zxdump::TaskHolder` object and are always used
by reference.  The API mirrors the Zircon system call API for the same kernel
object types.  `zxdump::Job`, `zxdump::Process`, and `zxdump::Thread` classes
are derived from a common base class `zxdump::Task`.

Each has an object type and a KOID (aka PID in the case of processes) exactly
reflecting the Zircon kernel objects in the snapshot of the running system
taken by the dump.  The `get_info` and `get_property` methods return all the
object-type-specific information captured in the snapshot, using the Zircon
system call API's own data structures.  `zxdump::Thread` objects also have
`read_state` methods.  The preferred form of each of these uses strong typing
via a template parameter selecting the topic, property, or state kind to avoid
the hassles and unsafety of the raw buffer and size in the C system call API.

As in the live system's API, the various "KOID list" topics from `get_info` can
be used with the `get_child` method to navigate the task hierarchy, from job to
child job, from job to process, and from process to thread.  A more convenient
and efficient `find` method is also provided to look up a descendent task by
KOID from any job or process above it in the hierarchy.  `zxdump::Job` and
`zxdump::Process` objects also have convenience methods that return
`std::map<zx_koid_t, zxdump::...>` for the children, processes, and threads
lists for doing full enumeration.

### Task Hierarchy & Reading Multiple Dumps

A single ELF core dump file describes only one process (with all its threads).
A job archive can describe any number of jobs and processes.  Any particular
dump file, whether a single-process dump or a job archive, might be only one
slice of the picture that needs to be reassembled for post-mortem analysis.

The `zxdump::TaskHolder` API supports reading multiple dump files into a
single, unified view of the data.  As each dump is inserted, new job and
process objects are collated by matching up task KOIDs with children and
process KOID lists.  The tasks thus "self-assemble" into a task hierarchy
replicating a partial view of the live system's task hierarchy.

#### Root Job & The Super-Root

Navigating the task hierarchy of a `zxdump::TaskHolder` works just like in the
Zircon system call API: start with the root job, and enumerate children.  In
the zxdump case, the `zxdump::TaskHolder::root_job()` method simply returns
the root job object.

It's possible that a single job archive, or a collection of job archives
together, actually represent the root job of a system instance and all its
descendent tasks.  More often, the reader API is only looking at a partial
view of some subset of the tree.  This might be a strict subtree with a single
parent job that can be considered the "local root job".  But it could also be
just a collection of jobs that don't all share ancestry that's visible in the
dump data.  It may well be only a collection of individual process dumps and
no job information at all to indicate any kind of hierarchy above the threads
within each process.  The reader API handles all these cases.

When job archives provide a coherent view that assembles into a single tree
with one root job, then the `zxdump::Job` object returned by `root_job()` is
just this job, with all its job-specific information as well as its children
and process lists.

In other cases, `root_job()` is actually a special placeholder `zxdump::Job`
object called the "super-root".  This object doesn't correspond to any real
Zircon kernel object from the dumped system.  It serves only to provide the
child job and process lists that a real root job would provide.  The
placeholder object has KOID of zero and no other job information to report.
All it does have is a children list and a processes list, which appear like the
normal job `get_info` topics for those KOID lists even though no other topics
are available.  Every "orphaned" job or process whose parent job wasn't
described in any dump file will appear to be a child job or process of the
super-root.  (When the only task without a known parent is a job, then that job
becomes the "real" root job instead and there is no "super-root".)

When displaying information from a dump, a nonzero KOID for the root job
identifies a real, rooted job tree that can be displayed whole.  The zero KOID
of the super-root indicates that instead it's really just a collection of
unrelated "top-level" jobs and/or processes.

### Memory-Mapped & Streaming Input

The reader code uses file descriptors to read dump files.  When possible, it
will use `mmap` to map an ELF or archive file into read-only memory and use
its contents without requiring copies in memory.  But the reader will also
generally work with pipes as input, and will read in a streaming fashion with
some caveats.

The reader first reads all the file headers and the "notes" and caches them in
memory.  This contains all `get_info`, `get_property`, and `read_state` data
items.  What remains in the dump file is the contents of process memory,
which is read from files only on demand as needed for `read_memory` calls.
This has some ramifications:

 * When the reader can't use memory-mapped files, it has to hold onto the file
   descriptor so it can seek and read for later `read_memory` calls.

 * When the input file descriptor is not seekable (such as streaming input from
   a pipe or socket), then `read_memory` calls only work when they match the
   order of the data in the input dump file's layout.

Recall from the ordering sections in the format description above that dump
file layout is quite flexible.  The reader can cope with any valid layout.  But
the streaming input support is optimized for the canonical layout with all the
headers and note data first, followed by memory data with file order correlated
to ascending address order.  If the reader has to seek past memory data to get
to all the note data, this may be inefficient; and no `read_memory` calls will
succeed later, even in ascending address order.

Many particular uses of dump-reading are not concerned with reading memory.  So
the `zxdump::TaskHolder::Insert` method takes an optional flag argument to say
that `read_memory` isn't expected to be used later.  In this case, the reader
will clean up and close the file descriptors immediately after inserting the
dump.  Any later attempts to use `read_memory` on a `zxdump::Process` whose
data came from that dump will fail with `ZX_ERR_NOT_SUPPORTED`.

When full access to process memory via `read_memory` is required from a dump
file coming from a streaming input source, it's probably best to just write
the whole dump stream into a file and then use the memory-mapped reading mode.
In other cases, it works very well to feed the reader a dump stream piped from
a network connection or decompression process, etc.

### Partial Dumps & Missing Data

The dump format in theory represents every type of information about each job,
process, and thread it describes.  However, the dump-writer has wide discretion
to omit some pieces of information for any reason.  Particular `get_info`,
`get_property`, or `read_state` items might be elided because the task died
while being dumped; because the kernel or hardware didn't support a particular
kind of information; to save space in the dump; to redact sensitive data; or
simply by the whim of the user.  The dump reader can also often successfully
read a dump that has been truncated, and will then present it just the same as
a dump where specific information was elided intentionally.  In all these
cases, particular task API calls will fail with `ZX_ERR_NOT_SUPPORTED` when the
specific data requested is missing, even where their Zircon system call
counterparts might never get that error.

The `zxdump::Process::read_memory` distinguishes more cases:

 * `ZX_ERR_NO_MEMORY` is the same error the kernel returns for a memory range
   that simply isn't all mapped to anything in the process.

 * `ZX_ERR_NOT_FOUND` indicates that the dump described the memory region as
   present in the process, but intentionally omitted these actual memory
   contents.  The core file has a `PT_LOAD` segment covering the region, and
   may include `ZX_INFO_PROCESS_MAPS` data that gives more details, but the
   contents were not included in the dump.

 * `ZX_ERR_OUT_OF_RANGE` indicates that the memory was included in the dump,
   but can't be read because the dump was truncated.  This is also the result
   when trying to read memory from a non-seekable dump stream where the needed
   portion of the file has already been passed.

 * `ZX_ERR_NOT_SUPPORTED` specifically means that the dump file containing this
   process was inserted by a `zxdump::TaskHolder::Insert` call with `false`
   passed for the optional `read_memory` flag argument.

## Live Task API

As described above, the `zxdump` C++ library's API for reading information out
of dumps looks very much like a subset of the Zircon system call API for
getting the same information on a live system from running Zircon jobs and
processes.  So it's natural that when using this API on Fuchsia, you can use
the same API to handle information either from a dump or from a live system.

When the [`<lib/zxdump/task.h>`](include/lib/zxdump/task.h) API is used on
Fuchsia systems, an additional signature for the `Insert` method is available
on `zxdump::TaskHolder` objects.  Rather than taking a file descriptor to a
core file or job archive to read the dump of a process or job (aka a task),
this takes a Zircon handle to a process or job using the C++
[lib/zx](/zircon/system/ulib/zx/) API's `zx::handle` family of types.  This
"inserts" that live task into the holder in the same way: it "self-assembles"
with the other tasks already in the holder to form a job tree, presenting a
"super-root" if unrelated tasks go into the same holder.  (It's not possible
to insert a `zx::thread` directly, only the `zx::process` containing it.)

The biggest difference between inserting a dump and inserting a live task is
that the live task's information is not immediately collected.  Instead, when
`get_info`, `get_property`, and `read_state` calls are made on a
`zxdump::Task` family object that actually represents a live task, the
information is collected on demand.  Each topic, property, and state kind is
fetched only once and then cached, but none is fetched until it's requested.
(The one exception is the "basic" information, so the type and KOID are always
on hand.)  This means that it's efficient to use this API purely as a nicer
API front-end for `get_info` et al, while also making it easy to write code
that makes use of the information in exactly the same way for either a live
case or a post mortem case.  But the API is designed for the post mortem
style, which is to say, examining the state just once rather than fetching
fresh information as it changes over time.

Once a live task has been inserted, all the same API conveniences are
available, including the `find` methods as well as direct `get_child` lookups.
Once a live job has been inserted, its child jobs and processes are implicitly
inserted on demand as they are found by KOID via `get_child` or `find` from
the job tree already inserted.  As with dumps, when disconnected processes or
job trees are inserted, there will be a super-root presented as the fake root
job in the `zxdump::TaskHolder` object.  But if the actual root job handle of
the running system is inserted as a live task, then `root_job().find(KOID)`
efficiently finds any job or process on the system by KOID.

It's even possible to comingle live tasks and dump data in a single
`zxdump::TaskHolder` object.  Just like with inserting multiple dumps,
whatever KOIDs are made visible in the holder by inserting a process or job
tree all self-assemble by KOID and become accessible under the root job tree.
So it can work to insert a post mortem dump of jobs and processes, that are
now dead but came from the currently running system and exist in the same KOID
space, alongside the current root job.  The result is a combined picture of
the whole system's job tree that includes current jobs and processes in their
active state intermingled with their deceased relatives each in their last
known state.  (Inserting a live task with the same KOID as a task already read
from a dump may have confusing results.  The information from the dump will be
used as the cached information, but any information not present in the dump
that's requested later might be filled in from the live task.)

Code that can work equally well with dumps or with live tasks can be built for
Fuchsia or for other host operating systems.  To reduce the need for
conditional compilation, the `zxdump::LiveTask` type is provided as an alias
for `zx::handle` on Fuchsia that is also available as a placeholder API on
other systems.  When not on Fuchsia, the only `zxdump::LiveTask` objects that
exist are default-constructed "invalid handle" objects.  All the same APIs are
available, but they'll always fail because the handle passed will always be
invalid.

For convenience, the `zxdump::GetRootJob()` function is provided to fetch the
live root job handle via the [fuchsia.kernel.RootJob][fuchsia.kernel.RootJob]
FIDL protocol.  This returns failure if the current process's component
sandbox doesn't have access to that privileged protocol.  (Even this is also
available on non-Fuchsia systems in a version that always returns failure, so
no conditional compilation is required.)  A tool or service can insert one or
more dump files, or it can insert the live root job; and then look up tasks by
KOID and interrogate them with identical code either way.

[fuchsia.kernel.RootJob]: https://fuchsia.dev/reference/fidl/fuchsia.kernel#RootJob
