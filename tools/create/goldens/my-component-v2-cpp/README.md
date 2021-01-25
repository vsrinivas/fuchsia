# my-component-v2-cpp

TODO: Brief overview of the component.

## Building

To add this component to your build, append
`--with-base tools/create/goldens/my-component-v2-cpp`
to the `fx set` invocation.

NOTE: V2 components must be included in `base`. Ephemerality is not supported yet.

## Running

There is no convenient way to run a V2 component directly. First launch `component_manager_for_examples`
as a V1 component and give it the URL of this component.

Note, you may need to add this variant of component manager to your build.
If so, append `--with //src/sys/component_manager:component-manager-for-examples` to the `fx set` invocation.

```
$ fx shell run fuchsia-pkg://fuchsia.com/component-manager-for-examples#meta/component_manager_for_examples.cmx \
  fuchsia-pkg://fuchsia.com/my-component-v2-cpp#meta/my-component-v2-cpp.cm
```

## Testing

Unit tests for my-component-v2-cpp are available in the `my-component-v2-cpp-tests`
package.

```
$ fx test my-component-v2-cpp-tests
```

