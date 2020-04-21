# measure-tape

A measure tape lets you measure the size which FIDL values will take on the
wire, both in terms of number of bytes and number of handles. The
`measure-tape` tool automatically generates code to create a measure tape for a
specific target type.

For instance, Scenic has an enqueue method which takes a vector of commands.
With this tool, we can generate a measure tape, i.e. code to measure each
command, which the Scenic client can leverage to batch commands appropriately,
and maximize throughput.

## Build integration

To add a measure tape to your project, use the `measure_tape` template:

```gn
measure_tape("measure_tape_for_targettype") {
  target_type = "fuchsia.your.library/TargetType"
  fidls = [ ":fuchsia.your.library" ]
}
```

_This template must be imported with
`import("//tools/fidl/measure-tape/measure-tape.gni")`._

 * `target_type` key indicates the FIDL target type for which to generate a
   measure tape. It must be provided in its fully qualified form, e.g.
   `fuchsia.ui.scenic/Command` or `fuchsia.mem/Buffer`

 * `fidls` key must list all FIDL libraries transitively reachable through the
   target type. For instance, the `fuchsia.ui.scenic/Command` requires
   `:fuchsia.images`, `:fuchsia.ui.gfx`, `:fuchsia.ui.input`,
   `:fuchsia.ui.scenic`, `:fuchsia.ui.views`

## Contributing

    fx set core.x64 --with //tools/fidl/measure-tape:host
    fx build

Then

    fx measure-tape \
        --json out/default/fidling/gen/sdk/fidl/fuchsia.images/fuchsia.images.fidl.json \
        --json out/default/fidling/gen/sdk/fidl/fuchsia.ui.gfx/fuchsia.ui.gfx.fidl.json \
        --json out/default/fidling/gen/sdk/fidl/fuchsia.ui.input/fuchsia.ui.input.fidl.json \
        --json out/default/fidling/gen/sdk/fidl/fuchsia.ui.scenic/fuchsia.ui.scenic.fidl.json \
        --json out/default/fidling/gen/sdk/fidl/fuchsia.ui.views/fuchsia.ui.views.fidl.json \
        --target-type fuchsia.ui.scenic/Command \
        --h-include-path lib/ui/scenic/cpp/commands_sizing.h \
        --out-h sdk/lib/ui/scenic/cpp/commands_sizing.h \
        --out-cc sdk/lib/ui/scenic/cpp/commands_sizing.cc

## Testing

```
fx test measure-tape-tests 
```
