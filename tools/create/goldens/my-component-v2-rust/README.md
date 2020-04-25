# my-component-v2-rust

TODO: Brief overview of the component.

## Building

To add this component to your build, append
`--with-base tools/create/goldens/my-component-v2-rust`
to the `fx set` invocation.

NOTE: V2 components must be included in `base`. Ephemerality is not supported yet.

## Running

There is no convenient way to run a V2 component directly. First launch `component_manager`
as a V1 component and give it the URL of this component.

```
$ fx shell run fuchsia-pkg://fuchsia.com/component_manager#meta/component_manager.cmx \
  fuchsia-pkg://fuchsia.com/my-component-v2-rust#meta/my-component-v2-rust.cm
```

## Testing

Unit tests for my-component-v2-rust are available in the `my-component-v2-rust-tests`
package.

```
$ fx test my-component-v2-rust-tests
```

