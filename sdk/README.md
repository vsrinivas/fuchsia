# SDK build tools

This directory contains templates and scripts used to build and consume SDKs in
the Fuchsia GN build.


## Overview

The build output of "an SDK build" is a manifest file describing the various
elements of the SDK, the files that constitute them, as well as metadata.

Metadata includes the nature of an element (e.g. programming language(s),
runtime type), its relationship with other elements (e.g. dependencies,
supporting tools), the context in which the element was constructed (e.g.
target architecture, high-level compilation options), etc...

The packaging of an SDK is a post-build step using this manifest as a blueprint.

A single build can produce multiple SDK manifests.


## Implementation

Individual elements are declared using the [`sdk_atom`](sdk_atom.gni) template.
It should be rare for a developer to directly use that template though: in most
instances its use should be wrapped by another higher-level template, such as
language-based templates.

Groups of atoms are declared with the [`sdk_molecule`](sdk_molecule.gni)
template. A molecule can also depend on other molecules. Molecules are a great
way to provide hierarchy to SDK atoms.


## Declaring SDK elements

There are a few GN templates developers should use to enable the inclusion of
their code in an SDK:
- [`prebuilt_shared_library`](/cpp/prebuilt_shared_library.gni)
- [`sdk_shared_library`](/cpp/sdk_shared_library.gni)
- [`sdk_source_set`](/cpp/sdk_source_set.gni)
- [`sdk_static_library`](/cpp/sdk_static_library.gni)
- [`sdk_executable`](/cpp/sdk_executable.gni)

Some language-specific targets are also SDK-ready:
- [`go_binary`](/go/go_binary.gni)

A target `//foo/bar` declared with one of these templates will yield an
additional target `//foo/bar:bar_sdk` which is an atom ready to be included in
an SDK.

Additionally, the [`sdk`](sdk.gni) template should be used to declare an
SDK.


## Creating a custom SDK

Once elements have been set up for inclusion in an SDK, declaring such an SDK
only takes a few steps:

1. Identify the atoms needed in the SDK;
2. Create a new SDK `//my/api` with the `sdk` template, regrouping the atoms and
   molecules that should be included;
3. Add a new
   [package](https://fuchsia.googlesource.com/docs/+/master/build_packages.md)
   file for the molecule:
```
{
  "labels": [
    "//my/api"
  ]
}
```

The package file can now be used in a standard Fuchsia build and will produce
the manifest at `//out/foobar/gen/my/api/api.sdk`. A JSON schema for this
manifest is available [here](manifest_schema.json).

#### Using a custom SDK in the build

By setting the `export` property to true on an SDK target, that SDK's contents
become available in the build output directory and may be used for other GN
targets to depend on. This is useful for example when building third-party code
which would otherwise rely on an official SDK.

For an SDK declared at `//my/api` and marked as "exported", an additional GN
target exists: `//my/api:api_export`. This target will generate a usable SDK
under `out/<build-type>/sdks/<sdk-target-name>`.

An exported SDK can also be declared as "old school", in which case it will
produce a sysroot with all the libraries in that SDK.
Note that this is a temporary feature which will disappear once the third-party
runtimes that need it have all been updated.
