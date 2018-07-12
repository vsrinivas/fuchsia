# Glossary


## Overview

The purpose of this document is to provide short definitions for a collection
of technical terms used in the Fuchsia source tree.

#### Adding new definitions

- A definition should be limited to two or three sentences and deliver a
high-level description of a term.
- When another non-trivial technical term needs to be employed as part of the
description, consider adding a definition for that term and linking to it from
the original definition.
- A definition should be complemented by a list of links to more detailed
documentation and related topics.


## Terms

#### **Agent**

A component whose life cycle is not tied to any story, is a singleton in user
scope, and provides services to other components. An agent can be invoked by
other components or by the system in response to triggers like push
notifications. An agent can provide services to components, send and receive
messages, and make proposals to give suggestions to the user.

#### **AppMgr**

The Application Manager (AppMgr) is responsible for launching components and
managing the namespaces in which those components run. It is the first process
started in the `fuchsia` job by the [DevMgr](#DevMgr).

#### **Armadillo**

An implementation of a user shell.

- [Source](https://fuchsia.googlesource.com/topaz/+/master/shell/armadillo_user_shell/)

#### **Base shell**

The platform-guaranteed set of software functionality which provides a basic
user-facing interface for boot, first-use, authentication, escape from and
selection of user shells, and device recovery.

#### **Component**

A component is a unit of execution and accounting. It consists of a manifest
file and associated code, which comes from a Fuchsia package. A component runs
in a sandbox, accesses objects via its [namespace](#Namespace) and publishes
objects via its export directory. [Modules](#Module) and [Agents](#Agent) are
examples of components.

#### **Channel**

A Channel is the fundamental IPC primitive provided by Zircon.  It is a
bidirectional, datagram-like transport that can transfer small messages
including
[Handles](#Handle).
- [Channel Overview](https://fuchsia.googlesource.com/zircon/+/master/docs/objects/channel.md)

#### **DevHost**

A Device Host (DevHost) is a process containing one or more device drivers.  They are
created by the Device Manager, as needed, to provide isolation between drivers for
stability and security.

#### **DevMgr**

The Device Manager (DevMgr) is responsible for enumerating, loading, and
managing the life cycle of device drivers, as well as low level system tasks
(providing filesystem servers for the boot filesystem, launching [AppMgr](
#AppMgr), and so on).

#### **DDK**

The Driver Development Kit is the documentation, APIs, and ABIs necessary to build Zircon
Device Drivers.  Device drivers are implemented as ELF shared libraries loaded by Zircon's
Device Manager.
- [DDK includes](https://fuchsia.googlesource.com/zircon/+/master/system/ulib/ddk/include/ddk/)

#### **Escher**

Graphics library for compositing user interface content. Its design is inspired
by modern real-time and physically based rendering techniques though we
anticipate most of the content it renders to have non-realistic or stylized
qualities suitable for user interfaces.

#### **FAR**

The Fuchsia Archive Format is a container for files to be used by Zircon and Fuchsia.
- [FAR Spec](the-book/archive_format.md)

#### **fdio**

fdio is the Zircon IO Library.  It provides the implementation of posix-style open(), close(),
read(), write(), select(), poll(), etc, against the RemoteIO RPC protocol.  These APIs are
return-not-supported stubs in libc, and linking against libfdio overrides these stubs with
functional implementations.
- [Source](https://fuchsia.googlesource.com/zircon/+/master/system/ulib/fdio/)

#### **FIDL**

The Fuchsia Interface Definition Language (FIDL) is a language for defining protocols
for use over [Channels](#channel). FIDL is programming language agnostic and has
bindings for many popular languages, including C, C++, Dart, Go, and Rust. This
approach lets system components written in a variety of languages interact seamlessly.

#### **Flutter**

[Flutter](https://flutter.io/) is a functional-reactive user interface framework
optimized for Fuchsia and is used by many system components. Flutter also runs on
a variety of other platform, including Android and iOS. Fuchsia itself does not
require you to use any particular language or user interface framework.

#### **Fuchsia Package**

A Fuchsia Package is a collection of files (a manifest and other metadata, and zero or more executables and assets), possibly delivered as a [Fuchsia Archive](#FAR).

#### **FVM**

Fuchsia Volume Manager is a partition manager providing dynamically allocated
groups of blocks known as slices into a virtual block address space. The
FVM partitions provide a block interface enabling filesystems to interact
with it in a manner largely consistent with a regular block device.
- [Filesystems](the-book/filesystems.md)

#### **Garnet**

Garnet is one of the four layers of the Fuchsia codebase.
- [The Fuchsia layer cake](development/source_code/layers.md)
- [Source](https://fuchsia.googlesource.com/garnet/+/master)

#### **GN**

GN is a meta-build system which generates build files so that Fuchsia can be
built with [Ninja](#ninja).
GN is fast and comes with solid tools to manage and explore dependencies.
GN files, named `BUILD.gn`, are located all over the repository.
- [Language and operation](https://chromium.googlesource.com/chromium/src/+/master/tools/gn/docs/language.md)
- [Reference](https://chromium.googlesource.com/chromium/src/tools/gn/+/HEAD/docs/reference.md)
- [Fuchsia build overview](development/build/overview.md)

#### **Handle**

The "file descriptor" of the Zircon kernel.  A Handle is how a userspace process refers
to a kernel object.  They can be passed to other processes over [Channel](#Channel)s.
- [Handle (in Zircon Concepts Doc)](https://fuchsia.googlesource.com/zircon/+/master/docs/concepts.md)

#### **Hub**

The hub is a portal for introspection.  It enables tools to access detailed
structural information about realms and component instances at runtime,
such as their names, job and process ids, and published services.
- [Hub](the-book/hub.md)

#### **Jiri**

Jiri is a tool for multi-repo development. It is used to checkout the Fuchsia
codebase. It supports various subcommands which makes it easy for developers to
manage their local checkouts.
- [Reference](https://fuchsia.googlesource.com/jiri/+/master/README.md)
- [Sub commands](https://fuchsia.googlesource.com/jiri/+/master/README.md#main-commands-are)
- [Behaviour](https://fuchsia.googlesource.com/jiri/+/master/behaviour.md)
- [Tips and tricks](https://fuchsia.googlesource.com/jiri/+/master/howdoi.md)

#### **Job**

TODO(cpu): add definition

#### **Ledger**

[Ledger](https://fuchsia.googlesource.com/peridot/+/master/docs/ledger/README.md)
is a distributed storage system for Fuchsia. Applications use Ledger either
directly or through state synchronization primitives exposed by the Modular
framework that are based on Ledger under-the-hood.

#### **LK**

Little Kernel (LK) is the embedded kernel that formed the core of the Zircon Kernel.
LK is more microcontroller-centric and lacks support for MMUs, userspace, system calls --
features that Zircon added.
- [LK on Github](https://github.com/littlekernel/lk)

#### **Maxwell**

Services to expose ambient and task-related context, suggestions and
infrastructure for leveraging machine intelligence.

#### **Module**

A component with a `module` metadata file which primarily describes the
Module's data compatibility and semantic role.

Modules show UI and participate in Stories at runtime.

- [module metadata format](https://fuchsia.googlesource.com/peridot/+/HEAD/docs/modular/manifests/module.md)

#### **Mozart**

The view subsystem. Includes views, input, compositor, and GPU service.

#### **Musl**

Fuchsia's standard C library (libc) is based on Musl Libc.
- [Source](https://fuchsia.googlesource.com/zircon/+/master/third_party/ulib/musl/)
- [Musl Homepage](https://www.musl-libc.org/)

#### **Namespace**

A namespace is the composite hierarchy of files, directories, sockets, [service](#Service)s,
and other named objects which are offered to components by their
[environment](#Environment).
- [Fuchsia Namespace Spec](the-book/namespaces.md)

#### **Netstack**

TODO(tkilbourn): add definition.

#### **Ninja**

Ninja is the build system executing Fuchsia builds.
It is a small build system with a strong emphasis on speed.
Unlike other systems, Ninja files are not supposed to be manually written but
should be generated by other systems, such as [GN](#gn) in Fuchsia.
- [Manual](https://ninja-build.org/manual.html)
- [Ninja rules in GN](https://gn.googlesource.com/gn/+/master/tools/gn/docs/reference.md#ninja_rules)
- [Fuchsia build overview](development/build/overview.md)

#### **Paver**

A tool in Zircon that installs partition images to internal storage of a device.
- [Guide for installing Fuchsia with paver](/development/workflows/fuchsia_paver.md).

#### **Peridot**

Peridot is one of the four layers of the Fuchsia codebase.
- [The Fuchsia layer cake](development/source_code/layers.md)
- [Source](https://fuchsia.googlesource.com/peridot/+/master)

#### **Realm**

TODO(jeffbrown): add definition.

#### **RemoteIO**

RemoteIO is the Zircon RPC protocol used between fdio (open/close/read/write/ioctl)
and filesystems, device drivers, etc.  As part of [FIDL](#FIDL) v2, it will become a set
of FIDL Interfaces (File, Directory, Device, ...) allowing easier interoperability,
and more flexible asynchronous IO for clients or servers.

#### **Service**

A service is an implementation of a [FIDL](#FIDL) interface. Components can offer
their creator a set of services, which the creator can either use directly or
offer to other components.

Services can also be obtained by interface name from a [Namespace](#namespace),
which lets the component that created the namespace pick the implementation of
the interface. Long-running services, such as [Mozart](#mozart), are typically
obtained through a [Namespace](#namespace), which lets many clients connect to a
common implementation.

#### **Story**

A user-facing logical container encapsulating human activity, satisfied by one
or more related modules. Stories allow users to organize activities in ways they
find natural, without developers having to imagine all those ways ahead of time.

#### **Story Shell**

The system responsible for the visual presentation of a story. Includes the
presenter component, plus structure and state information read from each story.

#### **Topaz**

Topaz is one of the four layers of the Fuchsia codebase.
- [The Fuchsia layer cake](development/source_code/layers.md)
- [Source](https://fuchsia.googlesource.com/topaz/+/master)

#### **User Shell**

The user-specific and replaceable set of software functionality that works in
conjunction with devices to create an environment in which people can interact
with modules.

#### **VDSO**

The VDSO is a Virtual Shared Library -- it is provided by the [Zircon](#Zircon) kernel
and does not appear in the filesystem or a package.  It provides the Zircon System Call
API/ABI to userspace processes in the form of an ELF library that's "always there."
In the Fuchsia SDK and [Zircon DDK](#DDK) it exists as `libzircon.so` for the purpose of
having something to pass to the linker representing the VDSO.

#### **VMAR**

A Virtual Memory Address Range is a Zircon Kernel Object that controls where and how
VMOs may be mapped into the address space of a process.
- [VMAR Overview](https://fuchsia.googlesource.com/zircon/+/master/docs/objects/vm_address_region.md)

#### **VMO**

A Virtual Memory Object is a Zircon Kernel Object that represents a collection of pages
(or the potential for pages) which may be read, written, mapped into the address space of
a process, or shared with another process by passing a Handle over a Channel.
- [VMO Overview](https://fuchsia.googlesource.com/zircon/+/master/docs/objects/vm_object.md)

#### **Zedboot** ####

TODO(raggi): add definition.

#### **Zircon**

Zircon is the [microkernel](https://en.wikipedia.org/wiki/Microkernel) and lowest level
userspace components (driver runtime environment, core drivers, libc, etc) at the core of
Fuchsia.  In a traditional monolithic kernel, many of the userspace components of Zircon
would be part of the kernel itself.
Zircon is also one of the four layers of the Fuchsia codebase.
- [Zircon Documentation](https://fuchsia.googlesource.com/zircon/+/master/README.md)
- [Zircon Concepts](https://fuchsia.googlesource.com/zircon/+/master/docs/concepts.md)
- [The Fuchsia layer cake](development/source_code/layers.md)
- [Source](https://fuchsia.googlesource.com/zircon/+/master)

#### **ZX**

ZX is an abbreviation of "Zircon" used in Zircon C APIs/ABIs (`zx_channel_create()`, `zx_handle_t`,
 `ZX_EVENT_SIGNALED`, etc) and libraries (libzx in particular).
