# scene_management

Reviewed on: 2021-01-21

`scene_management` is a library for creating and modifying a Scenic scene graph
in a session. For more information, see
[Scenic, the Fuchsia graphics engine](/docs/concepts/graphics/scenic/scenic.md).

## Building
To add `scene_management` to your build, append
`--with //src/ui/lib/scene_management` to the `fx set` invocation.

## Using
`scene_management` can be used by depending on the
`//src/ui/lib/scene_management` GN target.

`scene_management` is not available in the SDK.

## Testing
Unit tests for `scene_management` are available in the`scene_management_tests`
package.

```
$ fx test scene_management_tests
```

## Source layout
The main implementation is linked in `src/lib.rs`.
