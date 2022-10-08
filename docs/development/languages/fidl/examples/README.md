# FIDL examples

This is a catalog of FIDL examples intended to demonstrate FIDL concepts through
simplified implementations of real software workflows.

## Example index

The following examples sequentially demonstrate useful FIDL concepts.

### Key-value store

The [key-value store][example-key-value-store] example demonstrates how to build
a simple key-value store using FIDL in order to learn about the various data
types available in the language.

### Canvas

The [canvas][example-canvas] example demonstrates how to build a simple 2D
line-rendering canvas using FIDL in order to learn about commonly used data flow
patterns.

## Concept index

Each "concept" in the FIDL language is exemplified in at least one of the
examples listed in the preceding section. A quick reference of each such
concept, as well as its example implementations, is listed in the following
section.

### Data types

#### Alias

<<../concepts/_alias.md>>

#### Enum

<<../concepts/_enum.md>>

#### Named method payload

<<../concepts/_named_payload.md>>

[example-canvas]: canvas/README.md
[example-key-value-store]: key-value-store/README.md
