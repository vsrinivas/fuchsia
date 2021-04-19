# Starnix glossary

This document explains the terminology we use around POSIX compatibility in Fuchsia.

## Bionic

*Bionic* is the name of the
[C library used on Android](https://android.googlesource.com/platform/bionic/).
We use an Android-based [Linux distribution](#linux-distribution) to test
[starnix](#starnix), which means we often run [Linux binaries](#linux-binary)
that dynamically link against bionic.

## Child address space

The *child address space* is a range of memory addresses that are specific to
each [child processes](#child-process). When running in
[restricted mode](#restricted-mode), a thread in a child process has read/write
accesss to the memory mapped to these addresses.

The rest of the userspace addresses in each child process are part of the
[Starnix addres space](#starnix-address-space).

## Child process

A *child process* is a process running with [starnix](#starnix) as its
[supervisor](#supervisor). The child process runs in an environment that matches
the [Linux UAPI](#linux-uapi). These processes can run binaries that have been
compiled for Linux rather than Fuchsia.

## File descriptor

A *file descriptor* is a numerical value that a [child process](#child-process)
can use to refer to a file object. The mapping between file descriptors and
file objects is stored in the [file descriptor table](#file-descriptor-table)
for the child process.

## File descriptor table

A *file descriptor table* is a mapping between [file descriptors](#file-descriptor)
and file objects. The [Starnix runner](#starnix-runner) maintains a separate
file descritptor table for each [child process](#child-process).

## Futex

A *futex* is an object that lets the caller wait for a condition to occur
at a given memory address. Futexes are often used as primitive building blocks
for synchronization primitives, such as mutexes and condition variables.

Different operating systems have different futex semantics. In the context of
[Starnix](#starnix), we need to be careful to distinguish between
[Zircon](#zircon) futex semantics and Linux futex semantics, which we refer to
as a [lutex](#lutex).

The performance of the system overall is quite sensitive to the performance of
futexes.

## Handle

A *handle* is a numerical value that a Fuchsia process can use to refer to a
[Zircon](#zircon) kernel object. The mapping between handles and Zircon kernel
objects is stored in the [handle table](#handle-table) for the Fuchsia process.

[Child processes](#child-process) cannot interact directly with handles.
Instead, they need to issue Linux [system calls](#system-call) to instruct the
[starnix runner](#starnix-runner) to interact with handles on their behalf.

## Handle table

A *handle table* is a mapping between [handles](#handle) and [Zircon](#zircon)
kernel objects. The Zircon kernel maintains a separate handle table for each
Fuchsia process.

## gVisor

*gVisor* is an implementation of the [Linux UAPI](#linux-uapi) in Go. See
[https://github.com/google/gvisor](https://github.com/google/gvisor).

## Linux binary

A *Linux binary* is binary program that was compiled targeting the
[Linux UAPI](#linux-uapi). For example, the binary might have been compiled with
`clang` using the `--target=aarch64-linux-android21` flag. These binaries do
not run directly on Fuchsia because they expect an initial memory layout and
[system call](#system-call) semantics that match Linux rather than Fuchsia.
Instead, we can use [Starnix](#starnix) to run these binaries because Starnix
provides the runtime environment that these binaries expect.

## Linux distribution

A *Linux distribution* is one or more disk images that contain
[Linux binaries](#linux-binary) and associated configuration information. For
example, a typical distribution will contain a `/bin` directory with
executables, a `/lib` directory with shared libraries, and an `/etc` directory
with configuration information.

Although [Starnix](#starnix) can run standalone Linux binaries, most Linux
binaries are designed to run in the context of a Linux distribution. For this
reason, Starnix often runs Linux binaries with a root file system that has
mounted a Linux distribution.

## Linux UAPI

The *Linux UAPI* is the application programing interface that the Linux kernel
exposes to userspace programs. [Linux binaries](#linux-binary) expect to run in
an environment that matches the semantices defined by the Linux UAPI.

There are many implementations of the Linux UAPI. The most common
implementation is provided by the Linux kernel itself (or one of its forks),
but there are other implementations, such as [gVisor](#gvisor).

## Lutex

A *lutex* is a [futex](#futex) that has the semantics defined by the
[Linux UAPI](linux-uapi). These semantics differ substantially from the
semantics of Fuchsia futexes and will likely require a separate implementation.

## Normal mode

*Normal mode* is an execution mode for a thread that receives
[system calls](#system-call) from when the thread is executing in
[restricted mode](#restricted-mode). When running in normal mode,
the thread is executing code from the [starnix runner](#starnix-runner), has
read/write access to the [starnix address space](#starnix-address-space), and
can interact directly with [handles](#handle).

Normal mode is currently in the design phase.

## POSIX Lite

*POSIX Lite* is an implementation of many POSIX interfaces on Fuchsia. POSIX
Lite differs from [Starnix](#starnix) because programs that use POSIX Lite need
to be compiled targetting the
[Fuchsia System Interface](/docs/concepts/system/abi/system.md) whereas
programs that use [Starnix](#starnix) need to be compiled targetting the
[Linux UAPI](#linux-uapi).

Additionally, POSIX Lite is a subset of the POSIX interface that can be
implemented while respecting the [capability](/docs/glossary.md#capability)
security model of the Fuchsia system whereas Starnix is a more complete
implementation of POSIX that can run precompiled [Linux binaries](#linux-binary)
without modification.

## Restricted mode

*Restricted mode* is a execution mode for a thread that causes
[system calls](#system-call) issued by the thread to be routed to the
[normal mode](#normal-mode) rather than handled by the [Zircon](#zircon)
kernel itself. When running in restricted mode, the thread is executing code
from the [Linux binary](#linux-binary), has no access to the
[starnix address space](#starnix-address-space), and cannot interact directly with
[handles](#handle).

Restricted mode is currently in the design phase.

## Starnix

*Starnix* is an environment for running precompiled [Linux binaries](#linux-binary)
on Fuchsia. Starnix aims to implement the [Linux UAPI](#linux-uapi) in
sufficient detail to be able to run a large number of Linux binaries without
modification.

## Starnix address space

The *starnix address space* is a range of memory addresses that have a
consistent mapping to physical memory across [child processes](#child-process).
These shared mappings let the [starnix runner](#starnix-runner) implement
[system calls](#system-call) from child processes using data structures that
are shared between child processes.

For example, suppose a child process calls `open()` on `/proc/14/fd/7`. The
shared Starnix address space lets the starnix runner examine the
[file descriptor table](#file-descriptor-table) for process 14 and create
a new entry in the file descriptor table for the current process that refers
to the same file object as file descriptor 7 for process 14.

## Starnix manager

The *starnix manager* is a Fuchsia component that provides scaffolding for
developing starnix. For example, starnix manager provides a `playground`
collection that developers can use to run and interact with basic components,
such as `hello_starnix` and `sh`.

As starnix matures, we might remove starnix manager entirely or replace it
with a dedicated developer component.

## Starnix runner

The *starnix runner* is a Fuchsia component framework (CFv2) runner that can
run [Linux binaries](#linux-binary). Specifically, the starnix runner implements
the `fuchsia.component.runner.ComponentRunner` protocol. The starnix runner
is analogous to the ELF Runner, which runs ELF binaries compiled for Fuchsia.

To run a Linux binary, the starnix runner creates a new
[child process](#child-process) and loads the binary into the process. If
that process calls `fork()`, starnix will create another child process to
back the newly forked process.

## Starnix test runner

The *starnix test runner* is a [test runner](/src/sys/test_runners/README.md)
that adapts [Linux binaries](#linux-binary) to the `fuchsia.test.Suite`
interface, making it possible to run Linux binaries as Fuchsia tests. The
*starnix test runner* uses a dedicated instance of the
[starnix runner](#starnix-runner) in order to make these tests hermetic.

## System call

A *system call* (also called a *syscall*) is a mechanism that lets a userspace
process transfer control of a thread to the kernel. Typically, kernels provide
userspace a variety of functionality that userspace can select among using a
*system call number*. The semantics of system calls vary by operating system.

For [Starnix](#starnix), we need to distinguish between Linux and
[Zircon](#zircon) system calls. When a [Linux binary](#linux-binary) issues a
system call, the program expects the system call to have the semantics defined
by the [Linux UAPI](#linux-uapi). When a Fuchsia program issues a system call,
the program expects the system call to have the semantics defined by Zircon.

To distinguish these cases, a given thread in a [child process](#child-process)
is either running in [restricted mode](#restricted-mode) or
[supervisor mode](#supervisor-mode). When running in restricted mode, system
calls have Linux UAPI semantics and are handled by the
[starnix runner](#starnix-runner). When running in supervisor mode, system calls
are handled by Zircon.

## Vector state

The *vector state* of the CPU is the state of the *vector registers* of the
CPU, which contain the operands for vector instructions. Typically, the vector
state of a CPU is quite extensive compared to the *integer state* of the CPU,
which are the registers that contain the operands for normal arithmetic and
logical instructions.

## Zircon

*Zircon* is the kernel used by Fuchsia. Zircon is responsible for implementing
the [system calls](#system-call) on a Fuchsia system. See the
[Zircon entry in the main glossary](/docs/glossary.md#zircon) for more
information.
