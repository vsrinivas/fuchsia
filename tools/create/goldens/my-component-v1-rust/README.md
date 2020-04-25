# my-component-v1-rust

TODO: Brief overview of the component.

## Building

To add this component to your build, append
`--with tools/create/goldens/my-component-v1-rust`
to the `fx set` invocation.

## Running

```
$ fx shell run fuchsia-pkg://fuchsia.com/my-component-v1-rust#meta/my-component-v1-rust.cmx
```

## Testing

Unit tests for my-component-v1-rust are available in the `my-component-v1-rust-tests`
package.

```
$ fx test my-component-v1-rust-tests
```

