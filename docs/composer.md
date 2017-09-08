Mozart Composer
=================

The Mozart Composer is a Fuchsia system service which composes, animates,
and renders graphical objects to a display.

The Mozart Composer's responsibilities are:

- Composition: Mozart builds coherent 3D models from trees of graphical
  objects (nodes) which were independently generated and linked together
  by its clients.  Composition makes it possible to seamlessly intermix
  the graphical content of separately implemented UI components.

- Animation: Mozart re-evaluates any time-varying expressions within models
  prior to rendering each frame, thereby enabling animation of model
  properties without further intervention from clients.  Offloading animations
  to Mozart ensures smooth jank-free transitions.

- Rendering: Mozart renders its models to rendering targets (such as displays)
  using Escher, a hardware accelerated graphical renderer which applies
  realistic lighting and shadows to the entire scene.

- Scheduling: Mozart schedules scene updates and animations to anticipate
  and match the rendering target's presentation latency and refresh interval.

- Diagnostics: Mozart provides a diagnostic interface to help developers
  debug their models and measure performance.

# Concepts

## Composers

The `Composer` FIDL interface is Mozart's front door.  Each composer instance
represents an isolated rendering context with its own content, render targets,
and scheduling loop.

Each composer provides the following operations:

- Create client `Sessions` to publish graphical content to this composer.
- Create rendering targets to specify where the output of the composer
  should be rendered, such as to a display or to an image (e.g. screen shots).
- Bind scenes to rendering targets.
- Obtain another Magenta channel which is bound to the same composer
  instance.  (Duplicate)

A single composer instance can update, animate, and render multiple `Scenes`
(trees of graphical objects) to multiple targets in tandem on the same
scheduling loop.  This means that the timing model for a composer instance
is _coherent_: all of its associated content belongs to the same scheduling
domain and can be seamlessly intermixed.

Conversely, independent composer instances cannot share content and are
therefore not coherent amongst themselves.  Creating separate composer
instances can be useful for rendering to targets which have very different
scheduling requirements or for running tests in isolation.

When a composer instance is destroyed, all of its sessions become
inoperable and its rendering ceases.

`Views` typically do not deal with the composer directly; instead
they receive a `Session` from the view manager.  Thus they can contribute
graphical content but they cannot create new rendering targets within their
containing composer.

## Sessions

The `Session` FIDL interface is the primary API used by clients of Mozart
to contribute graphical content to the `Composer` in the form of `Resources`.
Each session instance has its own resource table and is unable to directly
interact with resources belonging to other sessions.

Each session provides the following operations:

- Submit operations to add, remove, or modify resources.
- Commit a sequence of operations to be presented atomically.
- Awaiting and signaling fences.
- Schedule subsequent frame updates.
- Form links with other sessions (by mutual agreement).
- Obtain another Magenta channel which is bound to the same session
  instance.  (Duplicate)

When a session instance is destroyed, all of its resources are released
and all of its links become inoperable.

`Views` typically receive separate sessions from the view manager.

## Resources

`Resources` represent scene elements such as nodes, shapes, materials,
and animations which belong to particular `Sessions`.

Clients of Mozart generate graphical content to be rendered by queuing and
submitting operations to add, remove, or modify resources within their
session.

Each resource is identified within its session by a locally unique id which is
assigned by the owner of the session (by arbitrary means).  Sessions cannot
directly refer to resources which belong to other sessions (even if they
happen to know their id) therefore content embedding between sessions is
performed using `Link` objects as intermediaries.

To add a resource, perform the following steps:

- Enqueue an operation to add a resource of the desired type and assign
  it a locally unique id within the session.
- Enqueue one or more operations to set that resource's properties
  given its id.

Certain more complex resources may reference the ids of other resources
within their own definition.  For instance, a `Node` references its `Shape`
thus the `Shape` must be added before the `Node` so that the node may
reference it as part of its definition.

To modify a resource, enqueue one or more operations to set the desired
properties in the same manner used when the resource was added.

The remove a resource, enqueue an operation to remove the resource.

Removing a resource causes its id to become available for reuse.  However,
the session maintains a reference count for each resource which is internally
referenced.  The underlying storage will not be released (and cannot be
reused) until all remaining references to the resource have been cleared
*and* until the next frame which does not require the resource has been
presented.  This is especially important for `Memory` resources.
See also (Fences)[#fences].

This process of addition, modification, and removal may be repeated
indefinitely to incrementally update resources within a session.

### Nodes

A `Node` resource represents a graphical object which can be assembled into
a hierarchy called a `node tree` for rendering.

TODO: Discuss this in more detail, especially hierarchical modeling concepts
such as per-node transforms, groups, adding and removing children, etc.

### Scenes

A `Scene` resource combines a tree of nodes with the scene-wide parameters
needed to render it.  A composer instance may contain multiple scenes but
each scene must have its own independent tree of nodes.

A scene resource has the following properties:

- The scene's root node.
- The scene's global parameters such as its lighting model.

### TODO: More Resources

Add sections to discuss all other kinds of resources: shapes, materials, links,
memory, images, buffers, animations, variables, etc.

## Renderers

The `Renderer` FIDL interface represents a connection to a rendering target
such as a display or image pipe.

TODO: Describe how this gets created and bound to a particular scene,
including the problem of setting and animating its properties such as the
projection matrix.  May make sense to model part of this as a resource.

## Timing Model

TODO: Talk about scheduling frames, presentation timestamps, etc.

## Fences

TODO: Talk about synchronization.

# API Guide

## TODO

Talk about how to get started using the compositor, running examples,
recommended implementation strategies, etc.
