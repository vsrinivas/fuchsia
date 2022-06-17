# Scenes

Scenes are a higher level Carnelian abstraction to allow reusable bits of user-interface. They are usable as is, but quite incomplete.

The basic unit of UI is a [facet](https://fuchsia-docs.firebaseapp.com/rust/carnelian/scene/facets/trait.Facet.html). A facet is
reposible for rendering, sizing and message handling.

Facets are collected together in [groups](https://fuchsia-docs.firebaseapp.com/rust/carnelian/scene/group/index.html#). Groups
may also contain other groups.

Groups are responsible for [layout](https://fuchsia-docs.firebaseapp.com/rust/carnelian/scene/layout/index.html) via their
optional [Arranger](https://fuchsia-docs.firebaseapp.com/rust/carnelian/scene/layout/trait.Arranger.html).

## Notes

Ideally, scenes could handle user interaction without requiring boiler plate in view assistant. [This WIP CL](https://fuchsia-review.googlesource.com/c/fuchsia/+/649983) is a start towards
allowing scenes to handle input.

## Concerns

Facet and group IDs can become stale if the scene is rebuilt but the old facet and group IDs are used. Individual IDs are never reused, so such issue will arise as panics when non-existent facet or group IDs are used.
