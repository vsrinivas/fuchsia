# Overview

Fuchsia is an open-source operating system designed from the ground up for
security and updatability.

## Fuchsia is…

### Fuchsia is designed for security and privacy

Security and privacy are woven deeply into the architecture of Fuchsia.  The
basic building blocks of Fuchsia, the kernel primitives, are exposed to
applications as object-capabilities, which means that applications running on
Fuchsia have no ambient authority: applications can interact only with the
objects to which they have been granted access explicitly.

Software is delivered in hermetic packages and everything is sandboxed, which
means all software that runs on the system, including applications and system
components, receives the least privilege it needs to perform its job and gains
access only to the information it needs to know.

### Fuchsia is designed to be updatable 

Fuchsia works by combining components delivered in packages.  Fuchsia packages
are designed to be updated independently or even delivered ephemerally, which
means packages are designed to come and go from the device as needed and the
software is always up-to-date, like a Web page.

Fuchsia aims to provide drivers with a binary-stable interface. In the future,
drivers compiled for one version of Fuchsia will continue to work in future
versions of Fuchsia without needing to be modified or even recompiled.  This
approach means that Fuchsia devices will be able to update to newer versions of
Fuchsia seamlessly while keeping their existing drivers.

### Fuchsia is designed to be language and runtime agnostic

Fuchsia currently supports a variety of languages and runtimes, including C++,
Rust, Flutter, and Web.  Fuchsia is designed to let developers bring their own
runtime, which means a developer can use a variety of languages or runtimes
without needing to change Fuchsia itself.

Applications interact with each other and the system using message passing,
which means any application that can format messages appropriately can
participate fully in the system regardless of its language or runtime.  Fuchsia
is defined by these protocols, much like the Internet is defined by its
protocols rather than a particular client or server implementation.

### Fuchsia is designed for performance

Fuchsia makes heavy use of asynchronous communication, which reduces latency by
letting the sender proceed without waiting for the receiver.  Fuchsia optimizes
memory use by avoiding garbage collection in the core operating system, which
helps to minimize memory requirements to achieve equivalent performance.

### Fuchsia is open source

Fuchsia is built in the open using BSD/MIT-style open source licenses.
Fuchsia has an inclusive community that welcomes high-quality, well-tested
contributions from everyone.

## Fuchsia is not…

### Fuchsia is not based on Linux

Fuchsia does not use the Linux kernel.  Instead, Fuchsia has its own kernel,
Zircon, which evolved from LittleKernel.  Fuchsia implements some, but not all,
of the POSIX specification as a library on top of the underlying kernel
primitives, which focus on secure message passing and memory management.  Many
core system services, such as file systems and networking, run outside the
kernel in least-privilege, need-to-know sandboxes.

### Fuchsia is not a microkernel

Although Fuchsia applies many of the concepts popularized by microkernels,
Fuchsia does not strive for minimality.  For example, Fuchsia has over 170
syscalls, which is vastly more than a typical microkernel.  Instead of
minimality, the system architecture is guided by practical concerns about
security, privacy, and performance.  As a result, Fuchsia has a pragmatic,
message-passing kernel.

### Fuchsia is not a user experience

Fuchsia is not tied to a specific end-user experience.  Instead, Fuchsia is
general purpose and contains the building blocks necessary for creating a wide
variety of high-quality user experiences.

Fuchsia does have a developer experience, which lets developers write software
for Fuchsia via SDKs and tools.

### Fuchsia is not a science experiment

Fuchsia's goal is to power production devices and products used for
business-critical applications.  As such, Fuchsia is not a playground for
experimental operating system concepts.  Instead, the platform roadmap is driven
by practical use cases arising from partner and product needs.