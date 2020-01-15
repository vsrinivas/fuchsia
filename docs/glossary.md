# Glossary

## Overview

The purpose of this document is to provide short definitions for a collection of
technical terms used in Fuchsia.

#### Adding new definitions

-   A definition should provide a high-level description of a term and in most
    cases should not be longer than two or three sentences.
-   When another non-trivial technical term needs to be employed as part of the
    description, consider adding a definition for that term and linking to it
    from the original definition.
-   A definition should be complemented by a list of links to more detailed
    documentation and related topics.

## Terms

#### **Agent** {#agent}

This is a [Modular](/docs/concepts/modular/overview.md) concept that is being deprecated.

An agent is a role a [component](#component) can play to execute in the
background in the context of a [session](#session). An agent's life cycle is not
tied to any [story](#story), is a singleton per session, and provides services
to other components. An agent can be invoked by other components or by the
system in response to triggers like push notifications. An agent can provide
services to components, send and receive messages, and make proposals to give
suggestions to the user.

#### **AppMgr** {#appmgr}

The Application Manager (AppMgr) is responsible for launching components and
managing the namespaces in which those components run. It is the first process
started in the `fuchsia` job by the [DevMgr](#devmgr).

#### **Banjo** {#banjo}

Banjo is a language for defining protocols that are used to communicate between
[drivers](#driver). It is different from [FIDL](#fidl) in that it specifies an
ABI for drivers to use to call into each other, rather than an IPC protocol.

#### **Base shell** {#base-shell}

The platform-guaranteed set of software functionality which provides a basic
user-facing interface for boot, first-use, authentication, escape from and
selection of session shells, and device recovery.

#### **BlackBoxTest** {#black-box-test}

`BlackBoxTest` is a Rust client-side library that sets up black box integration
tests for a v2 component.

-   [Documentation](/docs/concepts/components/black_box_testing.md#blackboxtest)

#### **bootfs** {#bootfs}

The bootfs RAM disk contains the files needed early in the boot process when no
other filesystems are available. It is part of the [ZBI](#zircon-boot-image),
and is decompressed and served by [bootsvc](#bootsvc). After the early boot
process is complete, the bootfs is mounted at `/boot`.

-   [Documentation](/docs/concepts/booting/userboot.md#BOOTFS)

#### **bootsvc** {#bootsvc}

`bootsvc` is the second process started in Fuchsia. It provides a filesystem
service for the [bootfs](#bootfs) and a loader service that loads programs from
the same bootfs. After starting these services, it loads the third program,
which defaults to `devmgr`.

-   [Documentation](/docs/concepts/booting/bootsvc.md)

#### **BreakpointSystemClient** {#breakpoint-system-client}

`BreakpointSystemClient` is a Rust client-side library that connects to
the breakpoint system in component manager. It allows setting breakpoints on
system events in component manager. Integration tests use this library to
verify the state of components.

-   [Documentation](/docs/concepts/components/black_box_testing.md#breakpointsystemclient)

#### **Bus Driver** {#bus-driver}

A [driver](#driver) for a device that has multiple children. For example,
hardware interfaces like PCI specify a topology in which a single controller is
used to interface with multiple devices connected to it. In that situation, the
driver for the controller would be a bus driver.

#### **Cache directory** {#cache-directory}

Similar to a [data directory](#data-directory), except that the contents of a
cache directory may be cleared by the system at any time, such as when the
device is under storage pressure. Canonically mapped to /cache in the component
instance’s [namespace](#namespace).

-   [Testing isolated cache storage](/docs/concepts/testing/testing_isolated_cache_storage.md).

#### **Capability** {#capability}

A capability is a value which combines an *object reference* and a set of
*rights*. When a program has a capability it is conferred the privilege to
perform certain actions using that capability. A [handle](#handle) is a common
example for a capability.

#### **Capability routing** {#capability-routing}

A way for one [component](#component-instance) to give
[capabilities](#capability) to another instance over the
[component instance tree](#component-instance-tree).
[Component manifests](#component-manifest) define how routing takes place, with
syntax for [service capabilities](#service-capability),
[directory capabilities](#directory-capability), and
[storage capabilities](#storage-capability).

Capability routing is a [components v2](#components-v2) concept.

##### expose {#expose}

A [component instance](#component-instance) may use the `expose`
[manifest](#component-manifest) keyword to indicate that it is making a
capability available to its parent to route. Parents may [offer](#offer) a
capability exposed by any of their children to their other children or to their
parent, but they cannot [use](#use) it themselves in order to avoid dependency
cycles.

##### offer {#offer}

A [component instance](#component-instance) may use the `offer`
[manifest](#component-manifest) keyword to route a capability that was
[exposed](#expose) to it to one of its children (other than the child that
exposed it).

##### use {#use}

A [component instance](#component-instance) may use the `use`
[manifest](#component-manifest) keyword to consume a capability that was
[offered](#offer) to it by its parent.

#### **Channel** {#channel}

A channel is an IPC primitive provided by Zircon. It is a bidirectional,
datagram-like transport that can transfer small messages including
[Handles](#handle). [FIDL](#fidl) protocols typically use channels as their
underlying transport.

-   [Channel Overview](/docs/concepts/objects/channel.md)
-   [Update Channel Usage Policy](/docs/contribute/best-practices/update_channel_usage_policy.md)

#### **Component** {#component}

A component is a unit of executable software on Fuchsia. Components support
[capability routing](#capability-routing), software composition, isolation
boundaries, continuity between executions, and introspection.

#### **Component collection** {#component-collection}

A node in the [component instance tree](#component-instance-tree) whose children
are dynamically instantiated rather than statically defined in a
[component manifest](#component-manifest).

Component collection is a [components v2](#components-v2) concept.

#### **Component declaration** {#component-declaration}

A component declaration is a [FIDL](#fidl) table ([fuchsia.sys2.ComponentDecl])
that includes information about a [component](#component)’s runtime
configuration, [capabilities](#capabilities) it [exposes](#expose),
[offers](#offer), and [uses](#use), and [facets](#component-manifest-facet).

Component declaration is a [components v2](#components-v2) concept.

[fuchsia.sys2.ComponentDecl]: /sdk/fidl/fuchsia.sys2/decls/component_decl.fidl

#### **Component Framework** {#component-framework}

An application framework for declaring and managing [components](#component),
consisting of build tools, APIs, conventions, and system services.

-   [Components v1](#components-v1), [Components v2](#components-v2)

#### **Component instance** {#component-instance}

One of possibly many instances of a particular [component](#component) at
runtime. A component instance has its own [environment](#environment) and
[lifecycle](#lifecycle) independent of other instances.

#### **Component instance tree** {#component-instance-tree}

A tree structure that represents the runtime state of parent-child relationships
between [component instances](#component-instance). If instance A launches
instance B then in the tree A will be the parent of B. The component instance
tree is used in [static capability routing](#static-capability-routing) such
that parents can [offer](#offer) capabilities to their children to [use](#use),
and children can [expose](#expose) capabilities for their parents to expose to
their parents or offer to other children.

Component instance tree is a [components v2](#components-v2) concept.

#### **Component Manager** {#component-manager}

A system service which lets [component instances](#component-instance) manage
their children and [routes capabilities](#capability-routing) between them, thus
implementing the [component instance tree](#component-instance-tree). Component
Manager is the system service that implements the
[components v2](#components-v2) runtime.

#### **Component Manifest** {#component-manifest}

In [Components v1](#components-v1), a component manifest is a JSON file with a
`.cmx` extension that contains information about a [component](#component)’s
runtime configuration, services and directories it receives in its
[namespace](#namespace), and [facets](#component-manifest-facet).

In [Components v2](#components-v2), a component manifest is a file with a `.cm`
extension, that encodes a [component declaration](#component-declaration).

-   [Component manifests v2](/docs/concepts/components/component_manifests.md)

#### **Component Manifest Facet** {#component-manifest-facet}

Additional metadata that is carried in a
[component manifest](#component-manifest). This is an extension point to the
[component framework](#component-framework).

#### **Components v1** {#components-v1}

A shorthand for the [Component](#component) Architecture as first implemented on
Fuchsia. Includes a runtime as implemented by [appmgr](#appmgr) and
[sysmgr](#sysmgr), protocols and types as defined in [fuchsia.sys], build-time
tools such as [cmc], and SDK libraries such as [libsys] and [libsvc].

-   [Components v2](#components-v2)

[fuchsia.sys]: /sdk/fidl/fuchsia.sys/
[cmc]: /src/sys/cmc
[libsys]: /sdk/lib/sys
[libsvc]: /sdk/lib/svc

#### **Components v2**  {#components-v2}

A shorthand for the [Component](#component) Architecture in its modern
implementation. Includes a runtime as implemented by
[component_manager](#component-manager), protocols and types as defined in
[fuchsia.sys2], and build-time tools such as [cmc].

-   [Components v1](#components-v1)

[fuchsia.sys2]: /sdk/fidl/fuchsia.sys2/
[cmc]: /src/sys/cmc

#### **Concurrent Device Driver** {#concurrent-device-driver}

A concurrent device driver is a [hardware driver](#hardware-driver) that
supports multiple concurrent operations. This may be, for example, through a
hardware command queue or multiple device channels. From the perspective of the
[core driver](#core-driver), the device has multiple pending operations, each of
which completes or fails independently. If the driven device can internally
parallelize an operation, but can only have one operation outstanding at a time,
it may be better implemented with a
[sequential device driver](#sequential-device-driver).

#### **Core Driver** {#core-driver}

A core driver is a [driver](#driver) that implements the application-facing RPC
interface for a class of drivers (e.g. block drivers, ethernet drivers). It is
hardware-agnostic. It communicates with a [hardware driver](#hardware-driver)
through [banjo](#banjo) to service its requests.

#### **Data directory** {#data-directory}

A private directory within which a [component instance](#component-instance) may
store data local to the device, canonically mapped to /data in the component
instance’s [namespace](#namespace).

#### **DevHost** {#devhost}

A Device Host (`DevHost`) is a process containing one or more device drivers.
They are created by the Device Manager, as needed, to provide isolation between
drivers for stability and security.

#### **DevMgr** {#devmgr}

The Device Manager (DevMgr) is responsible for enumerating, loading, and
managing the life cycle of device drivers, as well as low level system tasks
(providing filesystem servers for the boot filesystem, launching
[AppMgr](#appmgr), and so on).

#### **DDK** {#ddk}

The Driver Development Kit is the documentation, APIs, and ABIs necessary to
build Zircon Device Drivers. Device drivers are implemented as ELF shared
libraries loaded by Zircon's Device Manager.

-   [DDK Overview](/docs/concepts/drivers/overview.md)
-   [DDK includes](/zircon/system/ulib/ddk/include/ddk/)

#### **Directory capability** {#directory-capability}

A [capability](#capability) that permits access to a filesystem directory by
adding it to the [namespace](#namespace) of the
[component instance](#component-instance) that [uses](#use) it. If multiple
[component instances](#component-instance) are offered the same directory
capability then they will have access to the same underlying filesystem
directory.

Directory capability is a [components v2](#components-v2) concept.

-   [Capability routing](#capability-routing)

#### **Driver** {#driver}

A driver is a dynamic shared library which [DevMgr](#devmgr) can load into a
[DevHost](#devhost) and that enables, and controls one or more devices.

-   [Reference](/docs/concepts/drivers/driver-development.md)
-   [Driver Sources](/zircon/system/dev)

#### **Environment** {#environment}

A container for a set of components, which provides a way to manage their
lifecycle and provision services for them. All components in an environment
receive access to (a subset of) the environment's services.

#### **Escher** {#escher}

Graphics library for compositing user interface content. Its design is inspired
by modern real-time and physically based rendering techniques though we
anticipate most of the content it renders to have non-realistic or stylized
qualities suitable for user interfaces.

#### **FAR** {#far}

The Fuchsia Archive Format is a container for files to be used by Zircon and
Fuchsia.

-   [FAR Spec](/docs/concepts/storage/archive_format.md)

#### **FBL** {#fbl}

FBL is the Fuchsia Base Library, which is shared between kernel and userspace.

-   [Zircon C++](/docs/development/languages/c-cpp/cxx.md)

#### **fdio** {#fdio}

`fdio` is the Zircon IO Library. It provides the implementation of posix-style
open(), close(), read(), write(), select(), poll(), etc, against the RemoteIO
RPC protocol. These APIs are return- not-supported stubs in libc, and linking
against libfdio overrides these stubs with functional implementations.

-   [Source](/zircon/system/ulib/fdio/)

#### **FIDL** {#fidl}

The Fuchsia Interface Definition Language (FIDL) is a language for defining
protocols that are typically used over [channels](#channel). FIDL is programming
language agnostic and has bindings for many popular languages, including C, C++,
Dart, Go, and Rust. This approach lets system components written in a variety of
languages interact seamlessly.

-   [FIDL](/docs/development/languages/fidl/)

#### **Flutter** {#flutter}

[Flutter](https://flutter.dev/) is a functional-reactive user interface framework
optimized for Fuchsia and is used by many system components. Flutter also runs
on a variety of other platforms, including Android and iOS. Fuchsia itself does
not require you to use any particular language or user interface framework.

#### **Fuchsia API Surface** {#fuchsia-api-surface}

The Fuchsia API Surface is the combination of the
[Fuchsia System Interface](#fuchsia-system-interface) and the client libraries
included in the [Fuchsia SDK](#fuchsia-sdk).

#### **Fuchsia Package** {#fuchsia-package}

A Fuchsia Package is a unit of software distribution. It is a collection of
files, such as manifests, metadata, zero or more executables (e.g.
[Components](#component)), and assets. Individual Fuchsia Packages can be
identified using [fuchsia-pkg URLs](#fuchsia-pkg-url).

#### **fuchsia-pkg URL** {#fuchsia-pkg-url}

The [fuchsia-pkg URL](/docs/concepts/storage/package_url.md) scheme is a means for referring
to a repository, a package, or a package resource. The syntax is
`fuchsia-pkg://<repo-hostname>[/<pkg-name>][#<path>]]`. E.g., for the component
`echo_client_dart.cmx` published under the package `echo_dart`'s `meta`
directory, from the `fuchsia.com` repository, its URL is
`fuchsia-pkg://fuchsia.com/echo_dart#meta/echo_client_dart.cmx`.

#### **Fuchsia SDK** {#fuchsia-sdk}

The Fuchsia SDK is a collection of libraries and tools that the Fuchsia project
provides to Fuchsia developers. Among other things, the Fuchsia SDK contains a
definition of the [Fuchsia System Interface](#fuchsia-system-interface) as well
as a number of client libraries.

#### **Fuchsia System Interface** {#fuchisa-system-interface}

The [Fuchsia System Interface](/docs/concepts/system/abi/system.md) is the binary
interface that the Fuchsia operating system presents to software it runs. For
example, the entry points into the vDSO as well as all the FIDL protocols are
part of the Fuchsia System Interface.

#### **Fuchsia Volume Manager** {#fuchsia-volume-manager}

Fuchsia Volume Manager (FVM) is a partition manager providing dynamically
allocated groups of blocks known as slices into a virtual block address space.
The FVM partitions provide a block interface enabling filesystems to interact
with it in a manner largely consistent with a regular block device.

-   [Filesystems](/docs/concepts/filesystems/filesystems.md)

#### **GN** {#gn}

GN is a meta-build system which generates build files so that Fuchsia can be
built with [Ninja](#ninja). GN is fast and comes with solid tools to manage and
explore dependencies. GN files, named `BUILD.gn`, are located all over the
repository.

-   [Language and operation](https://gn.googlesource.com/gn/+/master/docs/language.md)
-   [Reference](https://gn.googlesource.com/gn/+/master/docs/reference.md)
-   [Fuchsia build overview](development/build/overview.md)

#### **Handle** {#handle}

A Handle is how a userspace process refers to a [kernel object](#kernel-object).
They can be passed to other processes over [Channels](#channel).

-   [Reference](/docs/concepts/objects/handles.md)

#### **Hardware Driver** {#hardware-driver}

A hardware driver is a [driver](#driver) that controls a device. It receives
requests from its [core driver](#core-driver) and translates them into
hardware-specific operations. Hardware drivers strive to be as thin as possible.
They do not support RPC interfaces, ideally have no local worker threads (though
that is not a strict requirement), and some will have interrupt handling
threads. They may be further classified into
[sequential device drivers](#sequential-device-driver) and
[concurrent device drivers](#concurrent-device-driver).

#### **Hub** {#hub}

The hub is a portal for tools to access detailed structural information about
component instances at runtime, such as their names, job and process ids, and
exposed capabilities.

-   [Hub](/docs/concepts/components/hub.md)

#### **Jiri** {#jiri}

Jiri is a tool for multi-repo development. It is used to checkout the Fuchsia
codebase. It supports various subcommands which makes it easy for developers to
manage their local checkouts.

-   [Reference](https://fuchsia.googlesource.com/jiri/+/master/README.md)
-   [Sub commands](https://fuchsia.googlesource.com/jiri/+/master/README.md#main-commands-are)
-   [Behaviour](https://fuchsia.googlesource.com/jiri/+/master/behaviour.md)
-   [Tips and tricks](https://fuchsia.googlesource.com/jiri/+/master/howdoi.md)

#### **Job** {#job}

A Job is a [kernel object](#kernel-object) that groups a set of related
[processes][#process], their child processes, and their jobs (if any).
Every process in the system belongs to a job and all jobs form a single
rooted tree.

-   [Job Overview](/docs/concepts/objects/job.md)

#### **Kernel Object** {#kernel-object}

A kernel object is a kernel data structure which is used to regulate access to
system resources such as memory, i/o, processor time and access to other
processes. Userspace can only reference kernel objects via [Handles](#handle).

-   [Reference](/docs/concepts/objects/objects.md)

#### **KOID** {#koid}

A Kernel Object Identifier.

-   [Kernel Object](#kernel-object)

#### **Ledger** {#ledger}

[Ledger](/src/ledger/docs/README.md) is a distributed storage system for
Fuchsia. Applications use Ledger either directly or through state
synchronization primitives exposed by the [Modular](/docs/concepts/modular/overview.md) framework that are based on
Ledger under-the-hood.

#### **LK** {#lk}

Little Kernel (LK) is the embedded kernel that formed the core of the Zircon
Kernel. LK is more microcontroller-centric and lacks support for MMUs,
userspace, system calls -- features that Zircon added.

-   [LK on Github](https://github.com/littlekernel/lk)

#### **Module** {#module}

This is a [Modular](/docs/concepts/modular/overview.md) concept that is being deprecated.


A module is a role a [component](#Component) can play to participate in a
[story](#Story). Every component can be be used as a module, but typically a
module is asked to show UI. Additionally, a module can have a `module` metadata
file which describes the Module's data compatibility and semantic role.

-   [Module metadata format](/docs/concepts/modular/module.md)

#### **Musl** {#musl}

Fuchsia's standard C library (libc) is based on Musl Libc.

-   [Source](/zircon/third_party/ulib/musl/)
-   [Musl Homepage](https://www.musl-libc.org/)

#### **Namespace** {#namespace}

A namespace is the composite hierarchy of files, directories, sockets,
[service](#service)s, and other named objects which are offered to components by
their [environment](#environment).

-   [Fuchsia Namespace Spec](/docs/concepts/framework/namespaces.md)

#### **Netstack** {#netstack}

An implementation of TCP, UDP, IP, and related networking protocols for Fuchsia.

#### **Ninja** {#ninja}

Ninja is the build system executing Fuchsia builds. It is a small build system
with a strong emphasis on speed. Unlike other systems, Ninja files are not
supposed to be manually written but should be generated by other systems, such
as [GN](#gn) in Fuchsia.

-   [Manual](https://ninja-build.org/manual.html)
-   [Ninja rules in GN](https://gn.googlesource.com/gn/+/master/docs/reference.md#ninja_rules)
-   [Fuchsia build overview](development/build/overview.md)

#### **Outgoing directory** {#outgoing-directory}

A file system directory where a [component](#component) may [expose](#expose)
capabilities for others to use.

#### **Package** {#package}

Package is an overloaded term. Package may refer to a
[Fuchsia Package](#fuchsia-package) or a [GN build package](#gn).

#### **Paver** {#paver}

A tool in Zircon that installs partition images to internal storage of a device.

-   [Guide for installing Fuchsia with paver](/docs/development/hardware/paving.md).

#### **Platform Source Tree** {#platform-source-tree}

The Platform Source Tree is the open source code hosted on
fuchsia.googlesource.com, which comprises the source code for Fuchsia. A given
Fuchsia system can include additional software from outside the Platform Source
Tree by adding the appropriate [Fuchsia Package](#fuchsia-package).

#### **Process** {#process}

A Process is a [kernel object](#kernel-object) that represents an instance
of a program as a set of instructions which are executed by one or more
[threads](#thread) together with a collection of [capabilities](#capability).
Every process is contained in a [job](#job).

-   [Process Overview](/docs/concepts/objects/process.md)

#### **Protocol** {#protocol}

In [FIDL](#fidl), a protocol groups methods and events to describe how one
process interacts with another.

In [components v1](#components-v1), a component may access a protocol
(called a "service" in v1) from its [environment](#environment) through its
[namespace](#namespace) by naming the protocol in its services whitelist.

In [components v2](#components-v2), a protocol is used and routed to other
components as a [protocol capability](#protocol-capability).

#### **Realm** {#realm}

In [components v1](#components-v1), realm is synonymous to
[environment](#environment).

In [components v2](#components-v2), a realm is a subtree of component instances
in the [component instance tree](#component-instance-tree). It acts as a
container for component instances and capabilities in the subtree.

#### **Runner** {#runner}

A [component](#component) that provides a runtime environment for other components,
e.g. the ELF runner, the Dart AOT runner, the Chromium web runner.

Every component needs a runner in order to launch. Components express their
dependency on a runner in the component's [declaration](#component-declaration).

When the [component framework](#component-framework) starts a component, it first
determines the capabilities that the component should receive, then asks the
component's runner to launch the component. The runner is responsible for creating
any necessary processes, loading executable code, initializing language runtimes,
handing control to the component's entry points, and terminating the component when
requested by the component framework.

-   [ELF runner](/docs/concepts/components/elf_runner.md)
-   [Component runner](/docs/concepts/components/runners.md)

#### **Scenic** {#scenic}

The system compositor. Includes views, input, compositor, and GPU services.

#### **Sequential Device Driver** {#sequential-device-driver}

A sequential device driver is a [hardware driver](#hardware-driver) that will
only service a single request at a time. The [core driver](#core-driver)
synchronizes and serializes all requests.

#### **Service** {#service}

In [FIDL](#fidl), a service groups [protocols](#protocol) to describe how one
process interacts with another.

Services can be used and provided to other components by
[routing](#capability-routing) [service capabilities](#service-capability).

-   [Service overview](/docs/concepts/components/services.md)

#### **Service capability** {#service-capability}

A [capability](#capability) that permits communicating with a
[service](#service) over a [channel](#channel) using a specified [FIDL](#fidl)
service. The server end of the channel is held by the
[component instance](#component-instance) that provides the capability. The
client end of the channel is given to the
[component instance](#component-instance) that [uses](#use) the capability.

-   [Capability routing](#capability-routing)

Service capability is a [components v2](#components-v2) concept.

#### **Protocol capability** {#protocol-capability}

A [capability](#capability) that permits communicating with a
[protocol](#protocol) over a [channel](#channel) using a specified [FIDL](#fidl)
protocol. The server end of the channel is held by the
[component instance](#component-instance) that provides the capability. The
client end of the channel is given to the
[component instance](#component-instance) that [uses](#use) the capability.

-   [Capability routing](#capability-routing)

Protocol capability is a [components v2](#components-v2) concept.

#### **Session** {#session}

This is a [Modular](/docs/concepts/modular/overview.md) concept that is being deprecated.

An interactive session with one or more users. Has a
[session shell](#session-shell), which manages the UI for the session, and zero
or more [stories](#story). A device might have multiple sessions, for example if
users can interact with the device remotely or if the device has multiple
terminals.

#### **Session Shell** {#session-shell}

This is a [Modular](/docs/concepts/modular/overview.md) concept that is being deprecated.

The replaceable set of software functionality that works in conjunction with
devices to create an environment in which people can interact with mods, agents
and suggestions.

#### **Storage capability** {#storage-capability}

A storage capability is a [capability](#capability) that allocates per-component
isolated storage for a designated purpose within a filesystem directory.
Multiple [component instances](#component-instance) may be given the same
storage capability, but underlying directories that are isolated from each other
will be allocated for each individual use. This is different from
[directory capabilities](#directory-capability), where a specific filesystem
directory is routed to a specific component instance.

Isolation is achieved because Fuchsia does not support
[dotdot](/docs/concepts/filesystems/dotdot.md).

There are three types of storage capabilities:

-   *data*: a directory is added to the [namespace](#namespace) of the
    [component instance](#component-instance) that [uses](#use) the capability.
    Acts as a [data directory](#data-directory).
-   *cache*: same as data, but acts as a [cache directory](#cache-directory).
-   *meta*: a directory is allocated to be used by component manager, where it
    will store metadata to enable features like persistent
    [component collections](#component-collection).

Storage capability is a [components v2](#components-v2) concept.

-   [Capability routing](#capability-routing)
-   [Storage capabilities](/docs/concepts/components/capabilities/storage.md)

#### **Story** {#story}

This is a [Modular](/docs/concepts/modular/overview.md) concept that is being deprecated.

A user-facing logical container encapsulating human activity, satisfied by one
or more related modules. Stories allow users to organize activities in ways they
find natural, without developers having to imagine all those ways ahead of time.

#### **Story Shell** {#story-shell}

This is a [Modular](/docs/concepts/modular/overview.md) concept that is being deprecated.

The system responsible for the visual presentation of a story. Includes the
presenter component, plus structure and state information read from each story.

#### **Thread** {#thread}

A Thread is a [kernel object](#kernel-object) that represents a time-shared
CPU execution context. Each thread is contained in a [process](#process).

-   [Thread Overview](/docs/concepts/objects/thread.md)

#### **userboot** {#userboot}

userboot is the first process started by the Zircon kernel. It is loaded from
the kernel image in the same way as the [vDSO](#virtual-dynamic-shared-object),
instead of being loaded from a filesystem. Its primary purpose is to load the
second process, [bootsvc](#bootsvc), from the [bootfs](#bootfs).

-   [Documentation](/docs/concepts/booting/userboot.md)

#### **Virtual Dynamic Shared Object** {#virtual-dynamic-shared-object}

The Virtual Dynamic Shared Object (vDSO) is a Virtual Shared Library -- it is
provided by the [Zircon](#zircon) kernel and does not appear in the filesystem
or a package. It provides the Zircon System Call API/ABI to userspace processes
in the form of an ELF library that's "always there." In the Fuchsia SDK and
[Zircon DDK](#ddk) it exists as `libzircon.so` for the purpose of having
something to pass to the linker representing the vDSO.

#### **Virtual Memory Address Range** {#virtual-memory-address-range}

A Virtual Memory Address Range (VMAR) is a Zircon
[kernel object](#kernel-object) that controls where and how
[Virtual Memory Objects](#virtual-memory-object) may be mapped into the address
space of a process.

-   [VMAR Overview](/docs/concepts/objects/vm_address_region.md)

#### **Virtual Memory Object** {#virtual-memory-object}

A Virtual Memory Object (VMO) is a Zircon [kernel object](#kernel-object) that
represents a collection of pages (or the potential for pages) which may be read,
written, mapped into the address space of a process, or shared with another
process by passing a [Handle](#handle) over a [Channel](#channel).

-   [VMO Overview](/docs/concepts/objects/vm_object.md)

#### **Zircon Boot Image** {#zircon-boot-image}

A Zircon Boot Image (ZBI) contains everything needed during the boot process
before any drivers are working. This includes the kernel image and a
[RAM disk for the boot filesystem](#bootfs).

-   [ZBI header file](/zircon/system/public/zircon/boot/image.h)

#### **Zedboot** {#zedboot}

Zedboot is a recovery image that is used to install and boot a full Fuchsia
system. Zedboot is actually an instance of the Zircon kernel with a minimal set
of drivers and services running used to bootstrap a complete Fuchsia system on a
target device. Upon startup, Zedboot listens on the network for instructions
from a bootserver which may instruct Zedboot to [install](#paver) a new OS. Upon
completing the installation Zedboot will reboot into the newly installed system.

#### **Zircon** {#zircon}

Zircon is the [microkernel](https://en.wikipedia.org/wiki/Microkernel) and
lowest level userspace components (driver runtime environment, core drivers,
libc, etc) at the core of Fuchsia. In a traditional monolithic kernel, many of
the userspace components of Zircon would be part of the kernel itself.

-   [Zircon Documentation](/zircon/README.md)
-   [Zircon Concepts](/docs/concepts/kernel/concepts.md)
-   [Source](/zircon)

#### **ZX** {#zx}

ZX is an abbreviation of "Zircon" used in Zircon C APIs/ABIs
(`zx_channel_create()`, `zx_handle_t`, `ZX_EVENT_SIGNALED`, etc) and libraries
(libzx in particular).

#### **ZXDB** {#zxdb}

The native low-level system debugger.

-   [Reference](/docs/development/debugger/README.md)
