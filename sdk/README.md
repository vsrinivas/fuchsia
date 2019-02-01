# Fuchsia SDK

This directory contains the source code for the core of the [Fuchsia
SDK](../docs/glossary.md#fuchsia-sdk). The SDK itself is produced as an output
of the build by processing the contents of this directory.  For example, this
directory might contain the source code for a library that is included in the
SDK as a prebuilt shared library.

Software outside of the [Fuchsia Source
Tree](../docs/glossary.md#fuchsia-source-tree) should depend only on the Fuchsia
SDK.

## Governance

The API surface described by the SDK is governed by the
[Fuchsia API Council](../docs/development/api/council.md) and should conform to
the appropriate [API rubrics](../docs/development/api/README.md).
