# Overview

`tiles-session` is a simple graphical session with basic window management functionality.  As graphical components are added to the session, all existing components are resized to fit on-screen together.  TODO(fxbug.dev/78286): not yet

# Usage
*TODO(fxbug.dev/88656): delete temporary steps when no longer necessary.*
- *(temporary)* patch in "Flip flatland flags" CL: Iad5238550484b748be5df0fe5240a4d8b5b7b0fc
- add `//src/session/examples/tiles-session:packages` to your gn build args
- `ffx session launch fuchsia-pkg://fuchsia.com/tiles-session-routing#meta/tiles-session-routing.cm`
- `ffx session add fuchsia-pkg://fuchsia.com/flatland-examples#meta/flatland-view-provider-example.cmx`

## Current limitations
*TODO(fxbug.dev/88656): update the following as features are added, and delete when fully-featured.*
Only one view is supported. Adding an additional view replaces the existing one.

# Use cases:
`tile-session` fills a few roles in the Fuchsia ecosystem:
- educational: explain workings of a simple yet fully-functional session
- testing: reliable basis for integration tests
- development: support development by teams such as Input, Scenic, etc.
  - inspect
  - expose APIs for tests to query various conditions?
    - *TODO(fxbug.dev/88656): ask if this is a valid use case.  Anyone have any ideas?*

# Design 

## Philosophy

There is a design tension between simplicity and featurefulness.  Making this an accessible example requires simplicity.  On the other hand, supporting test/dev use cases requires extra features and hence extra complexity.  We aim for a balance.

As much as possible, the code for test/dev features should be encapsulated such that they don't confuse the reader who is just trying to gains an understanding of how a session works.  The place(s) where this code is plugged in to the basic session should be clearly marked as rabbit holes that the reader may wish to avoid falling into.

## Architecture

The tiles session consists of two components, one of which contains a Rust binary which implements the behavior of the session, and another which integrates a number of non-example components (e.g. [`element_manager`](https://osscs.corp.google.com/fuchsia/fuchsia/+/main:src/session/bin/element_manager/README.md) and [`scene_manager`](https://osscs.corp.google.com/fuchsia/fuchsia/+/main:src/session/bin/scene_manager/README.md)) and establishes capability routing between the session component and these others.
