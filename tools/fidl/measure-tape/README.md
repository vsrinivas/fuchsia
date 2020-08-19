# measure-tape

A measure tape simplifies maximizing the number of elements which can be batched
at once over a FIDL channel communication. More details on this topic are
provided in the guide [max out pagination].

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
  target_binding = "hlcpp"
  target_type = "fuchsia.your.library/TargetType"
  fidls = [ ":fuchsia.your.library" ]
}
```

_This template must be imported with
`import("//tools/fidl/measure-tape/measure_tape.gni")`._

 * `target_binding` key indicates what bindings to generate.
   Valid values are currently "hlcpp" and "rust".

 * `target_type` key indicates the FIDL target type for which to generate a
   measure tape. It must be provided in its fully qualified form, e.g.
   `fuchsia.ui.scenic/Command` or `fuchsia.mem/Buffer`

 * `fidls` key must list all FIDL libraries transitively reachable through the
   target type. For instance, the `fuchsia.ui.scenic/Command` requires
   `:fuchsia.images`, `:fuchsia.ui.gfx`, `:fuchsia.ui.input`,
   `:fuchsia.ui.scenic`, `:fuchsia.ui.views`

## Using a measure tape

When constructing batches, use the measure tape to determine the size of
individual elements, and aggregate this size along with the batch being
constructed for transmission.

The following is a simplified [example][go-example] taking many `Element` and
sending batches of `vector<Element>` over a hypothetical `SendNext` FIDL method.

The example assumes that `Element` contains no handles. If your data contains
handles, be sure to also aggregate the handle count.

```go
func sendInBatches(elements []Element, over YourFavoriteFidlBinding) {
	var (
		batchSize = baseBatchSize
		batch     []Element
	)
	for _, element := range elements {
		size := Measure(element)
		if zxMaxChannelBytes < batchSize+size {
			over.SendNext(batch)
			batch = nil
			batchSize = baseBatchSize
		}
		batch = append(batch, element)
		batchSize += size
	}
	if len(batch) != 0 {
		over.SendNext(batch)
	}
}

// sizeof(fidl_message_header_t) + sizeof(fidl_vector_t)
const baseBatchSize int = 32
```

An [interactive verion of this example][go-example] is hosted on the Go
Playground.

## Contributing

```
fx set core.x64 --with //tools/fidl/measure-tape/src:host
fx build
```

Then

```
fx measure-tape \
    -json out/default/fidling/gen/sdk/fidl/fuchsia.images/fuchsia.images.fidl.json \
    -json out/default/fidling/gen/sdk/fidl/fuchsia.ui.gfx/fuchsia.ui.gfx.fidl.json \
    -json out/default/fidling/gen/sdk/fidl/fuchsia.ui.input/fuchsia.ui.input.fidl.json \
    -json out/default/fidling/gen/sdk/fidl/fuchsia.ui.scenic/fuchsia.ui.scenic.fidl.json \
    -json out/default/fidling/gen/sdk/fidl/fuchsia.ui.views/fuchsia.ui.views.fidl.json \
    -target-type fuchsia.ui.scenic/Command \
    -target-binding hlcpp \
    -h-include-path lib/ui/scenic/cpp/commands_sizing.h \
    -out-h sdk/lib/ui/scenic/cpp/commands_sizing.h \
    -out-cc sdk/lib/ui/scenic/cpp/commands_sizing.cc
```

## Testing

```
fx set core.x64 --with //tools/fidl/measure-tape:tests
fx test measure-tape_test
```

<!-- xrefs -->
[max out pagination]: /docs/development/languages/fidl/guides/max-out-pagination.md
[go-example]: https://play.golang.org/p/KODYMAEg88L
