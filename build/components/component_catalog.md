# Component Catalog HOWTO

This document explains how to use the Component Catalog to answer some questions about components
in the build graph.

## Producing reports

This guide assumes that you already configured your `args.gn`, such as using an `fx set` command,
for instance `fx set core.x64`.

To regenerate all component catalog reports, use `fx gen`.

All reports have the filename suffix `.component_catalog.json`. To find all reports, you can
use for instance:

```posix-terminal
find $(fx get-build-dir) -name "*.component_catalog.json"
```

The directory structure generally follows the target names. For instance a component target
`//foo/bar:qux` will emit `$(fx get-build-dir)/obj/foo/bar/qux_component_catalog.json`.

The Component Catalog is presented in human-readable JSON for some definition of human and readable.

To clean stale metadata reports (such as from a previous build), you may use `fx clean`.

## What information is present in the Component Catalog?

* General identifying information about the component.
* Information about programming languages used in compilation for code that is
  associated with the component.
* Information about SDK elements used by the component.

## Sample output

A report might look as follows:

```json
[
  {
    "label": "//sdk/lib/syslog/cpp:backend(//build/toolchain/fuchsia:x64)",
    "sdk_id": "sdk://pkg/syslog_cpp_backend"
  },
  {
    "has_cxx": true,
    "label": "//src/developer/debug/debug_agent:bin(//build/toolchain/fuchsia:x64)"
  },
  {
    "has_cxx": true,
    "label": "//src/developer/debug/debug_agent:launcher(//build/toolchain/fuchsia:x64)"
  },
  {
    "component_manifest_path": "meta/debug_agent.cml",
    "label": "//src/developer/debug/debug_agent:debug_agent-component_manifest_compile(//build/toolchain/fuchsia:x64)"
  }
]
```

Each clause tells you something about some aspect of the component.
Clauses have a label associated with them for troubleshooting reasons.
This indicates where the metadata was defined.
Clauses with `has_*` keys indicate some additive boolean aspect of the component.
For instance in the example above we see two clauses that indicate that this component has C++ code.
Clauses with `sdk_id` indicate an SDK atom that is used by this component.

## Troubleshooting

Use `fx gn meta` for more information on how metadata is generated.
For instance:

```posix-terminal
fx gn meta $(fx get-build-dir) sdk/lib/input_report_reader:input_report_test --data=component_catalog
```

This should print the same information as what's presented in the generated file,
but additionally list all the targets that had metadata collected from.

## Known issues

1. Indicating which languages are used in a component could produce confusing results.
* For most components you will see exactly one example used.
* For some components you will see multiple. For instance a component written in Rust with dependencies
  on C++ code will indicate both Rust and C++ are used.
* For some components you will see no languages at all. It's possible to construct a build graph this way,
  if for instance the component's binary is a dependnecy of the package target but not a dependency of the
  component target itself. It's technically wrong but it works.

2. The order of clauses within a JSON report file is unstable.

## Converting to csv

Use `component_catalog.py` to harvest all component catalog files in the tree and produce a single
table in CSV format.
