# Fuchsia is not Linux
_A modular, capability-based operating system_

This document is a collection of articles describing the Fuchsia operating system,
organized around particular subsystems. Sections will be populated over time.

[TOC]

## Zircon Kernel

Zircon is the microkernel underlying the rest of Fuchsia. Zircon
also provides core drivers and Fuchsia's libc implementation.

 - [Concepts][zircon-concepts]
 - [System Calls][zircon-syscalls] / VDSO (libzircon)
 - Boot Sequence

## Zircon Core

 - Device Manager & Device Hosts
 - Device Driver Model (DDK)
 - [C Library (libc) & POSIX IO (libfdio)](libc.md)
 - [Process Start / ELF Loading (liblaunchpad)](launchpad.md)

## Framework

 - [Core Libraries](core_libraries.md)
 - Application model
   - Interface description language
   - Services
   - Environments
 - [Boot sequence](boot_sequence.md)
 - Device, user, and story runners
 - Components
 - [Namespaces](namespaces.md)
 - [Sandboxing](sandboxing.md)
 - [Story][framework-story]
 - [Module][framework-module]
 - [Agent][framework-agent]
 - Links

## Storage

 - [Block devices](block_devices.md)
 - [File systems](filesystems.md)
 - Directory hierarchy
 - [Ledger][ledger]
 - Document store
 - Application cache

## Networking

 - Ethernet
 - [Wireless](wireless_networking.md)
 - Bluetooth
 - Sockets
 - HTTP

## Graphics

 - [Magma (vulkan driver)](https://fuchsia.googlesource.com/garnet/+/master/lib/magma/)
 - [Escher (physically-based renderer)](
   https://fuchsia.googlesource.com/garnet/+/master/public/lib/escher/)
 - [Scenic (compositor)](
   https://fuchsia.googlesource.com/garnet/+/master/docs/ui_scenic.md)
 - [Input manager](https://fuchsia.googlesource.com/garnet/+/master/docs/ui_input.md)
 - [View manager](https://fuchsia.googlesource.com/garnet/+/master/bin/ui/view_manager/)
 - [Flutter (toolkit)](https://flutter.io/)

## Media

 - Audio
 - Video
 - DRM

## Intelligence

 - Context
 - Agent Framework
 - Suggestions

## User interface

 - Device, user, and story shells
 - Stories and modules

## Backwards compatibility

 - POSIX lite
 - Web runtime

## Update and recovery

 - Verified boot
 - Updater

[zircon-concepts]: https://fuchsia.googlesource.com/zircon/+/master/docs/concepts.md "Zircon concepts"
[zircon-syscalls]: https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls.md "Zircon syscalls"
[framework-story]: https://fuchsia.googlesource.com/peridot/+/master/docs/modular/story.md "Framework story"
[framework-module]: https://fuchsia.googlesource.com/peridot/+/master/docs/modular/module.md "Framework module"
[framework-agent]: https://fuchsia.googlesource.com/peridot/+/master/docs/modular/agent.md "Framework agent"
[ledger]: https://fuchsia.googlesource.com/peridot/+/master/docs/ledger/README.md
