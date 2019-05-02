# Download the Fuchsia SDK

You can download the Fuchsia SDK using the links below. Please be aware that
Fuchsia is under active development and its API surface is subject to frequent
changes. The Fuchsia SDK is produced continuously as Fuchsia is developed.

Because the [Fuchsia System Interface](../abi/system.md) is changing, you will
need to run software built using a particular version of the SDK on a Fuchsia
system with a matching version. The [Core SDK](#core) contains a matching system
image appropriate for running in [Qemu](#qemu).

## Core

The Core SDK is a version of the SDK that is build system agnostic. The Core SDK
contains metadata that can be used by an [SDK backend](README.md#backend) to
generate an SDK for a specific build system.

* Linux: https://chrome-infra-packages.appspot.com/p/fuchsia/sdk/core/linux-amd64/+/latest
* MacOS: https://chrome-infra-packages.appspot.com/p/fuchsia/sdk/core/mac-amd64/+/latest

## Qemu

A distribution of [Qemu](https://www.qemu.org/) that has been tested to work
with Fuchsia system images contained in the SDK.

* Linux (amd64): https://chrome-infra-packages.appspot.com/p/fuchsia/qemu/linux-amd64/+/latest
* Linux (arm64): https://chrome-infra-packages.appspot.com/p/fuchsia/qemu/linux-arm64/+/latest
* MacOS (amd64): https://chrome-infra-packages.appspot.com/p/fuchsia/qemu/mac-amd64/+/latest
