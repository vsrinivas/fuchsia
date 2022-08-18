# escher-examples

These examples show off how to build an application using Escher, the Vulkan rendering engine
powering Scenic.

## How to run the examples

Add the following args to your `fx set`:

```
fx set core.x64 --with //src/ui/examples/escher --args='core_realm_shards += [ "//src/ui/examples/escher:escher_examples_shard" ]'
```

Build the product, start the emulator/device and then run the following commands:

```
ffx component create /core/escher-examples:waterfall fuchsia-pkg://fuchsia.com/escher_waterfall#meta/escher_waterfall.cm
ffx component create /core/escher-examples:rainfall fuchsia-pkg://fuchsia.com/escher_rainfall#meta/escher_rainfall.cm
```

Now start one of the examples:

```
ffx component start /core/escher-examples:waterfall
```

```
ffx component start /core/escher-examples:rainfall
```
