Entity Types
===
> Status: DRAFT
> 
> This is a stand-in for a future Entity/user-data reference system.

In Fuchsia, data sharing between system and application components is a central
part of enabling interesting and powerful user experiences. To promote
interoperability, Fuchsia includes an `Entity` runtime primitive, which
represents structured data. An `Entity` can represent data of multiple `types`.
A `type` indicates both the semantics of the data as well as its schema.

Entity types and their schemas are published and public to everyone.

## Defining Entity types in a Metadata file
Entity schemas are defined in the `entity_type` metadata file in the `meta/`
directory of a Fuchsia package (TODO link).

The `entity_type` file contains a JSON-encoded list of dictionaries with the
following format (JSON schema [available
here](../src/package_manager/metadata_schemas/entity_type.json)):

```javascript
[
  {
    "name": "FirstType",
    "schema": "path/to/schema.json"
  },
  {
    "name": "SecondType",
    "schema": ...
  },
  ...
]

```

`name` is a string identifier that must be unqiue within this metadata file. Names are constrained to the following letters: `[a-zA-Z0-9_]`.

`schema` is a path to a file within the package that contains a [JSON Schema](http://json-schema.org/).

Any number of `Entity` types can be defined in one `entity_type` file.
