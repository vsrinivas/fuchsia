# SDK build tools

This directory contains templates and scripts used to build and consume SDKs in
the Fuchsia GN build.


## Overview

The build output of "an SDK build" is a tarball containing files collected from
the source tree and the output directory, augmented with metadata files.

Metadata includes the nature of an element (e.g. programming language(s),
runtime type), its relationship with other elements (e.g. dependencies,
supporting tools), the context in which the element was constructed (e.g.
target architecture, high-level compilation options), etc...
Schemas for the various types of SDK elements are available under [meta/](meta).


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
- [`sdk_shared_library`](/build/cpp/sdk_shared_library.gni)
- [`sdk_source_set`](/build/cpp/sdk_source_set.gni)
- [`sdk_host_tool`](/build/sdk/sdk_host_tool.gni)

Some language-specific targets are also SDK-ready:
- [`dart_library`](/build/dart/dart_library.gni)
- [`fidl_library`](/build/fidl/fidl_library.gni)
- [`go_binary`](/build/go/go_binary.gni)

In order to add documentation to an SDK, use the
[`sdk_documentation`](sdk_documentation.gni) template.

Static data (e.g. configuration or LICENSE files) being added to the SDK should use the
[`sdk_data`](sdk_data.gni) template.

A target `//foo/bar` declared with one of these templates will yield an
additional target `//foo/bar:bar_sdk` which is an atom ready to be included in
an SDK.

Additionally, the [`sdk`](sdk.gni) template should be used to declare an
SDK.

### Visibility

The `sdk_atom` template declares a `category` parameter which allows developers
to control who may be able to use their atom. See the parameter's documentation
for more information on this.


## Creating a custom SDK

Once elements have been set up for inclusion in an SDK, declaring such an SDK
only takes a few steps:

1. Identify the atoms needed in the SDK;
2. Create a new SDK `//some/place:my_sdk` with the `sdk` template, regrouping
   the atoms and molecules that should be included;
3. Add a new
   [package](/docs/development/idk/documentation/packages.md)
   file for the molecule:
```
{
  "labels": [
    "//some/place:my_sdk"
  ]
}
```

The package file can now be used in a standard Fuchsia build and will produce
the archive at `//out/<build-type>/sdk/archive/my_sdk.tar.gz`.

Note that in order for the archive to be generated, an extra GN argument has to
be passed to GN:
```
build_sdk_archives=true
```

### Using a custom SDK in the build

By setting the `export` property to true on an SDK target, that SDK's contents
become available in the build output directory and may be used for other GN
targets to depend on. This is useful for example when building third-party code
which would otherwise rely on an official SDK.

For an SDK declared at `//some/place:my_sdk` and marked as "exported", an
additional GN target exists: `//some/place:my_sdk_export`.
This target will generate a usable SDK under
`//out/<build-type>/sdk/exported/my_sdk`.


## GN build arguments

##### `build_sdk_archives`

By default, the build system will not produce SDK tarballs as it is a somewhat
time-consuming build step. Set this argument to `true` in order to have tarballs
created under `$OUTPUT_DIR/sdk/archive`.

##### `warn_on_sdk_changes`

For each element in the SDK, a reference file representing its API is checked
into the source tree. If the API is modified but the reference file is not
updated, the build will fail. Set this argument to `true` in order to turn the
errors into mere warnings.
