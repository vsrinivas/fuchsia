# Integrating the Core SDK

Integrating the Core SDK is the process of consuming the Core SDK and turning
it into something usable.

The main entry point for the ingestion process is a file at
`//meta/manifest.json`.
As with every metadata file in the SDK, the manifest follows a JSON schema which
is included under `//meta/schemas/manifest.json`.

This file contains a list of all the elements included in this SDK, represented
by the path to their respective metadata file.
Each element file is guaranteed to contain a top-level `type` attribute, which
may be used to apply different treatments to different element types. For example,
generating a build file for a FIDL library or just moving a host tool to a
convenient location in the final development environment.

The existence of the various metadata files as well as the exhaustiveness of
their contents should make it so that the ingestion process may be fully
automated.
JSON schemas may even be used to generate code representing the metadata
containers and let the ingestion program handle idiomatic data structures
instead of raw JSON representations.

The metadata schemas will evolve over time.
In order to allow consumers of that metadata to adjust to schema changes, the
main metadata file contains a property named `schema_version` which is an opaque
version identifier for these schemas.
This version identifier will be modified every time the metadata schemas evolve
in a way that requires the attention of a developer.
SDK consumers may record the version identifier of the metadata they used to last
ingest an SDK and compare that version identifier to next SDK's version
identifier in order to detect when developer action may be required.


