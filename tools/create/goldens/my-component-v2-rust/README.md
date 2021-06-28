# my-component-v2-rust

TODO: Brief overview of the component.

## Building

To add this component to your build, append
`--with-base tools/create/goldens/my-component-v2-rust`
to the `fx set` invocation.

## Running

Use `ffx component run` to launch this component into a restricted realm
for development purposes:

```
$ ffx component run fuchsia-pkg://fuchsia.com/my-component-v2-rust#meta/my-component-v2-rust.cm
```

## Testing

Unit tests for my-component-v2-rust are available in the `my-component-v2-rust-tests`
package.

```
$ fx test my-component-v2-rust-tests
```

