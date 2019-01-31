# JSON validator

The present tool verifies JSON files against schemas defined with
[JSON Schema][json_schema].

## Usage

```
json_validator <path to schema> <path to file to verify> [path to stamp file]
```

The third argument represents the path to a stamp file which gets updated when
the tool runs successfully. This argument is mostly designed for integration
with GN actions.

## Schema references

The schema passed to the tool may refer to other schema files. The tool will be
able to locate schema references for schemas specified as paths relative to the
folder containing the main schema.

For example, if the tool is given a schema `/path/to/main_schema.json` and that
schema contains a reference to `file:another/sub_schema.json#/definitions/foo`,
the tool will look for the second schema at `path/to/another/sub_schema.json`.

Paths to schemas may be specified as `file:another/sub_schema.json` or more
simply `another/sub_schema.json`.


[json_schema]: http://json-schema.org/
