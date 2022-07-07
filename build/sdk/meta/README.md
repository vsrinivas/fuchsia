# SDK metadata schemas

This directory contains JSON schemas for metadata files added to SDKs.
These schemas are included in SDKs under `//meta/schemas`.

New schemas may only be added if given express permission from
sdk-dev@fuchsia.dev. New schemas must be added to the schema list in
BUILD.gn to ensure future compatibility. Thus, a corresponding
golden file for each new schema needs to be created under `/golden`.

## Versioning

In order to help consumers of the SDK adjust to metadata changes, each SDK
embeds an opaque version number for its metadata schema.
This version number is specified in [`version.gni`](version.gni) and can be
found in SDKs in `//meta/manifest.json`.

Changes to schemas that require some consumer action should be accompanied by an
increment of this version number.
Such changes include:
- adding a new property;
- changing a property from required to optional;
- renaming a property;
- changing a property's type;
- adding a schema for a new SDK element type.

## Testing

Run unit tests with:

```
fx test host_x64/sdk_metadata_lib_test
```
