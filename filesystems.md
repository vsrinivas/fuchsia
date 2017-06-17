# Filesystem Architecture

This document seeks to describe a high-level view of the Fuchsia filesystems,
from their initialization, discussion of standard filesystem operations (such as
Open, Read, Write, etc), and the quirks of implementing user-space filesystems
on top of a microkernel. Additionally, this document describes the VFS-level
walking through a namespace which can be used to communicate with non-storage
entities (such as system services).

## Implications of Existing in a Microkernel

Unlike more common monolithic kernels, Fuchsia’s filesystems live entirely
within userspace. They are not linked nor loaded with the kernel; they are
simply userspace processes which implement servers that can appear as
filesystems. As a consequence, Fuchsia’s filesystems themselves can be changed
with ease -- modifications don’t require recompiling the kernel. In fact,
updating to a new Fuchsia filesystem can be done without rebooting.

Like other native servers on Fuchsia, the primary mode of interaction with a
filesystem server is achieved using the handle primitive rather than system
calls. The kernel has no knowledge about files, directories, or filesystems. As
a consequence, filesystem clients cannot ask the kernel for “filesystem access”
directly.

This architecture implies that the interaction with filesystems are limited to
an intriguing interface:

 * The messages sent on communication channels established with the filesystem
   server.
 * The initialization routine (which is expected to be configured heavily on a
   per-filesystem basis; a networking filesystem would require network access,
   persistent filesystems may require block device access, in-memory filesystems
   would only require a mechanism to allocate new temporary pages).

As a benefit of this interface, any resources accessible via a channel can make
themselves appear like filesystems by implementing the expected protocols for
files or directories. For example, “serviceFS” (discussed in more detail later
in this document) allows for service discovery through a filesystem interface.

## Life of an 'Open'

To provide an end-to-end picture of filesystem access on Fuchsia, this section
dives into the details of each layer which is used when doing something as
simple as opening a file. It’s important to note: all of these layers exist in
userspace; even when interacting with filesystem servers and drivers, the kernel
is merely used to pass messages from one component to another.

Now, let’s begin. You call:

`open(“foobar”);`

Where does that request go?

### Standard Library: Where 'open' is defined

The ‘open’ call is a function, provided by a standard library. For C/C++
programs, this will normally be declared in `unistd.h`, which has a backing
definition in
[libmxio](https://fuchsia.googlesource.com/magenta/+/master/system/ulib/mxio/).
For Go programs, there is an equivalent (but distinct!) implementation in the
Go standard library. For each language and runtime, developers may opt into
their own definition of “open”.

On a monolithic kernel, `open` would be a lightweight shim around a system
call, where the kernel might handle path parsing, redirection, etc. In that
model, the kernel would need to mediate access to resources based on exterior
knowledge about the caller. The Magenta kernel, however, intentionally has no
such system call. Instead, clients access filesystems through **channels** --
when a process is initialized, it may be provided a handle representing the
**root (“/”) directory** and a handle representing the current working
directory **(CWD)**.  Alternatively, in more exotic runtimes, one or more of
these handles may not be provided. In our example, however, where we requested
to open “foobar”, a relative path was used, so the incoming call could be sent
over the path representing the current working directory.

The standard library is responsible for taking a handle (or multiple handles)
and making them appear like file descriptors. As a consequence, the “file
descriptor table” is a notion that exists within a client process (if a client
chooses to use a custom runtime, they can view their resources purely as
handles -- the “file descriptor” wrapping is optional).

This raises a question, however: given a file descriptor to files, sockets,
pipes, etc, what does the standard library do to make all these resources
appear functionally the same? How does that client know what messages to send
over these handles?

### Mxio: A client-side abstraction layer

A layer called **mxio** is responsible for providing a unified interface for a
variety of resources -- files, sockets, services, pipes, and more. This layer
defines a group of functions, such as **read, write, open, close, seek, etc**
that may be used on file descriptors backed by a variety of protocols. Each
supported protocol is responsible for providing client-side code to interpret
the specifics of their interaction. For example, **sockets** provide multiple
handles to clients; one acting for data flow, and one acting as a control
plane. In contrast, **files** typically use only a single channel for control
and data (unless extra work has been done to ask for a memory mapping).
Although both sockets and files might receive a call to `open` or `write`, they
will need to interpret those commands differently.

For the purposes of this document, we’ll be focusing on the primary protocol
used by filesystem clients: RemoteIO.

### RemoteIO: How filesystems clients talk to filesystem servers

Our program calling `open("foo")` has called into the standard library, found an
“mxio” object corresponding to the current working directory, and is about to
send a request to “please open foo”. How can this be accomplished? The client
has the following tools:

  * One or more **handles** representing a connection to the CWD
  * [mx_channel_write](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/channel_write.md):
    A system call which can send bytes and handles (over a channel)
  * [mx_channel_read](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/channel_read.md):
    A system call which can receive bytes and handles (over a channel)
  * [mx_object_wait_one](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/object_wait_one.md):
    A system call which can wait for a handle to be readable / writable

Using these primitives, the client can write a message to the filesystem server
on the CWD handle, which the server can read and then respond to with a
“success” or “failure message” in a write back to the client. While the server
is crunching away, figuring out what to actually open, the client may or may
not choose to wait before trying to read the status message.

It’s important that the client and server agree on the interpretation of those
N bytes and N handles when messages are transmitted or received: if there is
disagreement between them, messages might be dropped (or worse, contorted into
an unintended behavior). Additionally, if this protocol allowed the client to
have arbitrary control over the server, this communication layer would be ripe
for exploitation.

The [RemoteIO protocol
(RIO)](https://fuchsia.googlesource.com/magenta/+/master/system/ulib/mxio/include/mxio/remoteio.h)
describes the wire-format of what these bytes and handles should actually mean
when transmitted between two entities. The protocol describes things like
“expected number of handles”, “enumerated operation”, and “data”. In our case,
`open("foo")` creates an `MXRIO_OPEN` message, and sets the “data” field of the RIO
message to the string “foo”. Additionally, if we choose to pass any flags to
open (such as `O_RDONLY, O_RDWR, O_CREAT`, etc) these flags would be placed in
the “arg” field of the rio structure. However, if the operation was changed
(to, for example, `write`), the interpretation of this message would be
altered.

Exact byte agreement at this layer is critical, as it allows communication
between drastically different runtimes: **processes which understand RIO can
communicate easily between C, C++, Go, Rust, Dart programs (and more!)
transparently.**

This protocol, although incredibly useful, has currently been
constructed by hand, and is only used by the lower-level components within the
system. There is an ongoing effort to re-specify Remote IO as a FIDL
interface, which is used by the higher-level components of the Fuchsia, to take
advantage of the multiple-runtime interoperability that FIDL provides. Ideally,
the two protocols will become unified, and FIDL binding will be automatically
generated and typechecked in a variety of languages.

**libmxio** contains both the client and server-side code for the C/C++
implementation of RIO, and is responsible for cautiously verifying the input
and output of both ends.

In the case of our `open` operation, the remote IO protocol expects that the
client will create a channel and pass one end (as a handle) to the server. Once
the transaction is complete, this channel may be used as the mechanism to
communicate with the opened file, just as we had previously been communicating
with the “CWD” handle.

By designing the protocol so RIO clients provide handles, rather than servers,
the communication is better suited to pipelining. Access to RIO objects can be
asynchronous; requests to the RIO object can be transmitted before the object
is actually opened! This behavior is critical for interaction with services
(which will be described in more detail in the “ServiceFS” section).

To recap, our “open” call has gone through the standard library, acted on the
“CWD” mxio object, which transformed the request into a RIO message which is
sent to the server using the `mx_channel_write` system call. The client can
optionally wait for the server’s response using `mx_object_wait_one`, or continue
chugging along asynchronously. Either way, a channel has been created, where
one end lives with the client, and the other end is transmitted to the
“server".

### RemoteIO: Server-Side

#### Dispatching

Once the message has been transmitted from the client’s side of the channel, it
lives in the server’s side of the channel, waiting to be read. The server is
identified by “whoever holds the handle to the other end of the channel” -- it
may live in the same (or a different) process as the client, use the same (or a
different) runtime than the client, and be written in the same (or a different
language) than the client. By using an agreed-upon wire-format, the
interprocess dependencies are bottlenecked at the thin communication layer that
occurs over channels.

At some point in the future, this server-side end of the CWD handle will need
to read the message transmitted by the client. This process isn’t automatic --
the server will need to intentionally wait for incoming messages on the
receiving handle, which in this case was the “current working directory”
handle. When server objects (files, directories, services, etc) are opened,
their handles are registered with a server-side Magenta **port** that waits for
their underlying handles to be **readable** (implying a message has arrived) or
**closed** (implying they will never receive more messages). This object which
dispatches incoming requests to appropriate handles is known as the dispatcher;
it is responsible for redirecting incoming messages to a callback function,
along with some previously-supplied “iostate” representing the open connection.

For C++ filesystems using libfs, this callback function is called
`vfs_handler`, and it receives a couple key pieces of information:

  * The RIO message which was provided by the client (or artificially constructed
    by the server to appear like a “close” message, if the handle was closed)
  * The I/O state representing the current connection to the handle (passed as the
    “iostate” field, mentioned earlier).

`vfs_handler` can interpret the I/O state to infer additional information:

  * The seek pointer within the file (or within the directory, if readdir has been used)
  * The flags used to open the underlying resource
  * The Vnode which represents the underlying object (and may be shared between
    multiple clients, or multiple file descriptors)

This handler function, equipped with this information, acts as a large
“switch/case” table, redirecting the RIO message to an appropriate function
depending on the “operation” field provided by the client. In our case, the
MXRIO_OPEN field is noticed as the operation, so (1) a handle is expected, and
(2) the ‘data’ field (“foo”) is interpreted as the path.

#### VFS Layer

In Fuchsia, the “VFS layer” is a filesystem-independent library of code which
may dispatch and interpret server-side messages, and call operations in the
underlying filesystem where appropriate. Notably, this layer is completely
optional -- if a filesystem server does not want to link against this library,
they have no obligation to use it. To be a filesystem server, a process must
merely understand the remote IO wire format. As a consequence, there could be
any number of “VFS” implementations in a language, but at the time of writing,
two well-known implementations exist: one written in C++ within the [libfs
library](https://fuchsia.googlesource.com/magenta/+/master/system/ulib/fs/),
and another written in Go in the [rpc package of
ThinFS](https://fuchsia.googlesource.com/thinfs/+/master/magenta/rpc/rpc.go)]

The VFS layer defines the interface of operations which may be routed to the
underlying filesystem, including:

  * Read/Write to a Vnode
  * Lookup/Create/Unlink a Vnode (by name) from a parent Vnode
  * Rename/Link a Vnode by name
  * And many more

To implement a filesystem (assuming a developer wants to use the shared VFS
layer), one simply needs to define a Vnode implementing this interface and link
against a VFS layer. This will provide functionality like “path walking” and
“filesystem mounting” with minimal effort, and almost no duplicated code. In an
effort to be filesystem-agnostic, the VFS layer has no preconceived notion of
the underlying storage used by the filesystem: filesystems may require access
to block devices, networks, or simply memory to store data -- but the VFS layer
only deals with interfaces acting on paths, byte arrays of data, and vnodes.

#### Path Walking

To open a server-side resource, the server is provided some starting point
(represented by the called handle) and a string path. This path is split into
segments by the “/” character, and each component is “looked up” with a
callback to the underlying filesystem. If the lookup successfully returns a
vnode, and another “/” segment is detected, then the process continues until
(1) we fail to find a component, (2) we reach the last component in a path, or
(3) we find a **mountpoint vnode**, which is a vnode that has an attached “remote”
handle. For now, we will ignore mountpoint vnodes, although they are discussed
in a section on [filesystem mounting](#Mounting) below.

In our case, let’s assume we successfully lookup the “foo” Vnode. The
filesystem server will proceed to call the VFS interface “Open”, verifying that
the requested resource can be accessed with the provided flags, before calling
“GetHandles” asking the underlying filesystem if there are additional handles
required to interact with the Vnode. Assuming the client asked for the “foo”
object synchronously (which is implied with the default POSIX open call), any
additional handles required to interact with “foo” are packed into a small RIO
description object and passed back to the client. Alternatively, if we had
failed to open “foo”, we would still send back a RIO description object, but
with the “status” field set to an error code, indicating failure. In our case,
let’s assume the “foo” open was successful, so we proceed to create an
“iostate” object for “foo” and register it with the dispatcher. Doing so,
future calls to “foo” can be handled by the server. “Foo” has been opened, the
client is now ready to send additional requests.

From the client’s perspective, at the start of the “Open” call, a path and
handle combination was transmitted over the CWD handle to a remote filesystem
server. Since the call was synchronous, the client proceeded to wait for a
response on the handle. Once the server properly found, opened, and initialized
I/O state for this file, it sent back a “success” RIO description object. This
object would be read by the client, identifying that the call completed
successfully. At this point, the client could create an mxio object
representing the handle to “foo”, reference it with an entry in a file
descriptor table, and return the fd back to whoever called the original “open”
function. Furthermore, if the client wants to send any additional requests
(such as “read” or “write”) to ‘foo’, then they can communicate directly with
the filesystem server by using the connection to the opened file -- there is no
need to route through the ‘CWD’ on future requests.

### Life of an Open: Diagrams

```
             +----------------+
             | Client Program |
+-----------------------------+
|   fd: x    |   fd: y    |
| Mxio (RIO) | Mxio (RIO) |
+-------------------------+
| '/' Handle | CWD Handle |
+-------------------------+
      ^            ^
      |            |
Magenta Channels, speaking RIO                   State BEFORE open(‘foo’)
      |            |
      v            v
+-------------------------+
| '/' Handle | CWD Handle |
+-------------------------+
|  I/O State |  I/O State |
+-------------------------+
|   Vnode A  |   Vnode B  |
+------------------------------+
           | Filesystem Server |
           +-------------------+


             +----------------+
             | Client Program |
+-----------------------------+
|   fd: x    |   fd: y    |
| Mxio (RIO) | Mxio (RIO) |
+-------------------------+
| '/' Handle | CWD Handle |   **foo Handle x2**
+-------------------------+
      ^            ^
      |            |
Magenta Channels, speaking RIO                   Client Creates Channel
      |            |
      v            v
+-------------------------+
| '/' Handle | CWD Handle |
+-------------------------+
|  I/O State |  I/O State |
+-------------------------+
|   Vnode A  |   Vnode B  |
+------------------------------+
           | Filesystem Server |
           +-------------------+


             +----------------+
             | Client Program |
+-----------------------------+
|   fd: x    |   fd: y    |
| Mxio (RIO) | Mxio (RIO) |
+-------------------------+--------------+
| '/' Handle | CWD Handle | ‘foo’ Handle |
+-------------------------+--------------+
      ^            ^
      |            |
Magenta Channels, speaking RIO                   Client Sends RIO message to Server
      |            |                             Message includes a ‘foo’ handle
      v            v                             (and waits for response)
+-------------------------+
| '/' Handle | CWD Handle |
+-------------------------+
|  I/O State |  I/O State |
+-------------------------+
|   Vnode A  |   Vnode B  |
+------------------------------+
           | Filesystem Server |
           +-------------------+


             +----------------+
             | Client Program |
+-----------------------------+
|   fd: x    |   fd: y    |
| Mxio (RIO) | Mxio (RIO) |
+-------------------------+--------------+
| '/' Handle | CWD Handle | ‘foo’ Handle |
+-------------------------+--------------+
      ^            ^
      |            |
Magenta Channels, speaking RIO                   Server dispatches message to I/O State,
      |            |                             Interprets as ‘open’
      v            v                             Finds or Creates ‘foo’
+-------------------------+
| '/' Handle | CWD Handle |
+-------------------------+
|  I/O State |  I/O State |
+-------------------------+-------------+
|   Vnode A  |   Vnode B  |   Vnode C   |
+------------------------------+--------+
           | Filesystem Server |
           +-------------------+


             +----------------+
             | Client Program |
+-----------------------------+
|   fd: x    |   fd: y    |
| Mxio (RIO) | Mxio (RIO) |
+-------------------------+--------------+
| '/' Handle | CWD Handle | ‘foo’ Handle |
+-------------------------+--------------+
      ^            ^          ^
      |            |          |
Magenta Channels, speaking RIO|                  Server allocates I/O state for Vnode
      |            |          |                  Responds to client-provided handle
      v            v          v
+-------------------------+--------------+
| '/' Handle | CWD Handle | ‘foo’ Handle |
+-------------------------+--------------+
|  I/O State |  I/O State |  I/O State   |
+-------------------------+--------------+
|   Vnode A  |   Vnode B  |    Vnode C   |
+------------------------------+---------+
           | Filesystem Server |
           +-------------------+


             +----------------+
             | Client Program |
+-----------------------------+----------+
|   fd: x    |   fd: y    |    fd: z     |
| Mxio (RIO) | Mxio (RIO) |  Mxio (RIO)  |
+-------------------------+--------------+
| '/' Handle | CWD Handle | ‘foo’ Handle |
+-------------------------+--------------+
      ^            ^          ^
      |            |          |
Magenta Channels, speaking RIO|                  Client recognizes that ‘foo’ was opened
      |            |          |                  Allocated Mxio + fd, ‘open’ succeeds!
      v            v          v
+-------------------------+--------------+
| '/' Handle | CWD Handle | ‘foo’ Handle |
+-------------------------+--------------+
|  I/O State |  I/O State |  I/O State   |
+-------------------------+--------------+
|   Vnode A  |   Vnode B  |    Vnode C   |
+------------------------------+---------+
           | Filesystem Server |
           +-------------------+
```

## Filesystem Operations

### Passing Data

Once a connection has been established, either to a file or a directory,
subsequent operations are substantially easier to understand -- they appear
functionally very similar to the first half of `open`, where a message is
transmitted on a handle, and the client may optionally wait for a response.

For example, to seek within a file, a client would send an `MXRIO_SEEK` message
with the desired position and “whence” within the RIO message, and the new seek
position would be returned. To truncate a file, an `MXIO_TRUNCATE` message
could be sent with the new desired filesystem, and a status message would be
returned. To read a directory, an `MXRIO_READDIR` message could be sent, and a
list of direntries would be returned. If these requests were sent to a
filesystem entity that can’t handle them, an error would be sent, and the
operation would not be executed (like an `MXRIO_READDIR` message sent to a text
file).

#### Memory Mapping

For filesystems capable of supporting it, memory mapping files is slightly more
complicated. To actually “mmap” part of a file, a client sends an “MXIO_MMAP”
message, and receives a Virtual Memory Object, or VMO, in response. This object
is then typically mapped into the client’s address space using a Virtual Memory
Address Region, or VMAR. Transmitting a limited view of the file’s internal
“VMO” back to the client requires extra work by the intermediate message
passing layers, so they can be aware they’re passing back a server-vendored
object handle.

By passing back these virtual memory objects, clients can quickly access the
internal bytes representing the file without actually undergoing the cost of a
round-trip IPC message. This feature makes mmap an attractive option for
clients attempting high-throughput on filesystem interaction.

At the time of writing, on-demand paging is not supported by the
kernel, and has not been wired into filesystems. As a result, if a client
writes to a “memory-mapped” region, the filesystem cannot reasonably identify
which pages have and have not been touched. To cope with this restriction, mmap
has only been implemented on **read-only filesystems**, such as the blobstore.

#### Block Device Interaction

A reader considering classic disk-using filesystems may have the question on
their minds: “How exactly does a filesystem interact with a block device on
Fuchsia?”. In order to fully understand this question, it is important to
consider what the software layers of block devices actually are: userspace
drivers. Similar to most components on Fuchsia, this implies that they are
accessible via IPC primitives, and disk-oriented filesystems will have one or
more handles to these underlying drivers. Similar to filesystem clients, which
may send “read” or “write” requests to servers by encoding these requests
within MXRIO messages, filesystems servers act as clients to block devices, and
may transmit MXRIO messages to a “device host” (referred to as “devhost” within
Magenta). The devhost then transforms these requests into driver-understood
“I/O transactions”, where they are actually transmitted to the real hardware.

Under the hood, these block device drivers are often responsible for taking
large portions of memory, and queueing requests to a particular device to
either “read into” or “write from” a portion of memory. Unfortunately, as a
consequence of transmitting messages of a limited size from a “RIO protocol”
into an “I/O transaction”, repeated copying of large buffers is often required
to access block devices.

To avoid this performance bottleneck, the block device drivers implement
another mechanism to transmit reads and writes: a fast, FIFO-based protocol
which acts on a shared VMO. Filesystems (or any other client wishing to
interact with a block device) can acquire FIFOs from a block device, register a
“transaction buffer”, and pass handles to VMOs to the block device. Instead of
transmitting “read” or “write” messages with large buffers, a client of this
protocol can instead send a fast, lightweight control message on a FIFO,
indicating that the block device driver should act directly on the
already-registered VMO. For example, when writing to a file, rather than
passing bytes over IPC primitives directly, and copying them to a new location
in the block device’s memory, a filesystem (representing the file as a VMO)
could simply send a small FIFO message indicating “write N bytes directly from
offset X of VMO Y to offset Z on a disk”. When combined with the “mmap”
memory-mapping tools, this provides a “zero-copy” pathway directly from client
programs to disk (or in the other direction) when accessing files.

### Mounting

When Fuchsia filesystems are initialized, they are created with typically two
handles: One handle to a channel used to communicate with the mounting
filesystem (referred to as the “mount point” channel -- the “mounting” end of
this channel is saved as a field named “remote” in the parent Vnode, the other
end will be connected to the root directory of the new filesystem), and
(optionally) another to contact the underlying block device. Once a filesystem
has been initialized (reading initial state off the block device, finding the
root vnode, etc) it flags a signal (`MX_USER_SIGNAL0`) on the mount point
channel. This informs the parent (mounting) system that the child filesystem is
ready to be utilized. At this point, the channel passed to the filesystem on
initialization may be used to send filesystem requests, such as “open”.

At this point, the parent (mounting) filesystem “pins” the connection to the
remote filesystem on a Vnode. The VFS layers capable of path walking check for
this remote handle when observing Vnodes: if a remote handle is detected, then
the incoming request (open, rename, etc) is forwarded to the remote filesystem
instead of the underlying node. If a user actually wants to interact with the
mountpoint node, rather than the remote filesystem, they can pass the
`O_NOREMOTE` flag to the “open” operation identify this intention.

Unlike many other operating systems, the notion of “mounted filesystems” does
not live in a globally accessible table. Instead, the question “what
mountpoints exist?” can only be answered on a filesystem-specific basis -- an
arbitrary filesystem may not have access to the information about what
mountpoints exist elsewhere.

### Filesystem Initialization (Client's Perspective)

The previous discussion on filesystem operations all relied on a critical
assumption: all operations act on an already-existent connection (even “open”,
which must begin opening a resource from either the root or the CWD
connection). How are these resources initialized? In particular, those two key
connections -- “root” and “CWD” -- where do they come from?

Although the full details of process creation are outside the scope of this
document, there exists a collection of “handle + metadata” tuples which are
passed to and interpreted by the child process. This bootstrapping, though
fairly intuitive, has some more complex implications: First, child processes
may perceive different filesystem “roots”. Secondly, these initialization
tuples can be extended beyond “root” and “CWD” into arbitrary prefix namespaces
within the client.

### Dot Dot Considered Harmful

Child processes on Fuchsia are only capable of accessing the resources provided
to them -- this is an essential idea encompassing microkernels, and other
“capability-based” systems. If a handle is provided to a service, access to
that handle implies the client can use it.

Intuitively, this concept can be applied to filesystems: If a handle is
provided to a directory, it should imply access to resources within that
directory (and additionally, their subdirectories). Unfortunately, however, a
holdout from POSIX prevents directory handles from cleanly integrating with
these concepts in a capability system: “..”. If a handle is provided to a
directory, the client can simply request “..”, and the handle will be
“upgraded” to access the parent directory, with broader scope. As a
consequence, this implies that a handle to a directory can be upgraded
arbitrarily to access the entire filesystem.

Traditionally, filesystems have tried to combat this using "chroot", which
changes the notion of a filesystem root, preventing access beyond ".." in
trivial cases of path traversal. However, this approach has some problems:

  * Chroot changes the notion of root on a coarse, "per-program" basis, not on
    a per-descriptor basis
  * Chroots are often misused (i.e., fchdir to a different open handle which
    sits outside the chroot)
  * Chroots are not "on by default", so it may be tempting for (insecure,
    poorly written) programs to simply not use them.

To overcome these deficiencies, Fuchsia does not implement traditional dot dot
semantics on filesystem servers, which would allow open directories to traverse
upward. More specifically, it disallows access to “..”, preventing clients
from trivially accessing parent directories. This provides some strong
properties for process creation: If an application manager only wants to give a
process access to "/data/my_private_data", then it can simply provide a handle
to that open directory to the child process, and it will "automatically" be
sandboxed.

#### What about paths which can be resolved without the filesystem server?

Certain paths, such as “foo/../bar”, which can be transformed to “bar”, can be
determined without accessing a filesystem server in the absence of symbolic
links (and at the time of writing, symbolic links do not exist on Fuchsia).
These paths may be canonicalized, or cleaned, on the client-side, prior to
sending path-based requests to filesystem servers: the libmxio library already
does this for any mxio operations which are eventually transmitted to
filesystem servers in a function called `__mxio_cleanpath`.

#### What about shell traversal?

I.e, if someone “cd”s into a directory, how can they leave? Internally, the
notion of “CWD” isn’t merely a file descriptor to an open directory; rather,
it’s a combination of “file descriptor” and “absolute path interpreted to mean
CWD”. If all operations to cd act on this absolute path, then “..” can always
be resolved locally on a client, rather than being transmitted to a filesystem
server. For example, if the CWD is “/foo/bar”, and a user calls “cd ..”, then
the underlying call may be transformed into “chdir /foo/bar/..”, which can be
canonicalized to “/foo”.

Once these hurdles have been overcome, the benefits of removing “..” are
enormous: access to filesystem resources fits naturally within the capability
system, sandboxing new processes becomes massively easier, and resource access
can more naturally be composed through filesystem namespaces.

### Namespaces

On Fuchsia, a [namespace](namespaces.md) is a small filesystem which exists
entirely within the client. At the most basic level, the idea of the client
saving “/” as root and associating a handle is a very primitive namespace.
However, this idea can be extended: When a client refers to “/bin”, the client
may opt to redirect these requests to a local handle representing the “/bin”
directory, before sending a request directly to the “bin” directory within the
“root” directory.  Namespaces, like the majority of filesystem constructs, are
not visible from the kernel: rather, they are implemented in client-side
runtimes (such as libmxio) and are embedded between most client code and the
handles to remote filesystems.

Since namespaces operate on handles, and most Fuchsia resources and services
are accessible through handles, they are extremely powerful concepts.
Filesystem objects (such as directories and files), services, devices,
packages, and environments (visible by privileged processes) all are usable
through handles, and may be composed arbitrarily within a child process. As a
result, namespaces allows for customizable resource discovery within
applications. The services that one process observes within “/svc” may or may
not match what other processes see, and can be restricted or redirected
according to application-launching policy. For more detail the mechanisms
and policies applied to restricting process capability, refer to the
documentation on
[sandboxing](https://fuchsia.googlesource.com/docs/+/master/sandboxing.md).

### Other Operations acting on paths

In addition to the “open” operation, there are a couple other path-based
operations worth discussing: “rename” and “link”. Unlike “open”, these
operations actually act on multiple paths at once, rather than a single
location. This complicates their usage: if a call to “rename(‘/foo/bar’,
‘baz’)” is made, the filesystem needs to figure out a way to:

  * Traverse both paths, even when they have distinct starting points (which is the
    case this here; one path starts at root, and other starts at the CWD)
  * Open the parent directories of both paths
  * Operate on both parent directories and trailing pathnames simultaneously

To satisfy this behavior, the VFS layer takes advantage of a Magenta concept
called “cookies”. These cookies allow client-side operations to store open
state on a server, using a handle, and refer to it later using that same
handles. Fuchsia filesystems use this ability to refer to one Vnode while
acting on the other.

These multi-path operations do the following:
  * Open the parent source vnode (for “/foo/bar”, this means opening “/foo”)
  * Open the target parent vnode (for “baz”, this means opening the current
    working directory) and acquire a vnode token using the operation
    `IOCTL_DEVMGR_GET_TOKEN`, which is a handle to a filesystem cookie.
  * Send a “rename” request to the source parent vnode, along with the source
    and destination paths (“bar” and “baz”), along with the vnode token acquired
    earlier. This provides a mechanism for the filesystem to safely refer to the
    destination vnode indirectly -- if the client provides an invalid handle, the
    kernel will reject the request to access the cookie, and the server can return
    an error.

## Current Filesystems

Due to the modular nature of Fuchsia’s architecture, it is straightforward to
add filesystems to the system. At the moment, a handful of filesystems exist,
intending to satisfy a variety of distinct needs.

### MemFS: An in-memory filesystem

[MemFS](https://fuchsia.googlesource.com/magenta/+/master/system/core/devmgr/vfs-memory.cpp)
is used to implement requests to temporary filesystems like `/tmp`, where files
exist entirely in RAM, and are not transmitted to an underlying block device.
This filesystem is also currently used for the “bootfs” protocol, where a
large, read-only VMO representing a collection of files and directories is
unwrapped into user-accessible Vnodes at boot (for readers familiar with the
system, these files are accessible in `/boot`).

### MinFS: A persistent filesystem

[MinFS](https://fuchsia.googlesource.com/magenta/+/master/system/uapp/minfs/)
is a simple, traditional filesystem which is capable of storing files
persistently. Like MemFS, it makes extensive use of the VFS layers mentioned
earlier, but unlike MemFS, it requires an additional handle to a block device
(which is transmitted on startup to a new MinFS process). For ease of use,
MinFS also supplies a variety of tools: “mkfs” for formatting, “fsck” for
verification, as well as “mount” and “umount” for adding and subtracting MinFS
filesystems to a namespace from the command line.

### Blobstore: An immutable, integrity-verifying package storage filesystem

[Blobstore](https://fuchsia.googlesource.com/magenta/+/master/system/uapp/blobstore/)
is a simple, flat filesystem optimized for “write-once, then read-only” [signed
data](merkleroot.md), such as [application packages](package_metadata.md).
Other than two small prerequisites (file names which are deterministic, content
addressable hashes of a file’s Merkle Tree root, for integrity-verification)
and forward knowledge of file size (identified to the Blobstore by a call to
“ftruncate” before writing a blob to storage), the Blobstore appears like a
typical filesystem. It can be mounted and unmounted, it appears to contain a
single flat directory of hashes, and blobs can be accessed by operations like
“open”, “read”, “stat” and “mmap”.

### ThinFS: A FAT filesystem written in Go

[ThinFS](https://fuchsia.googlesource.com/thinfs/) is an implementation of a
FAT filesystem in Go. It serves a dual purpose: first, proving that our system
is actually modular, and capable of using novel filesystems, regardless of
language or runtime. Secondly, it provides a mechanism for reading a universal
filesystem, found on EFI partitions and many USB sticks.

### ServiceFS (svcfs): A filesystem used for service discovery

[ServiceFS](https://fuchsia.googlesource.com/magenta/+/master/system/ulib/svcfs),
used by the [Fuchsia Application
Manager](https://fuchsia.googlesource.com/application/+/master), provides a
mechanism for discovering non-filesystem services using the filesystem
namespace. As mentioned earlier, the client-provided tools in mxio which can be
used to access files have the option of both synchronous and asynchronous
operations, and upon success, return a handle (or handles) to a remote
filesystem. ServiceFS takes this one step further -- since arbitrary services
are accessible by handle, why not use this universal mechanism to access objects
that might not implement operations like “read” and “write”, but might implement
arbitrary other protocols? Svcfs, combined with FIDL is the primary mechanism to
find and talk to arbitrary components that exist within a process’s namespace.
