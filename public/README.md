Modular SDK
===========

## Overview

The Modular platform orchestrates applications (called modules) to cooperate in
a synchronized, shared context (called a session).

The shared context is a graph data structure that is built up while modules
execute. Module instances read part of the existing data in the session as
inputs, and create new data in the graph as outputs, according to a
specification called a “recipe.” A recipe is a list of steps, each with a
“verb”, “inputs”, and “outputs.” Each step in the recipe is mapped to a
module in a process called resolution (executed by a process or library
called resolver) when the recipe is executed (by a process called handler)
in a session. By referencing nodes and edges in the session graph, the recipe
implicitly and declaratively defines the structure of the data graph, the
life cycle of the modules, and the data flow between the modules.

## Getting the SDK

### Dart or Flutter

If you're only planning to write Modules in Dart or Flutter, simply add this
to your `pubspec.yaml`:
```yaml
dependencies:
  modular:
      git: https://github.com/domokit/modular_pub.git
```

There's an example consumer repo on the `pub_only` branch at
[domokit/modular_sdk_client](https://github.com/domokit/modular_sdk_client/tree/pub_only).

### Other languages

If you're planning to write Modules in any of the other Mojo supported languages
(C++, Go, Java, JavaScript, Python), then copy in the full
[Modular SDK](https://github.com/domokit/modular/tree/master/public) along with
the [Mojo SDK](https://github.com/domokit/mojo_sdk). Writing Modules is just
like writing any other Mojo applications.

There's an example consumer repo at
[domokit/modular_sdk_client](https://github.com/domokit/modular_sdk_client/tree/master)

## Writing modules

A `Module` is any [Mojo Application](https://github.com/domokit/mojo)
that implements the
[Module](https://github.com/domokit/modular/blob/master/public/interfaces/module.mojom)
interface and provides a `manifest.yaml`. Most Dart or Flutter based Modules can
save some boilerplate
code by instead subclassing
[FlutterModule](https://github.com/domokit/modular/blob/master/dart-packages/modular/lib/flutter_module.dart)
or
[MojoModule](https://github.com/domokit/modular/blob/master/dart-packages/modular/lib/mojo_module.dart).

## Building modules

To build a DART module, edit it's `pubspec.yaml` file and add a dependency to
`modular_tools`. Also, add a top-level option `modular` with the following
fields:
  * `root` Path to the root of your modular checkout.
  * `project_type` Whether this is a flutter project or a dart project(default).
  * `targets` A list of all targets to be build where each target has to specify
    name of the output binary, file containing the main method and any assets
    this module may bundle.

### Lifecycle

Modules receive state by implementing these methods:
  * `OnInitialize` Invoked when an instance of the module is initialized.

  * `OnChange` Invoked when a module's state has changed. This includes
    state in either its inputs or outputs. Inputs can change when other modules
    change their outputs. Outputs can change when another instance of this
    module that runs on another device with the same inputs changes its outputs.

Modules produce state by modifying the `StateGraph` received in `OnChange`
and calling `Push` to persist the change.

### Semantic labels

[Semantic labels](#semantic-labels) are the primary mechanism of loose coupling
and establish matches between the abstract steps listed in a recipe and the
concrete modules provided in manifests. Semantic labels also define the data
flow between the steps in the recipe and the structure in which the resulting
data in the session are organized.

See: [Semantic Label Expression Syntax](https://docs.google.com/document/d/109gDG5TQZVCN1HrBuXAcQoXsomYKOcfFez9oz3tg_y0/edit#heading=h.962kkvg45ptv)

TODO(mesch/tonyg): Move this document here.

### Module manifest

Every Module must describe its behavior by providing a file named
`manifest.yaml` in its root directory. This manifest file is represented in
[YAML](http://yaml.org), which is a human readable data serialization format.

At the top level are a series of fields:
  * `verb` Required. The verb this module implements, expressed as URL.
  * `url` Required. The URL from which to load the executable for the module.
  * `use` Optional. A use section with abbreviations used in the expressions
    in the subsequent sections. (The URL in the verb field is not abbreviated.)
  * `input` Optional. A list of expressions describing the inputs of the module.
  * `output` Optional. A list of expressions describing the outputs of the
    module.

TODO(mesch/tonyg): Move [details](https://docs.google.com/document/d/109gDG5TQZVCN1HrBuXAcQoXsomYKOcfFez9oz3tg_y0/edit#heading=h.dsfb221fpebr) here.

## Examples

See the
[//examples/modules](https://github.com/domokit/modular/tree/master/examples/modules)
directory for examples.

## Writing Recipes

A `Recipe` is a composition of `Module`s which collaborate to accomplish
a task. Like [module manifests](#module-manifest), Recipes are represented in
[YAML](http://yaml.org).

Each Recipe consists of these top level fields:
  * `recipe` Required. A list of “steps” each including a verb that make up
    this recipe. The steps are not executed in order, but define a data flow
    graph by connecting their outputs and inputs.
  * `title` Optional. A human readable short description of the recipe, to be
    used to tag log messages while the recipe is executing, and possibly for
    identification in the UI. In the latter case, which at this point is
    hypothetical, the title must be made amenable to translation.
  * `verb`, `input`, `output` Optional. A recipe can be resolved from a verb
    in another recipe, instead of a module. For that to happen, it must
    declare a verb, and possibly inputs and outputs like a module does in its
    manifest. The meaning of these fields is the same as in a module manifest.
  * `use` Optional. The use section for this document, described above. (The
    URL in the verb field is not abbreviated.)

Each item in the `recipe` list consists of one or more of the following fields:
  * `url` Optional. If specified, the Module at this URL is used. Its
    manifest must match all other specified fields. If omitted,
    modular's Resolver will choose a Module that meets the other requirements.
  * `verb` Optional. The single semantic verb this Module must perform.
  * `input` Optional. A list of the [concept names](#concept-names)
    this Module must accept as input.
  * `output` Optional. A list of the [concept names](#concept-names)
    this Module must produce as output.

TODO(mesch/tonyg): Move [details](https://docs.google.com/document/d/109gDG5TQZVCN1HrBuXAcQoXsomYKOcfFez9oz3tg_y0/edit#heading=h.2mwqz6heo3ei) here.

### Examples

See the
[//examples/recipes](https://github.com/domokit/modular/tree/master/examples/recipes)
directory for examples.
