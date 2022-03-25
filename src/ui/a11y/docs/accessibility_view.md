# Fuchsia Accessibility View

## Overview

The [scenic view][view-ref] is the basic unit of fuchsia graphics. Beyond
rendering graphical content, scenic views also confer several capabilities
of interest to the accessibility manager, namely:

1. Manipulating [view focus][view-focus].
1. [Receiving input][view-input].
1. [Injecting input][view-input].
1. Rendering content (e.g. [screen reader focus highlights][highlights]).
1. Applying transforms to descendants' content (e.g.
   [magnification][magnification]).

As opposed to offering these capabilities to the accessibility manager through
a set of privileged APIs, we prefer instead to vend them via an
accessibility-owned view near the root of the scene.

## Motivation

The accessibility manager and input system can both benefit from an
accessibility-owned view as a close descendant of the scene root. Benefits
include:

1. Simplified [input logic][view-input] in scenic. Without the accessibility
   view, scenic would need to make special accommodations to ensure that the
   accessibility manager receives input events, and has the first opportunity to
   claim them. An accessibility-owned view allows scenic to dispatch input events
   to the accessibility manager via essentially the same mechanism as it does to a
   “normal” view. Moreover, the accessibility view’s position at the top of the
   scene graph implicitly gives it input precedence over other views.

   **NOTE:** This is a desired future state. Currently, accessibility still
   consumes input through a dedicated API.

1. Reduced input latency. Without the accessibility view, the "special
   accommodations" mentioned above would introduce additional latency for all
   incoming input events when assistive technologies are enabled.
1. Fewer privileged APIs. Without the accessibility view, we would need
   dedicated APIs for the accessibility manager to perform magnification, draw
   highlights, receive input, control focus, inject input, etc.

## High-level architecture

The accessibility manager inserts a view into the scene, owned by either root
presenter or scene manager, at startup (exact details of the handshake to insert
the accessibility view are beyond the scope of this document). The accessibility
view is a close descendant of the scene root (see "Scene toplogy" below). This
decision is deliberate, as it allows us to guarantee the following properties:

1. Accessibility receives priority for input. Ancestor views have input
   precedence over their descendants.
1. The entire UI falls under the accessibility view's jurisdiction. View capabilities
   are generally restricted to their scene subgraphs, so this property ensures
   that the accessibility manager can, e.g., manipulate focus and inject input
   into UI views.

## Scene topology

[TODO.][scene-topology]

## GFX vs. Flatland
[TODO.][gfx-flatland]

[view-ref]: /docs/development/graphics/scenic/concepts/view_ref.md
[view-focus]: /docs/development/graphics/scenic/concepts/focus_chain.md
[view-input]: /docs/development/graphics/scenic/concepts/input
[highlights]: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=78639&q=component%3AAccessibility%20docs&can=2
[magnification]: /src/ui/a11y/docs/magnifier.md
[scene-topology]: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=96332
[gfx-flatland]: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=96411
