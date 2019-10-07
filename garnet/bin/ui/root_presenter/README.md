# Root Presenter

This directory contains the Root Presenter, a service which manages input device lifecycle,
lower-level input event dispatch, creation of the root of the global scene graph, and connection of
root-level Views by clients such as Sys UI.

This collection of code has a lot of complexity, and you may find various workarounds that exist for
very specific purposes.  Additionally, the code and comments make assumptions (both explicit and
implicit) that may no longer hold.  Please don't make plans and patches based on what you see in the
repository!  Instead, reach out to jaeheon@ or dworsham@ to coordinate your intended work.

## USAGE

This program should not be run directly. See the `present_view` tool.

