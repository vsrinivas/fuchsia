# Fuchsia shortcut

## Rendered Docs

* [FIDL](https://fuchsia.dev/reference/fidl/fuchsia.ui.shortcut)
* [Rust](https://fuchsia-docs.firebaseapp.com/rust/fidl_fuchsia_ui_shortcut/index.html)

## Overview

`fuchsia.ui.shortcut` provides information about activation of keyboard
combinations.

At the moment, provides registering and notification APIs for "hotkey" shortcuts
i.e. key + modifiers.

Scenic View hierarchy is used as a consolidated disambiguation mechanism. This
also implies that a component should provide a ViewRef in order to use shortcuts
manager API. Only focused components (i.e. ViewRef is on the Focus chain) are
notified of the shortcut activation.

For the cases of shortcut conflicts (e.g. parent and child components both
register CTRL + C shortcut listeners), the child component take precedence,
unless parent uses special flag when registering for the shortcut
(suppress_overrides = true).

Full disambiguation procedure:
- Only [focused](https://fuchsia.dev/fuchsia-src/concepts/graphics/scenic/focus_chain) components
  participate
- `suppress_overrides = true` resolution step:
  - Only shortcuts with `suppress_overrides = true` participate
  - Components are sorted in top-down order of Scenic View hierarchy, i.e. parent nodes first
  - All components in that order receive the notification until one of them
  returns `handled = true`
  - If handled, stop
- `suppress_overrides = false` resolution step:
  - Only shortcuts with `suppress_overrides = false` participate
  - Components are sorted in lowest-first order of Scenic View hierarchy, i.e. child nodes first
  - All components in that order receive the notification until one of them returns `handled = true`
