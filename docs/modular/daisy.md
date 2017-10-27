Daisies Everywhere
===
> Status: DRAFT

A [`Daisy`](../services/story/daisy.fidl) is a runtime structure for describing
a composable action in Fuchsia. `Daisies` are produced by 3rd-party
code and platform components.

Ultimately, the action a `Daisy` describes will be carried out by a
[`Module`](module.md). A process called [Module
Resolution](module_resolution.md) finds compatible `Modules` capable of executing the
requested action.

## Overview

Daisies have two roles:

1. Enumerating constraints on the world of all Modules to filter it down to
   just those that can successfully execute the described action.
2. Provide a set of initial data to pass to the Module once instantiated.

The first (1) is employed by [Module Resolution](module_resolution.md) to
search the ecosystem of Modules. The second (2) is employed by the Framework to
seed values in the `Link` that is provided to the running Module.

> TODO(thatguy): Add documentation about the process of instantiating a Module whose 
> specification started off as a Daisy.

## What makes a Daisy

For the baremetal details, see the [FIDL struct
definition](../../public/lib/story/fidl/daisy.fidl).

The basic Daisy captures either or both of a **verb** and **nouns**:

* a **verb** is an identifier that references a defined [verb
  template](manifests/verb_template.md)
* the **nouns** are runtime arguments to be passed to the resolved Module

Nouns in a Daisy can be supplied as different runtime types. Modules, on the
other hand, accept Fuchsia [`Entities`](entity.md) as input. If any of the
supplied nouns is *not* already an `Entity`, the process of [Module
Resolution](module_resolution.md) will attempt to translate the data given into
a structured `Entity`.

Since verb templates are expressed independently of the type of data they
operate on, the Daisy creator does not need to know what type of data is
represented in each noun. In fact, each noun can thus capture ambiguity, which
is in turn captured in the Daisy.

## Creating Daisies

### in Dart

> TODO(thatguy): The code below doesn't actually exist yet. :(

```dart
import "peridot/lib/daisy.dart"

// Assume we have a PDF Entity in |thisPdf|.
daisy = new Daisy("com.google.fuchsia.preview.v1", thisPdf);

// Want to present the user with any action we can do to |thisPdf|?
daisy = new Daisy.justNouns(thisPdf);

// A more complex action: Navigate. Let's use text instead of Entities, and
// rely on Entity translation for ensuring the arguments end up in a format
// the target Module can understand.
daisy = new Daisy("com.google.fuchsia.navigate.v1",
                  start: "home", destination: "Googleplex");
// Or using a Context signal for the start:
daisy = new Daisy("com.google.fuchsia.navigate.v1",
                  start: contextReader.select(entity: {"topic": "myLocation"}),
                  destination: "1234 Main St");
```

### in C++

TODO

## Using Daisies

TODO
