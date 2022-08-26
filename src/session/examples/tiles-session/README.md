# Overview

`tiles-session` is a simple graphical session with basic window management functionality.

## Usage

### Build with `tiles-session`

Make sure `//src/session/examples/tiles-session` to your gn build args, e.g:

```
fx set <product>.<board> --with //src/session/examples/tiles-session
```

### Launch `tiles-session`

```
ffx session launch fuchsia-pkg://fuchsia.com/tiles-session#meta/tiles-session.cm
```

### Add your view

```
ffx session add fuchsia-pkg://fuchsia.com/flatland-examples#meta/flatland-view-provider-example.cmx
```

## Current limitations
*TODO(fxbug.dev/88656): update the following as features are added, and delete when fully-featured.*
Only one view is supported. Adding an additional view replaces the existing one.

# Use cases

`tile-session` fills a few roles in the Fuchsia ecosystem:

- educational: explain workings of a simple yet fully-functional session
- testing: reliable basis for integration tests
- development: support development by teams such as Input, Scenic, etc.
  - inspect
  - expose APIs for tests to query various conditions

# Design

## Philosophy

There is a design tension between simplicity and featurefulness. Making this an accessible example
requires simplicity. On the other hand, supporting test/dev use cases requires extra features and
hence extra complexity. We aim for a balance.

As much as possible, the code for test/dev features should be encapsulated such that they don't
confuse the reader who is just trying to gains an understanding of how a session works. The
place(s) where this code is plugged in to the basic session should be clearly marked as rabbit
holes that the reader may wish to avoid falling into.
