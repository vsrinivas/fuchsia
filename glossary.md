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

### **AppMgr**

The Application Manager (AppMgr) is responsible for launching applications and
managing the namespaces in which those applications run. It is the first process
started in the `fuchsia` job by the [DevMgr](#DevMgr).

#### **Armadillo**
#### **Component**

#### **Channel**

A Channel is the fundamental IPC primitive provided by Magenta.  It is a bidirectional,
datagram-like transport that can transfer small messages including Handles.
- [Channel Overview](https://fuchsia.googlesource.com/magenta/+/master/docs/objects/channel.md)

#### **DevHost**

A Device Host (DevHost) is a process containing one or more device drivers.  They are
created by the Device Manager, as needed, to provide isolation between drivers for
stability and security.

#### **DevMgr**

The Device Manager (DevMgr) is responsible for enumerating, loading, and managing the
lifecycle of device drivers, as well as low level system tasks (providing filesystem
servers for the boot filesystem, launching [AppMgr](#AppMgr), and so on).

#### **DDK**

The Driver Development Kit is the documentation, APIs, and ABIs necessary to build Magenta
Device Drivers.  Device drivers are implemented as ELF shared libraries loaded by Magenta's
Device Manager.
- [DDK includes](https://fuchsia.googlesource.com/magenta/+/master/system/ulib/ddk/include/ddk/)

#### **Device shell**
#### **Environment**
#### **Escher**

#### **FAR**

The Fuchsia Archive Format is a container for files to be used by Magenta and Fuchsia.
It will replace Magenta's older BootFS container and be used in Fuchsia Packages.
- [FAR Spec](https://fuchsia.googlesource.com/docs/+/master/archive_format.md)

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

#### **GN**

GN is a meta-build system which generates build files so that Fuchsia can be
built with [Ninja](#ninja).
GN is fast and comes with solid tools to manage and explore dependencies.
GN files, named `BUILD.gn`, are located all over the repository.
- [Language and operation](https://chromium.googlesource.com/chromium/src/+/master/tools/gn/docs/language.md)
- [Reference](https://chromium.googlesource.com/chromium/src/tools/gn/+/HEAD/docs/reference.md)

#### **Handle**

The "file descriptor" of the Magenta kernel.  A Handle is how a userspace process refers
to a kernel object.  They can be passed to other processes over [Channel](#Channel)s.
- [Handle (in Magenta Concepts Doc)](https://fuchsia.googlesource.com/magenta/+/master/docs/concepts.md)

#### **Jiri**

#### **Job**

#### **Launchpad**

[Launchpad](launchpad.md) is a library provided by Magenta that provides the
functionality to create and start new processes (including loading ELF binaries,
passing initial RPC messages needed by runtime init, etc).  It is a low-level
library and over time it is expected that few pieces of code will make direct
use of it.
- [Launchpad API (launchpad.h)](https://fuchsia.googlesource.com/magenta/+/master/system/ulib/launchpad/include/launchpad/launchpad.h)

#### **LK**

Little Kernel (LK) is the embedded kernel that formed the core of the Magenta Kernel.
LK is more microcontroller-centric and lacks support for MMUs, userspace, system calls --
features that Magenta added.
- [LK on Github](https://github.com/littlekernel/lk)

#### **Magenta**

Magenta is the [microkernel](https://en.wikipedia.org/wiki/Microkernel) and lowest level
userspace components (driver runtime environment, core drivers, libc, etc) at the core of
Fuchsia.  In a traditional monolithic kernel, many of the userspace components of Magenta
would be part of the kernel itself.
- [Magenta Documentation](https://fuchsia.googlesource.com/magenta/+/master/README.md)
- [Magenta Concepts](https://fuchsia.googlesource.com/magenta/+/master/docs/concepts.md)

#### **Maxwell**
#### **Module**
#### **Mozart**

#### **Musl**

Fuchsia's standard C library (libc) is based on Musl Libc.
- [Source](https://fuchsia.googlesource.com/magenta/+/master/third_party/ulib/musl/)
- [Musl Homepage](https://www.musl-libc.org/)

#### **MX**

MX is an abbreviation of "Magenta" used in Magenta C APIs/ABIs (`mx_channel_create()`, `mx_handle_t`,
 `MX_EVENT_SIGNALED`, etc) and libraries (mxio, mxtl, etc).

#### **mxio**

mxio is the Magenta IO Library.  It provides the implementation of posix-style open(), close(),
read(), write(), select(), poll(), etc, against the RemoteIO RPC protocol.  These APIs are
return-not-supported stubs in libc, and linking against libmxio overrides these stubs with
functional implementations.

#### **Namespace**

A namespace is the composite hierarchy of files, directories, sockets, [service](#Service)s,
and other named objects which are offered to application components by their
[environment](#Environment).
- [Fuchsia Namespace Spec](https://fuchsia.googlesource.com/docs/+/master/namespaces.md)

#### **Netstack**

#### **Ninja**

Ninja is the build system executing Fuchsia builds.
It is a small build system with a strong emphasis on speed.
Unlike other systems, Ninja files are not supposed to be manually written but
should be generated by more featureful systems, such as [GN](#gn) in Fuchsia.
- [Manual](https://ninja-build.org/manual.html)
- [Ninja rules in GN](https://chromium.googlesource.com/chromium/src/tools/gn/+/HEAD/docs/reference.md#ninja_rules)

#### **RemoteIO**

RemoteIO is the Magenta RPC protocol used between mxio (open/close/read/write/ioctl)
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
#### **Story Shell**
#### **User Shell**

#### **VDSO**

The VDSO is a Virtual Shared Library -- it is provided by the [Magenta](#Magenta) kernel
and does not appear in the filesystem or a package.  It provides the Magenta System Call
API/ABI to userspace processes in the form of an ELF library that's "always there."
In the Fuchsia SDK and [Magenta DDK](#DDK) it exists as `libmagenta.so` for the purpose of
having something to pass to the linker representing the VDSO.

#### **VMAR**

A Virtual Memory Address Range is a Magenta Kernel Object that controls where and how
VMOs may be mapped into the address space of a process.
- [VMAR Overview](https://fuchsia.googlesource.com/magenta/+/master/docs/objects/vm_address_region.md)

#### **VMO**

A Virtual Memory Object is a Magenta Kernel Object that represents a collection of pages
(or the potential for pages) which may be read, written, mapped into the address space of
a process, or shared with another process by passing a Handle over a Channel.
- [VMO Overview](https://fuchsia.googlesource.com/magenta/+/master/docs/objects/vm_object.md)

