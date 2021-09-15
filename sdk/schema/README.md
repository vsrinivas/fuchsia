# SDK schemata

This directory contains externally published JSON schemata.

The rule of thumb is that if **external** tools need to validate JSON against
the schema (such schemata are typically versioned), the schema belongs in this
directory.

The schema files contained in this directory end up on
[fuchsia.dev][fuchsia-dev].

## Versioning
The versioning rules are simple:
1. Use a randomly generated 8 hex digit suffix in both the schama ID and its
   file name.
1. Create a new copy with a fresh version whenever making an incompatible
   schema change such as adding a new required field.

### Examples
`hardware-f6f47515.json` schema file has an ID of
`"https://fuchsia.dev/schema/product_bundle/hardware-f6f47515.json"`.

## Organization
Each significant effort, such as Modular SDK, should allocate a sub-directory
to house its schemata.

### Common definitions
Keep common definitions private to your projects unless they can actually be
shared across multiple efforts. In that case, add them to
`common-00000000.json`. Please avoid making incompatible changes to this file
as it would lead to either a proliferation of versions or cascading updates to
files depending on common definitions.


[fuchsia-dev]: https://fuchsia.dev/schema