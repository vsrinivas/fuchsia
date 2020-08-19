# Fuchsia IDK

This directory contains the source code for the core of the [Fuchsia
Fuchsia Integrator Development Kit (IDK)](../docs/glossary.md#fuchsia-idk),
formerlly called the Fuchsia SDK. The IDK is produced as an output
of the build by processing the contents of this directory. For example, this
directory might contain the source code for a library that is included in the
IDK as a prebuilt shared library.

Software outside of the [Platform Source
Tree](../docs/glossary.md#platform-source-tree) should depend only on the Fuchsia
IDK.

> [Learn more](../docs/development/sdk/)

## Categories

Not all the interfaces defined in this directory are part of every Fuchsia IDK.
Instead, interfaces have a `category` label that determines whether the
interface can be included in a given SDK. For example, interfaces with the
`internal` category are available only within the
[Platform Source Tree](../docs/glossary.md#platform-source-tree).
Interfaces with the `partner` category are additionally available to partner
projects. See [sdk_atom.gni](../build/sdk/sdk_atom.gni) for more details.

## Governance

The API surface described by the IDK is governed by the
[Fuchsia API Council](../docs/concepts/api/council.md) and should conform to
the appropriate [API rubrics](../docs/concepts/api/README.md).
