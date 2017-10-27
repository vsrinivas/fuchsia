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

The following sample `module` file describes a `Module` that implements a number
of `verb`s.

```javascript
[
  {
    "binary": "bin/myPersonPreviewer",
    "local_name": "previewPerson",
    "verb": "com.google.fuchsia.preview.v1",
    "noun_constraints": [
      {
        "name": "entityToPreview",
        "types": [
          "https://fuchsia.io/package/coreTypes/Person",
          "https://fuchsia.instagram.com/types/Friend"
        ]
      }
    ]
  },
  {
    "binary": "bin/myContactPicker",
    "local_name": "pickContact",
    "verb": "com.google.fuchsia.pick.v1",
    "noun_constraints": [
      {
        "name": "source",
        "types": [ "https://fuchsia.instagram.com/types/FriendRepository" ]
      },
      {
        "name": "picked",
        "types": [ "https://fuchsia.instagram.com/types/Friend" ]
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

Specifies the relative path from the root of the package where the Module
executable file can be found. Different verb implementations within the same
package can share a single `binary`.

#### local_name

```javascript
"local_name": "previewPerson",
```

The `local_name` attribute must be unique within this `module` metadata file.
It is used to inform the `Module` at runtime which verb implementation specified
in the `module` file is being invoked.

The `local_name` is passed to the Module in its `Module::Initialize()` FIDL
call. (**TODO(thatguy)**: create and document this in FIDL file)

#### verb

```javascript
"verb": "com.google.fuchsia.preview.v1",
```
> NOTE: The exactly format of the verb attribute is likely to evolve.

The `verb` attribute identifies which verb this Module implements. This dictates the semantic function of this Module, as well as the role of each noun.

The `verb` must match a `verb` name in an associated
[`meta/verb_template`](verb_template.md) file.

> TODO(thatguy): Add information about where verbs are discoverable.

#### noun constraints

```javascript
"noun_constraints": [
  {
    "name": "entityToPreview",
    "types": [
      "https://fuchsia.io/package/coreTypes/Person",
      "https://fuchsia.instagram.com/types/Friend"
    ]
  },
  ...
],
```

In the [verb template](verb_template.md), the nouns are given names but not
assigned concrete Entity types (**TODO** link). Here, we constrain this
verb implementation to operate on a specific set of [Entity](../entity.md)
types.

Each noun in the verb template gets an entry in `noun_constraints`. Each entry
is made up of the following fields:

* `name`: this is the name of the noun given in the [verb template](verb_template.md)
* `types`: a list of [Entity](../entity.md) types.
   Multiple entries in this list indicate that any of the listed types are compatible.
   
   > TODO(thatguy): Add information about where entity types & schemas are discoverable.
   
   > TODO(thatguy): The semantics of `types` doesn't make a lot of sense for *output*
     or maybe *input/output* nouns.

At runtime, this `Module` will communicate with its parent (the invoker of the
`Module`) through a [`Link`](../../services/story/link.fidl) interface. The `Link` enforces the
typing described here, making any attempt to write an `Entity` with an
incompatible type an error. This applies for all values of `direction` (`input`,
`output` and `input/output`) on the nouns as specified in the *verb*'s corresponding
[verb_template](verb_template.md) file.

#### outgoing services

TODO

### Attributes that define display constraints

TODO
