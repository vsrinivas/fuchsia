# Module Facet

Modules declare their run-time capabilities (e.g. which Intent actions they
handle) in the module facet of their [component manifest][component-manifest]
(i.e. their `.cmx` file).

## What is a facet?

Component facets are sections in the component manifest which aren't consumed
directly by the component manager, but are left to be defined and consumed by
other parts of Fuchsia. The Modular framework defines a `fuchsia.module` facet
which module authors use to define module specific properties.

This document describes how to declare a module facet in your component
manifest.

## Example

The following is an excerpt of a component manifest that defines a module facet.

```
{
   "facets": {
      "fuchsia.module": {
         "@version": 2,
         "suggestion_headline": "See details about person",
         "intent_filters": [
            {
               "action": "com.google.fuchsia.preview.v1",
               "parameters": [
                  {
                     "name": "entityToPreview",
                     "type": "https://fuchsia.com/types/Friend"
                  }
               ]
            }
         ],
         "composition_pattern": "ticker"
      }
   }
}
```

This module can be launched using an Intent with the action
`com.google.fuchsia.preview.v1` action, and a `entityToPreview` parameter of
type `https://fuchsia.com/types/Friend`.

## Module Facet fields

The module facet is defined under the `fuchsia.module` facet in a component
manifest. See [example](#example).

*   `@version` **unsigned integer** *(required)*
    -   Describes the version of the module facet. The fields below indicate
        which minimum `@version` they require.
    -   **example**: `2`
*   `composition_pattern`: **string** *(optional)*
    -   **minimum `@version`**: 1
    -   **possible values:**
    *   `ticker`: Show the module at the bottom of the screen underneath another
        module.
    *   `comments-right`: show the module to the right of other modules.
    -   **example**: `"ticker"`
    -   Specifies the compositional pattern that will be used by the story shell
        to display this module along-side other modules in the story. For
        example, the ticker pattern gives a signal to the story shell that the
        module should be placed below another module that it may share a link
        with.
*   `suggestion_headline`: **string** *(optional)*
    -   **minimum `@version`**: 2
    -   **possible values**: UTF-8 string
    -   **example**: `"See details about this person"`
    -   A human-readable string that may be used when suggesting this Module.
*   `placeholder_color`: **string** *(optional)*
    -   **minimum `@version`**: 2
    -   **possible values**: hex color code, leading with a hashtag (`#`)
    -   **example**: `"#ff00ff"`
    -   Defines the color of the placeholder widget used while the module loads.
*   `intent_filters`: **IntentFilter[]** *(optional)*
    -   **minimum `@version`**: 2
    -   **possible values**: JSON list of [IntentFilter](#IntentFilter)
    -   **example**: See [example](#example).
    -   A list of different Intent types this Module is able to handle. An
        action dictates a semantic function this Module implements, as well as
        the role of each of its parameters. When resolving an intent to a
        module, the intent resolver uses an index of these intent filter lists
        to determine which modules can handle an intent.

### IntentFilter

`IntentFilter` is a JSON object used to describe an intent type that a module is
able to handle. The following describes the fields of the IntentFilter JSON
object:

*   `action`: **string** *(required)*
    -   **minimum `@version`**: 2
    -   **possible values**: ASCII string
    -   The action this module is able to handle.
*   `parameters`: **ParameterConstraint[]** *(required)*
    -   **minimum `@version`**: 2
    -   **possible values**: JSON List of
        [ParameterConstraint](#ParameterConstraint)
    -   Describes the names and types of the parameters required to execute the
        specified action. Parameters are typically passed in as Entities.

### ParameterConstraint

`ParameterConstraint` describes a particular intent parameter's name, and it's
acceptable type.

*   `name`: **string** *(required)*
    -   **minimum `@version`**: 2
*   `type`: **string** *(required)*
    -   **minimum `@version`**: 2
    -   Type that is valid for this parameter.

See [example](#example).

[component-manifest]: /docs/concepts/storage/component_manifest.md
