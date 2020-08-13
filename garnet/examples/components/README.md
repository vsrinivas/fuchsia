# Hello World Components

This directory contains simple components to show how components are built
and run in the system.

## Building

To add these components to your build append `--with //garnet/examples:examples`
to your `fx set` command. Eg:
```
fx set core.x64 --with //garnet/examples:examples
```

After that do a full build.

(Disclaimer: if these build rules become out of date please check the
[Build documentation](/docs/development/build/fx.md)
and update this readme!)

## Running

To run the component:
```
fx shell run fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello_world.cmx
```

You can run the second binary by changing the manifest:
```
fx shell run fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello_world2.cmx
```

Make sure you have `fx serve` running in another terminal so your component can
be installed!

## Testing

To run the tests for the component:
```
fx run-test component_hello_world_tests
```
