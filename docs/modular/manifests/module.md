Module Metadata File
===
> Status: DRAFT

A Module metadata file defines the runtime capabilities of a single `Module`
(**TODO: link**). A Module expresses its capabilities by providing one or more
implementations for `verb`s (**TODO**: link to verb/verb template doc).

The file is part of a Fuchsia package (**TODO: link**), is named `module` and
placed within the `meta/` of the package.

The `module` file contents are a JSON-encoded array of dictionaries (JSON
schema [available here](../src/package_manager/metadata_schemas/module.json)).
Each element in the array defines a single verb implementation.

## Example `module` metadata file
The following sample `module` file describes a `Module` that implements a number of `verb`s.

```javascript
[
  {
    "binary": "bin/myPersonPreviewer",
    "local_name": "previewPerson",
    "verb": {
      "package": "https://fuchsia.io/package/coreVerbs",
      "name": "Preview"
    },
    "noun_constraints": [
      {
        "name": "entityToPreview",
        "types": [
          {
            "package": "https://fuchsia.io/package/coreTypes",
            "name": "Person"
          },
          {
            "package": "https://fuchsia.instagram.com/types",
            "name": "Friend"
          }
        ]
      }
    ]
  },
  {
    "binary": "bin/myContactPicker",
    "local_name": "pickContact",
    "verb": {
      "package": "https://fuchsia.io/package/coreVerbs",
      "name": "Pick"
    },
    "noun_constraints": [
      {
        "name": "source",
        "types": [
          {
            "package": "https://fuchsia.instagram.com/types",
            "name": "FriendRepository"
          }
        ]
      },
      {
        "name": "picked",
        "type": {
          "package": "https://fuchsia.instagram.com/types",
          "name": "Friend"
        }
      }
    ]
  }
]

```

## Detailed description

Let's break down the example above and detail the properties in each list element:

### Attributes that define behavior

#### binary

```javascript
"binary": "bin/myPreviewer",
```

Specifies the relative path from the root of the package where the Module executable file
can be found. Different verb implementations within the same package can share
a single `binary`.

#### local_name

```javascript
"local_name": "previewPerson",
```

The `local_name` attribute must be unique within this `module` metadata file.
It is used to inform the `Module` at runtime which verb implementation specified in the 
`module` file is being invoked.

The `local_name` is passed to the Module in its `Module::Initialize()` FIDL
call. (**TODO(thatguy)**: create and document this in FIDL file)

#### verb

```javascript
"verb": {
  "package": "https://fuchsia.io/package/coreVerbs",
  "name": "Preview"
},
```

The `verb` attribute identifies a verb defined in another package. The fully
qualified verb name consists of two parts:

1. `package`
2. `name`

The `package` is the unique ID of a Fuchsia package (**TODO:** link). It
identifies where the verb template associated with the `name` is defined.

The `name` must match a `verb` name in the referenced package's
[`meta/verb_template`](verb_template.md) file. 

#### noun constraints

```javascript
"noun_constraints": [
  {
    "name": "entityToPreview",
    "types": [
      {
        "package": "https://fuchsia.io/package/coreTypes",
        "name": "Person"
      },
      {
        "package": "https://fuchsia.instagram.com/types",
        "name": "Friend"
      }
    ]
  },
  ...
],
```

In the [verb template](verb_template.md), the nouns are given names but not
assigned concrete Entity types (**TODO** link). Here, we constrain a single
verb implementation to operate only on a specific set of Entity (**TODO** link)
types.

Each noun in the verb template gets an entry in `noun_constraints`. Each entry
is made up of the following fields:

* `name`: this is the name of the noun given in the [verb template](verb_template.md)
* `types`: a list of Entity types (each defined by an [entity type](entity_type_.md)). 
   Multiple entries in this list indicate that any of the listed types are compatible. A 
   type entry is made up of:
     - `package`: the Fuchsia package ID where the [Entity type](entity_type.md) is defined.
     - `name`: the name of the Entity type found in `package`'s `meta/entity_type.md` file.

At runtime, this `Module` will communicate with its parent (the invoker of the `Module`) through a `Link` interface (**TODO** link). The `Link` enforces the typing described here, making any attempt to write an `Entity` with an incompatible type an error. This applies for all values of `direction` (`input`, `output` and `input/output`) on the nouns.

#### outgoing services

TODO

### Attributes that define display constraints

TODO
