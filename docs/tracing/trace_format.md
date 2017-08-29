# Fuchsia Trace Format

This document describes the binary format used to collect, store, and
transmit Fuchsia trace records.

See [Fuchsia Tracing](tracing.md) for an overview.

## Purpose

While a trace is running, _trace providers_ write records into a trace buffer
VMO shared with the trace manager using the binary format described in this
document.

The binary format is designed to introduce minimal impact upon the
performance of the subject under trace while writing traces.  The records
are also written sequentially so that if a trace terminates (normally or
abnormally), the trace manager can still recover partial trace data already
stored in the trace buffer by reading everything up to the last well-formed
record.

As the trace progresses, the _trace manager_ aggregates records from all
trace providers which are participating in trace collection and concatenates
them together with some special metadata records to form a trace archive.

Once the trace completes, tools such as the `trace` command-line program
can read the trace records within the trace archive to visualize the results
or save them to a file for later consumption.

## Features

- Small footprint
  - Trace records are compact, packing information into a small number of bits.
  - Pooling strings, processes, and threads further compacts the trace data.
- Memory aligned
  - Trace records maintain an 8 byte alignment in memory to facilitate
    writing them directly into memory mapped VMOs.
- Variable size records
  - Overall record size is limited to 32 KB.
  - Large objects may need to be broken up into multiple records.
- Extensible
  - There’s room to define new record types as needed.
  - Unrecognized or malformed trace records can be skipped.

## Encoding Primitives

### Records

A trace record is a binary encoded piece of trace information consisting of
a sequence of [atoms](#atoms).

All records include a header word which contains the following basic
information:

- **Record Type**: A 4-bit field which identifies the type of the record
  and the information it contains.  See [Record Types](#record-types).
- **Record Size**: A 12-bit field which indicates the number of words
  (multiples of 8 byte units) within the record _including the record
  header itself_.  The maximum possible size of a record is 4095 words
  (32760 bytes).  Very simple records may be just 1 word (8 bytes) long.

Records are always a multiple of 8 bytes in length and are stored with
8 byte alignment.

### Atoms

Each record is constructed as a sequence of atoms.

Each atom is written with 8 byte alignment and has a size which is also a
multiple of 8 bytes so as to preserve alignment.

There are two kinds of atoms:

- **Word**: A 64-bit value which may be further subdivided into bit fields.
  Words are stored in machine word order (little-endian on all currently
  supported architectures).
- **Stream**: A sequence of bytes padded with zeros to the next 8 byte
  boundary.  Streams are stored in byte order.  Streams which are an exact
  multiple of 8 bytes long are not padded (there is no zero terminator).

**Fields** are subdivisions of 64-bit **Words**, denoted
`[<least significant bit> .. <most significant bit>]` where the first and
last bit positions are inclusive.  All unused bits are reserved for future
use and must be set to 0.

**Words** and **Fields** store unsigned integers unless otherwise specified
by the record format.

**Streams** may store either UTF-8 strings or binary data, as specified by
the record format.

### Archives

A trace archive is a sequence of trace records, concatenated end to end,
which stores information collected by trace providers while a trace is
running together with metadata records which identify and delimit sections
of the trace produced by each trace provider.

Trace archives are intended to be read sequentially since records which
appear earlier in the trace may influence the interpretation of records
which appear later in the trace.  The trace system provides tools for
extracting information from trace archives and converting it into other
forms for visualization.

### Timestamps

Timestamps are represented as 64-bit ticks derived from a hardware counter.
The trace initialization record describes the number of ticks per second
of real time.

By default, we assume that 1 tick equals 1 nanosecond.

### String References

Strings are encoded as **String Refs** which are 16-bit values of the
following form:

- **Empty strings**: Value is zero.
- **Indexed strings**: Most significant bit is zero.  The lower 15 bits
  denote an index in the **string table** which was previously assigned using a
  **String Record**.
- **Inline strings**: Most significant bit is one.  The lower 15 bits
  denote the length of the string in bytes.  The string's content appears
  inline in another part of the record as specified by the record format.

To make traces more compact, frequently referenced strings, such as event
category and name constants, should be registered into the **string table**
using **String Records** then referenced by index.

There can be at most 32767 strings in the string table.  If this limit is
reached, additional strings can be encoded by replacing existing entries
or by encoding strings inline.

String content itself is stored as a UTF-8 **Stream** without termination.

The theoretical maximum length of a string is 32767 bytes but in practice this
will be further reduced by the space required to store the rest of the record
which contains it, so we set a conservative maximum string length limit of
32000 bytes.

### Thread References

Thread and process kernel object ids (koids) are encoded as **Thread Refs**
which are 8-bit values of the following form:

- **Inline threads**: Value is zero.  The thread and process koid appears
  inline in another part of the record as specified by the record format.
- **Indexed threads**: Value is non-zero.  The value denotes an index in
  the **thread table** which was previously assigned using a **Thread Record**.

To make traces more compact, frequently referenced threads should be registered
into the **thread table** using **Thread Records** then referenced by index.

There can be at most 255 threads in the string table.  If this limit is
reached, additional threads can be encoded by replacing existing entries
or by encoding threads inline.

### Userspace Object Information

Traces can include annotations about userspace objects (anything that can be
referenced using a pointer-like value such as a C++ or Dart object) in the
form of **Userspace Object Records**.  Trace providers typically generate
such records when the object is created.

Thereafter, any **Pointer Arguments** which refer to the same pointer will
be associated with the referent's annotations.

This makes it easy to associate human-readable labels and other information
with objects which appear later in the trace.

### Kernel Object Information

Traces can include annotations about kernel objects (anything that can be
referenced using a Magenta koid such as a process, channel, or event)
form of **Kernel Object Records**.  Trace providers typically generate such
records when the object is created.

Thereafter, any **Kernel Object Id Arguments** which refer to the same koid will
be associated with the referent's annotations.

This makes it easy to associate human-readable labels and other information
with objects which appear later in the trace.

In particular, this is how the tracing system associates names with process
and thread koids.

### Arguments

Arguments are typed key value pairs.

Many record types allow up to 15 arguments to be appended to the record to
provide additional information from the developer.

Arguments are size-prefixed like ordinary records so that unrecognized
argument types can be skipped.

See also [Argument Types](#argument-types).

## Extending the Format

The trace format can be extended in the following ways:

- Defining new record types.
- Storing new information in reserved fields of existing record types.
- Appending new information to existing record types (the presence of this
  information can be detected by examining the record's size and payload).
- Defining new argument types.

_To preserve compatibility as the trace format evolves, all extensions must be
documented authoritatively in this file.  Currently there is no support for
private extensions._

## Notation

In the record format descriptions which follow, each constituent atom
is labeled in italics followed by a bullet-point description of its contents.

## Record Types

### Record Header

All records include this header which specifies the record's type and size
together with 48 bits of data whose usage varies by record type.

##### Format

_header word_
- `[0 .. 3]`: record type
- `[4 .. 15]`: record size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 63]`: varies by record type (must be zero if unused)

### Metadata Record (record type = 0)

Provides metadata about trace data which follows.

This record type is reserved for use by the _trace manager_ when generating
trace archives.  It must not be emitted by trace providers themselves.
If the trace manager encounters a **Metadata Record** within a trace produced
by a trace provider, it treats it as garbage and skips over it.

There are several metadata record subtypes, each of which contain different
information.

##### Format

_header word_
- `[0 .. 3]`: record type (0)
- `[4 .. 15]`: record size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 19]`: metadata type
- `[20 .. 63]`: varies by metadata type (must be zero if unused)

#### Provider Info Metadata (metadata type = 1)

This metadata identifies a trace provider which has contributed information to
the trace.

All data which follows until the next **Provider Section Metadata** or
**Provider Info Metadata** is encountered must have been collected from the
same provider.

##### Format

_header word_
- `[0 .. 3]`: record type (0)
- `[4 .. 15]`: record size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 19]`: metadata type
- `[20 .. 51]`: provider id (token used to identify the provider in the trace)
- `[52 .. 59]`: name length in bytes
- `[60 .. 63]`: reserved (must be zero)

_provider name stream_
- UTF-8 string, padded with zeros to 8 byte alignment

#### Provider Section Metadata (metadata type = 2)

This metadata delimits sections of the trace which have been obtained from
different providers.

All data which follows until the next **Provider Section Metadata** or
**Provider Info Metadata** is encountered is assumed to have been collected
from the same provider.

When reading a trace consisting of an accumulation of traces from different
trace providers, the reader must maintain state separately for each provider’s
traces (such as the initialization data, string table, thread table,
userspace object table, and kernel object table) and switch contexts
whenever it encounters a new **Provider Section Metadata** record.

##### Format

_header word_
- `[0 .. 3]`: record type (0)
- `[4 .. 15]`: record size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 19]`: metadata type
- `[20 .. 51]`: provider id (token used to identify the provider in the trace)
- `[52 .. 63]`: reserved (must be zero)

### Initialization Record (record type = 1)

Provides parameters needed to interpret the records which follow.  In absence
of this record, the reader may assume that 1 tick is 1 nanosecond.

##### Format

_header word_
- `[0 .. 3]`: record type (1)
- `[4 .. 15]`: record size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 63]`: reserved (must be zero)

_tick multiplier word_
- `[0 .. 63]`: number of ticks per second

### String Record (record type = 2)

Registers a string in the string table, assigning it a string index in the
range `0x0001` to `0x7fff`.  The registration replaces any prior registration
for the given string index when interpreting the records which follow.

String records which attempt to set a value for string index `0x0000` must be
ignored since this value is reserved to represent the empty string.

String records which contain empty strings must be tolerated but they’re
pointless since the empty string can simply be encoded as zero in a string ref.

##### Format

_header word_
- `[0 .. 3]`: record type (2)
- `[4 .. 15]`: record size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 30]`: string index (range 0x0001 to 0x7fff)
- `[31]`: always zero (0)
- `[32 .. 46]`: string length in bytes (range 0x0000 to 0x7fff)
- `[47]`: always zero (0)
- `[48 .. 63]`: reserved (must be zero)

_string value stream_
- UTF-8 string, padded with zeros to 8 byte alignment

### Thread Record (record type = 3)

Registers a process id and thread id pair in the thread table, assigning it a
thread index in the range `0x01` to `0xff`.  The registration replaces any
prior registration for the given thread index when interpreting the records
which follow.

Thread index `0x00` is reserved to denote the use of an inline thread id in
a thread ref.  Thread records which attempt to set a value for this value
must be ignored.

##### Format

_header word_
- `[0 .. 3]`: record type (3)
- `[4 .. 15]`: record size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 23]`: thread index (never 0x00)
- `[24 .. 63]`: reserved (must be zero)

_process id word_
- `[0 .. 63]`: process koid (kernel object id)

_thread id word_
- `[0 .. 63]`: thread koid (kernel object id)

### Event Record (record type = 4)

Describes a timestamped event.

This record consists of some basic information about the event including
when and where it happened followed by event arguments and event subtype
specific data.

##### Format

_header word_
- `[0 .. 3]`: record type (4)
- `[4 .. 15]`: record size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 19]`: event type
- `[20 .. 23]`: number of arguments
- `[24 .. 31]`: thread (thread ref)
- `[32 .. 47]`: category (string ref)
- `[48 .. 63]`: name (string ref)

_timestamp word_
- `[0 .. 63]`: number of ticks

_process id word_ (omitted unless thread ref denotes inline thread)
- `[0 .. 63]`: process koid (kernel object id)

_thread id word_ (omitted unless thread ref denotes inline thread)
- `[0 .. 63]`: thread koid (kernel object id)

_category stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

_name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

_argument data_ (repeats for each argument)
- (see below)

_event-type specific data_
- (see below)

#### Instant Event (event type = 0)

Marks a moment in time on this thread.  These are equivalent to Magenta
kernel probes.

##### Format

No event-type specific data required.

#### Counter Event (event type = 1)

Records sample values of each argument as data in a time series associated
with the counter’s name and id.  The values may be presented graphically as a
stacked area chart.

##### Format

_counter word_
- `[0 .. 63]`: counter id

#### Duration Begin Event (event type = 2)

Marks the beginning of an operation on a particular thread.  Must be matched
by a **Duration End Event**.  May be nested.

##### Format

No event-type specific data required.

#### Duration End Event (event type = 3)

Marks the end of an operation on a particular thread.

##### Format

No event-type specific data required.

#### Async Begin Event (event type = 4)

Marks the beginning of an operation which may span threads.  Must be matched
by an **Async End Event** using the same async correlation id.

##### Format

_async correlation word_
- `[0 .. 63]`: async correlation id

#### Async Instant Event (event type = 5)

Marks a moment within an operation which may span threads.  Must appear
between **Async Begin Event** and **Async End Event** using the same async
correlation id.

##### Format

_async correlation word_
- `[0 .. 63]`: async correlation id

#### Async End Event (event type = 6)

Marks the end of an operation which may span threads.

##### Format

_async correlation word_
- `[0 .. 63]`: async correlation id

#### Flow Begin Event (event type = 7)

Marks the beginning of an operation which results in a sequence of actions
which may span multiple threads or abstraction layers.  Must be matched by a
**Flow End Event** using the same flow correlation id.  This can be envisioned
as an arrow between duration events.

The beginning of the flow is associated with the enclosing duration event
for this thread; it begins where the enclosing **Duration Event** ends.

##### Format

_flow correlation word_
- `[0 .. 63]`: flow correlation id

#### Flow Step Event (event type = 8)

Marks a point within a flow.

The step is associated with the enclosing duration event for this thread;
the flow resumes where the enclosing duration event begins then is suspended
at the point where the enclosing **Duration Event** event ends.

##### Format

_flow correlation word_
- `[0 .. 63]`: flow correlation id

#### Flow End Event (event type = 9)

Marks the end of a flow.

The end of the flow is associated with the enclosing duration event for this
thread; the flow resumes where the enclosing **Duration Event** begins.

##### Format

_flow correlation word_
- `[0 .. 63]`: flow correlation id

### Blob Record (record type = 5)

Provides uninterpreted bulk data to be included in the trace.  This can be
useful for embedding captured trace data in other formats.

The blob name uniquely identifies separate blob data streams within the trace.
By writing multiple blob records with the same name, additional chunks of
data can be appended to a previously created blob.

The blob type indicates the representation of the blob's content.

##### Format

_header word_
- `[0 .. 3]`: record type (5)
- `[4 .. 15]`: record size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 31]`: blob name (string ref)
- `[32 .. 47]`: blob payload size in bytes (excluding padding)
- `[48 .. 55]`: blob type
- `[56 .. 63]`: reserved (must be zero)

_blob name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

_payload stream_ (variable size)
- binary data, padded with zeros to 8 byte alignment

##### Blob Types

The following blob types are defined:
- `0x01`: Catapult trace event data represented in JSON format

### Userspace Object Record (record type = 6)

Describes a userspace object, assigns it a label, and optionally associates
key/value data with it as arguments.  Information about the object is added
to a per-process userspace object table.

When a trace consumer encounters an event with a **Pointer Argument** whose
value matches an entry the process’s object table, it can cross-reference
the argument’s pointer value with a prior **Userspace Object Record** to find a
description of the referent.

##### Format

_header word_
- `[0 .. 3]`: record type (6)
- `[4 .. 15]`: record size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 23]`: process (thread ref)
- `[24 .. 39]`: name (string ref)
- `[40 .. 43]`: number of arguments
- `[44 .. 63]`: reserved (must be zero)

_pointer word_
- `[0 .. 63]`: pointer value

_process id word_ (omitted unless thread ref denotes inline thread)
- `[0 .. 63]`: process koid (kernel object id)

_name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

_argument data_ (repeats for each argument)
- (see below)

### Kernel Object Record (record type = 7)

Describes a kernel object, assigns it a label, and optionally associates
key/value data with it as arguments.  Information about the object is added
to a global kernel object table.

When a trace consumer encounters an event with a **Koid Argument**
whose value matches an entry in the kernel object table, it can
cross-reference the argument’s koid value with a prior **Kernel Object Record**
to find a description of the referent.

##### Format

_header word_
- `[0 .. 3]`: record type (7)
- `[4 .. 15]`: record size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 23]`: kernel object type (one of the MX_OBJ_TYPE_XXX constants from <magenta/syscalls/object.h>)
- `[24 .. 39]`: name (string ref)
- `[40 .. 43]`: number of arguments
- `[44 .. 63]`: reserved (must be zero)

_kernel object id word_
- `[0 .. 63]`: koid (kernel object id)

_name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

_argument data_ (repeats for each argument)
- (see below)

##### Argument Conventions

By convention, the trace writer should include the following named arguments
when writing kernel object records about objects of particular types.  This
helps trace consumers correlate relationships among kernel objects.

_This information may not always be available._

- `“process”`: for `MX_OBJ_TYPE_THREAD` objects, specifies the koid of the
  process which contains the thread

### Context Switch Record (record type = 8)

Describes a context switch during which a CPU handed off control from an
outgoing thread to an incoming thread which resumes execution.

The record specifies the new state of the outgoing thread following the
context switch.  By definition, the new state of the incoming thread is
"running" since it was just resumed.

##### Format

_header word_
- `[0 .. 3]`: record type (4)
- `[4 .. 15]`: record size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 23]`: cpu number
- `[24 .. 27]`: outgoing thread state (any of the values below except “running”)
- `[28 .. 35]`: outgoing thread (thread ref)
- `[36 .. 43]`: incoming thread (thread ref)
- `[44 .. 63]`: reserved

_timestamp word_
- `[0 .. 63]`: number of ticks

_outgoing process id word_ (omitted unless outgoing thread ref denotes inline thread)
- `[0 .. 63]`: process koid (kernel object id)

_outgoing thread id word_ (omitted unless outgoing thread ref denotes inline thread)
- `[0 .. 63]`: thread koid (kernel object id)

_incoming process id word_ (omitted unless incoming thread ref denotes inline thread)
- `[0 .. 63]`: process koid (kernel object id)

_incoming thread id word_ (omitted unless incoming thread ref denotes inline thread)
- `[0 .. 63]`: thread koid (kernel object id)

##### Thread States

The following thread states are defined:
- `0`: new
- `1`: running
- `2`: suspended
- `3`: blocked
- `4`: dying
- `5`: dead

These values align with the `MX_THREAD_STATE_XXX` constants from <magenta/syscalls/object.h>.

### Log Record (record type = 9)

Describes a message written to the log at a particular moment in time.

##### Format

_header word_
- `[0 .. 3]`: record type (9)
- `[4 .. 15]`: record size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 30]`: log message length in bytes (range 0x0000 to 0x7fff)
- `[31]`: always zero (0)
- `[32 .. 39]`: thread (thread ref)
- `[40 .. 63]`: reserved (must be zero)

_timestamp word_
- `[0 .. 63]`: number of ticks

_process id word_ (omitted unless thread ref denotes inline thread)
- `[0 .. 63]`: process koid (kernel object id)

_thread id word_ (omitted unless thread ref denotes inline thread)
- `[0 .. 63]`: thread koid (kernel object id)

_log message stream_
- UTF-8 string, padded with zeros to 8 byte alignment

## Argument Types

Arguments associate typed key/value data records.  They are used together
with **Event Record** and **Userspace Object Record** and
**Kernel Object Record**.

Each argument consists of a one word header followed by a variable number
words of payload.  In many cases, the header itself is sufficient to encode
the content of the argument.

### Argument Header

All arguments include this header which specifies the argument's type,
name, and size together with 32 bits of data whose usage varies by
argument type.

##### Format

_argument header word_
- `[0 .. 3]`: argument type
- `[4 .. 15]`: argument size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 31]`: argument name (string ref)
- `[32 .. 63]`: varies (must be zero if not used)

_argument name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

### Null Argument (argument type = 0)

Represents an argument which appears in name only without a value.

##### Format

_argument header word_
- `[0 .. 3]`: argument type (0)
- `[4 .. 15]`: argument size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 31]`: argument name (string ref)
- `[32 .. 63]`: reserved (must be zero)

_argument name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

### 32-bit Signed Integer Argument (argument type = 1)

Represents a 32-bit signed integer.

##### Format

_argument header word_
- `[0 .. 3]`: argument type (1)
- `[4 .. 15]`: argument size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 31]`: argument name (string ref)
- `[32 .. 63]`: 32-bit signed integer

_argument name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

### 32-bit Unsigned Integer Argument (argument type = 2)

Represents a 32-bit unsigned integer.

##### Format

_argument header word_
- `[0 .. 3]`: argument type (2)
- `[4 .. 15]`: argument size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 31]`: argument name (string ref)
- `[32 .. 63]`: 32-bit unsigned integer

_argument name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

### 64-bit Signed Integer Argument (argument type = 3)

Represents a 64-bit signed integer.  If a value will fit in 32-bits, prefer
using the **32-bit Signed Integer Argument** type instead.

##### Format

_argument header word_
- `[0 .. 3]`: argument type (3)
- `[4 .. 15]`: argument size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 31]`: argument name (string ref)
- `[32 .. 63]`: reserved (must be zero)

_argument name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

_argument value word_
- `[0 .. 63]`: 64-bit signed integer

### 64-bit Unsigned Integer Argument (argument type = 4)

Represents a 64-bit unsigned integer.  If a value will fit in 32-bits, prefer
using the **32-bit Unsigned Integer Argument** type instead.

##### Format

_argument header word_
- `[0 .. 3]`: argument type (4)
- `[4 .. 15]`: argument size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 31]`: argument name (string ref)
- `[32 .. 63]`: reserved (must be zero)

_argument name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

_argument value word_
- `[0 .. 63]`: 64-bit unsigned integer

### Double-precision Floating Point Argument (argument type = 5)

Represents a double-precision floating point number.

##### Format

_argument header word_
- `[0 .. 3]`: argument type (5)
- `[4 .. 15]`: argument size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 31]`: argument name (string ref)
- `[32 .. 63]`: reserved (must be zero)

_argument name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

_argument value word_
- `[0 .. 63]`: double-precision floating point number

### String Argument (argument type = 6)

Represents a string value.

##### Format

_argument header word_
- `[0 .. 3]`: argument type (6)
- `[4 .. 15]`: argument size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 31]`: argument name (string ref)
- `[32 .. 47]`: argument value (string ref)
- `[48 .. 63]`: reserved (must be zero)

_argument name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

_argument value stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

### Pointer Argument (argument type = 7)

Represents a pointer value.  Additional information about the referent can
be provided by a **Userspace Object Record** associated with the same pointer.

##### Format

_argument header word_
- `[0 .. 3]`: argument type (7)
- `[4 .. 15]`: argument size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 31]`: argument name (string ref)
- `[32 .. 63]`: reserved (must be zero)

_argument name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

_argument value word_
- `[0 .. 63]`: the pointer value

### Kernel Object Id Argument (argument type = 8)

Represents a koid (kernel object id).  Additional information about the
referent can be provided by a **Kernel Object Record** associated with the
same koid.

##### Format

_argument header word_
- `[0 .. 3]`: argument type (8)
- `[4 .. 15]`: argument size (inclusive of this word) as a multiple of 8 bytes
- `[16 .. 31]`: argument name (string ref)
- `[32 .. 63]`: reserved (must be zero)

_argument name stream_ (omitted unless string ref denotes inline string)
- UTF-8 string, padded with zeros to 8 byte alignment

_argument value word_
- `[0 .. 63]`: the koid (kernel object id)
