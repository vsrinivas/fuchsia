Entity Types
===
> Status: DRAFT
> 
> This is a stand-in for a future schematic metadata system for Entities. For now, 
> it is used to specify schemas for schematic content in the cases where that
> is relevant to clients creating Entities.

In Fuchsia, data sharing between system and application components is a central
part of enabling interesting and powerful user experiences. To promote
interoperability, Fuchsia includes an [`Entity`](../entity.md) runtime primitive, which
represents structured data. An `Entity` can represent data of multiple `types`.
A `type` indicates both the semantics of the data as access patterns.

Entity types and, if relevant, their schemas for schematic data, are published and 
public to everyone.

## Defining Entity types in a JSON file

> TODO(thatguy): Add information about where these schemas are discoverable.

Entity types and schemas are defined in file with a JSON-encoded list of dictionaries with the following format (JSON schema [available
here](../src/package_manager/metadata_schemas/entity_type.json)):

```javascript
[
  {
    "name": "https://types.fuchsia.io/FirstType",
    "schema": "path/to/schema.json"
  },
  {
    "name": "https://types.fuchsia.io/SecondType",
    "schema": ...
  },
  ...
]
```

`name` is a string identifier that must be unqiue within this metadata
file.

`schema` is a path to a file within the package that contains a [JSON
Schema](http://json-schema.org/).

Any number of `Entity` types can be defined in one file.
