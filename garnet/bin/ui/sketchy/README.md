# Sketchy Canvas

This package contains implementation for FIDLs in `//garnet/public/fidl/fuchsia.ui.sketchy`. It allows client to draw strokes on a canvas, and generates meshes for `scenic` to render. On one hand, it is a service that handles drawing commands from client. On the other hand, it is client to `scenic` that provides meshes to render.

* `buffer` contains helper classes for different types of buffers.
* `resources` contains resources that are defined in `fidl`.
* `stroke` contains helper classes for mananging strokes.
* `canvas.cc` is the implementation of the canvas `fidl`.
* `main.cc` is the entry point of the service.

## Frame

`Frame` is the abstraction of the state to be presented in a certain timeframe. A new `frame` will be created for a `Canvas::Present()` request at a new timestamp. It contains the command buffer to generate or update mesh buffers, and provides them to `scenic` so that they can be presented.

## Data Flow

![](docs/data-flow.png)

## Buffer

To support dynamic sizing, `capacity` is introduced to describe the total size of the buffer, and `size` is used to describe the actual size that is used. Various buffers are introduced to support different use cases:

* `EscherBuffer` wraps around an `escher::Buffer` and is able to grow dynamically when the content grows. It's used to hold data in `StrokePath` as input to `StrokeTessellator`.
* `SharedBuffer` is shared between `sketchy` and `scenic`: it wraps around an `escher::Buffer` and the corresponding `fuchsia::ui::gfx::Buffer`. It does NOT change size once created.
* `MeshBuffer` wraps around two `ShardBuffer`'s: one for vertex and the other for index. Dynamic sizing is handled here, rather than `SharedBuffer`, to avoid duplicate semantics.

### Mesh Buffer

Each `MeshBuffer` belongs to a `StrokeGroup`. It vendors portions of itself to `Stroke` by `MeshBuffer::Preserve()`. In addition, `MeshBuffer` provides the vertex buffer, index buffer, and the bounding box to `scenic`, so that `scenic` could render the strokes as meshes.

### Multi-buffering

Multi-buffering is required to handle the case where `scenic` has not fully rendered the mesh in the last frame but the new `Canvas::Present()` comes. It's implemented via a `SharedBuffer` pool for efficient buffer usage. Think it in this way: `sketchy` produces `SharedBuffer`'s and `scenic` consumes them. In each frame, `sketchy` returns the previous `SharedBuffer`'s to pool, and grab new ones. The pool monitors the returned `SharedBuffer`'s from `scenic`, and once `scenic` consumes them, they will be recycled for future use.

![](docs/multi-buffering.png)

Notice that we only recycle `SB1` when `SB2` is consumed for the first time. It's safe to recycle `SB1` because `SB2` starts to get used. However, `SB2` is not yet safe to be recycled because `scenic` keeps consuming `SB2` for rendering the following frames (for example, some other `SB` triggers a `ui::Session::Present()` but `SB2` has its content unchanged). In that case, we have no signal when `SB2` is finally consumed until a new `SB3` comes and replaces it.
