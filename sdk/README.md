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


## Tools

Individual elements are declared using the [`sdk_atom`](sdk_atom.gni) template.
It should be rare for a developer to directly use that template though: in most
instances its use should be wrapped by another higher-level template, such as
language-based templates. For example, for a target `//foo/bar` the
[`dart_package`](../dart/dart_package.gni) template will automatically generate
a `//foo/bar:bar_sdk` target which is the SDK representation for the Dart
package.

Groups of atoms are declared with the [`sdk_molecule`](sdk_molecule.gni)
template. A molecule can also depend on other molecules. Molecules are a great
way to provide hierarchy to SDK atoms.


## Creating a custom SDK

1. Identify the atoms needed in the SDK;
2. Create a new molecule `//my/api` regrouping the atoms and molecules that
   should be included;
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
the manifest at `//out/foobar/gen/my/api/api.sdk`

Note that atoms do not automatically pull their transitive dependencies into an
SDK build: they need to be manually added. This allows the system to produce
"partial" SDKs.
