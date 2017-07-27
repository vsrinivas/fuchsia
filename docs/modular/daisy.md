Daisies Everywhere
===

A [`Daisy`](../services/story/daisy.fidl) is a runtime structure for describing
an abstract action in Fuchsia. The primary producers of a `Daisy` are 3rd-party
code and some platform components.

Ultimately, the action a `Daisy` describes will be carried out by a [`Module`](module.md). It is through a process called [Module Resolution](module_resolution.md) that compatible `Modules` are discovered.

## What makes a Daisy

For the baremetal details, see the [FIDL struct definition](../services/story/daisy.fidl).

The basic Daisy captures either or both of a **verb** and **nouns**:

* a **verb** is an identifier that references a defined [verb template](manifests/verb_template.md)
* the **nouns** are runtime arguments to be passed to the resolved Module

Nouns in a Daisy can be supplied as different runtime types. Modules, on the other hand, accept Fuchsia [`Entities`](entity.md) as input. If any of the supplied nouns is *not* already an `Entity`, the process of [Module Resolution](module_resolution.md) will attempt to translate the data given into a structured `Entity`.

Since verb templates are expressed independently of the type of data they operate on, the Daisy creator does not need to know what type of data is represented in each noun. In fact, each noun can thus capture ambiguity, which is in turn captured in the Daisy.

## Creating Daisies

### in Dart

```dart
import "apps/modular/lib/daisy.dart"

// Assume we have a PDF Entity in |thisPdf|.
daisy = new Daisy("coreVerbs#Preview", thisPdf);

// Want to present the user with any action we can do to |thisPdf|?
daisy = new Daisy.justNouns(thisPdf);

// A more complex action: Navigate. Let's use text instead of Entities, and
// rely on Entity translation for ensuring the arguments end up in a format
// the target Module can understand.
daisy = new Daisy("coreVerbs#Navigate",
                  start: "home", destination: "Googleplex");
// Or using a Context signal for the start:
daisy = new Daisy("coreVerbs#Navigate",
                  start: contextReader.select(entity: {"topic": "myLocation"}),
                  destination: "1234 Main St");
```

### in C++

TODO

## Using Daisies

TODO