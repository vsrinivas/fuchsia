# SDK build templates

This directory contains templates and scripts used to build and consume SDKs in
the Fuchsia GN build.

This document describes a new SDK design that is intended to replace the
existing one described in README.md. It is a work in progress and may change
frequently until fully implemented.

## Overview

The Fuchsia SDK is a collection of modules, each serving a specific purpose and
possibly depending on other modules. For example, the "host_tools_linux_x64"
module contains binaries that allow a Linux x86-64 host to interact with a
Fuchsia device.

Each SDK module contains several SDK elements. An element is the unit of
compilation and distribution, i.e. it is the smallest buildable unit that can be
processed by SDK-compatible tooling. Each element usually corresponds to an
individual non-SDK build target, such as a library or binary. Continuing the
example above, each binary in the host tool module comes from a single SDK
element.

## Output
The output of an SDK build is metadata files that describe the contents of the
modules and elements and can be used to package them for distribution.

Element metadata includes the contents of the element, its stability category
(see the "Stability categories" section below), its dependencies on other
elements, the SDK version it was built for, and element type-specific
information such as target platform.

Module metadata is similar to element metadata, except the "contents" of a
module are elements and modules rather than files.

Schemas for the various types of SDK metadata are available under [meta/](meta)
and [//sdk/schema/](/sdk/schema).

### Packaging and distribution
The SDK has a canonical layout described by the elements' metadata that SDK
distributions should follow. However, note that neither SDK elements nor modules
produce self-contained, distributable artifacts such as tarballs. In fact, the
contents of an element may be scattered throughout the GN output directory.

Instead, the metadata output from the SDK build includes all the information
needed for tools and infrastructure to package modules and elements. This
greatly reduces the amount of work done during the SDK build process. It also
allows modules to overlap with one another without creating duplicate artifacts.

## Declaring SDK elements and modules

SDK elements are declared using specializations of the
[`sdk_element`](sdk_element.gni) template, such as
[`host_tool_sdk_element`](host_tool_sdk_element.gni), which then construct the
required `sdk_element`. Developers should rarely instantiate the `sdk_element`
template directly.

SDK modules are declared using the [`sdk_module`](sdk_module.gni) template,
similar to the GN `group` target type, and may include SDK elements and other
SDK modules.

### Stability categories

SDK element templates declares a `category` parameter which allows developers
to control who may be able to use their element. Possible values, from most
restrictive to least restrictive:
- `excluded`: The element may not be included in the SDK.
- `experimental`: The element is available with no quality or stability
guarantees.
- `internal`: The element is available within the Fuchsia tree only.
- `cts`: The element may be used in the Fuchsia Compatibility Test Suite.
- `partner`: The element may be used by select partners.
- `public`: The element may be used by anyone.

### API files

Many SDK elements support build-time API change detection by allowing developers
to declare which files make up their element's API and then comparing the hashes
of those files to canonical hashes listed in a file. The file containing the
canonical hashes is known as the API file.

API files are JSON files that contain a map of filenames to MD5 hashes. They are
checked into the Fuchsia tree, usually next to the BUILD.gn file defining their
corresponding SDK element. When the element is built, the newly-built files are
hashed and compared to the API file. Differences result in a build error.

This system can be used, for example, to prevent accidental changes to C++
header files. All elements that are part of a published SDK module should have
an API file.

Details on how to use API files are included in the SDK element build templates.
