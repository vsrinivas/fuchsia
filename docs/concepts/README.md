# Fuchsia is not Linux
_A modular, capability-based operating system_

This document is a collection of articles describing the Fuchsia operating system,
organized around particular subsystems. Sections will be populated over time.

[TOC]

## Zircon Kernel

Zircon is the microkernel underlying the rest of Fuchsia. Zircon
also provides core drivers and Fuchsia's libc implementation.

 - [Concepts][zircon-concepts]
 - [System Calls][zircon-syscalls]
 - [vDSO (libzircon)][zircon-vdso]

## Zircon Core

 - Device Manager & Device Hosts
 - [Device Driver Model (DDK)][zircon-ddk]
 - [C Library (libc)](/docs/concepts/system/libc.md)
 - [POSIX I/O (libfdio)](/docs/concepts/system/life_of_an_open.md)
 - [Process Creation](/docs/concepts/booting/process_creation.md)

## Framework

 - [Overview][framework-overview]
 - [Core Libraries](/docs/concepts/framework/core_libraries.md)
 - Application model
   - [Interface definition language (FIDL)][FIDL]
   - Services
   - Environments
 - [Boot sequence](/docs/concepts/framework/boot_sequence.md)
 - Device, user, and story runners
 - Components
 - [Namespaces](/docs/concepts/framework/namespaces.md)
 - [Sandboxing](/docs/concepts/framework/sandboxing.md)
 - [Story][framework-story]
 - [Module][framework-module]
 - [Agent][framework-agent]

## Storage

 - [Block devices](/docs/concepts/storage/block_devices.md)
 - [File systems](/docs/concepts/storage/filesystems.md)
 - Directory hierarchy
 - [Ledger][ledger]
 - Document store
 - Application cache

## Networking

 - Ethernet
 - [Wireless](/docs/concepts/networking/wireless_networking.md)
 - [Bluetooth](/docs/concepts/networking/bluetooth_architecture.md)
 - [Telephony][telephony]
 - Sockets
 - HTTP

## Graphics

 - [UI Overview][ui-overview]
 - [Magma (vulkan driver)][magma]
 - [Escher (physically-based renderer)][escher]
 - [Scenic (compositor)][scenic]
 - [Input manager][input-manager]
 - [Flutter (UI toolkit)][flutter]

## Components

 - [Component framework](/docs/concepts/components/README.md)

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

 - POSIX lite (what subset of POSIX we support and why)
 - Web runtime

## Update and recovery

 - [Software Update System][software-update-system]
 - Verified boot
 - Updater

[zircon-concepts]: /docs/concepts/kernel/concepts.md
[zircon-syscalls]: /docs/reference/syscalls/README.md
[zircon-vdso]: /docs/concepts/kernel/vdso.md
[zircon-ddk]: /docs/concepts/drivers/overview.md
[FIDL]: /docs/development/languages/fidl/README.md
[framework-overview]: /docs/concepts/modular/overview.md
[framework-story]: /docs/concepts/modular/story.md
[framework-module]: /docs/concepts/modular/module.md
[framework-agent]: /docs/concepts/modular/agent.md
[ledger]: /src/ledger/docs/README.md
[bluetooth]: /garnet/bin/bluetooth/README.md
[telephony]: /src/connectivity/telephony/
[magma]: /docs/concepts/graphics/magma/README.md
[escher]: /docs/concepts/graphics/escher/README.md
[ui-overview]: /docs/concepts/graphics/scenic/README.md
[scenic]: /docs/concepts/graphics/scenic/scenic.md
[input-manager]: /docs/concepts/graphics/scenic/input.md
[flutter]: https://flutter.dev/
[software-update-system]: /docs/concepts/system/software_update_system.md
