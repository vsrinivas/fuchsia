# Retrieve build information

Metrics and error reports are collected from devices in several ways: Cobalt,
feedback reports, crash reports, manual reports from developers and QA.
Interpreting these signals requires knowing where they are generated from to
varying levels of detail. This document describes the places where version
information about the system are stored for use in these types of reports.

Note that this information only applies to the base system; dynamically or
ephemerally added software will not be included here.

## View build information using CLI {#view-build-information-using-cli}

To view the device's build information using a command line, run the following
`ffx` command:

```posix-terminal
ffx target show
```

## Access build information at runtime {#access-build-information-at-runtime}

To access build information at runtime, add the feature `build-info` to the
[component manifest][component-manifest] of the component that needs to read the
following fields:

*   [Product](#product)
*   [Board](#board)
*   [Version](#version)
*   [Latest commit date](#latest-commit-date)
*   [Snapshot](#snapshot)
*   [Kernel version](#kernel-version)

### Product

**Location**: `/config/build-info/product`

This string describes the product configuration used at build time. Ths string
defaults to the value passed as `PRODUCT` in `fx set`. Example:
`products/core.gni` and `products/workstation.gni`.

### Board

**Location**: `/config/build-info/board`

This string describes the board configuration used at build time to specify the
target hardware. This string defaults to the value passed as `BOARD` in `fx
set`. Example: `boards/x64.gni`.

### Version

**Location**: `/config/build-info/version`

This string describes the version of the build. The string defaults to the same
string used currently in `latest-commit-date`, which can be overridden by build
infrastructure to provide a more semantically meaningful version, for example,
to include the release train the build was produced on.

### Latest commit date {#latest-commit-date}

**Location**: `/config/build-info/latest-commit-date`

This string contains a timestamp of the most recent commit to the integration
repository (specifically, the `CommitDate` field) formatted in strict ISO 8601
format in the UTC timezone. Example: `2019-03-28T15:42:20+00:00`.

### Snapshot

**Location**: `/config/build-info/snapshot`

This string contains the Jiri snapshot of the most recent ‘jiri update’.

### Kernel version {#kernel-version}

**Location**: Stored in vDSO. Accessed through
[`zx_system_get_version_string`][zx-system-version-string].

This string contains the Zircon revision computed during the kernel build
process.

<!-- Reference links -->

[component-manifest]: /docs/concepts/components/v1/component_manifests.md
[zx-system-version-string]: /docs/reference/syscalls/system_get_version_string.md
