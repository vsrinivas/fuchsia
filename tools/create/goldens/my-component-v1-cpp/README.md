# my-component-v1-cpp

TODO: Brief overview of the component.

## Building

To add this component to your build, append
`--with tools/create/goldens/my-component-v1-cpp`
to the `fx set` invocation.

## Running

```
$ fx shell run fuchsia-pkg://fuchsia.com/my-component-v1-cpp#meta/my-component-v1-cpp.cmx
```

## Testing

Unit tests for my-component-v1-cpp are available in the `my-component-v1-cpp-tests`
package.

```
$ fx test my-component-v1-cpp-tests
```

