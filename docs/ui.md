# Garnet UI services

Fuchsia's Garnet layer does not provide a full-fledged end-user UI.  Instead, it provides services that provide a foundation upon which to build secure, performant, multi-process UIs.

Collectively, these services are known as "Mozart".

These services include:

## Scenic, the Fuchsia graphics engine

Scenic ([doc](ui_scenic.md)) provides a retained-mode scene graph that allows graphical objects from multiple processes to be composed and rendered within a unified lighting environment.

## View Manager

The view manager ([doc](ui_view_manager.md)) supports hierarchical embedding of client modules, and is responsible for propagating layout information, dispatching input events, and maintaining a model of the user's focus.

## Input

The input subsystem ([doc](ui_input.md)) is responsible for discovering the available input devices, and allowing clients to register for events from these devices.
