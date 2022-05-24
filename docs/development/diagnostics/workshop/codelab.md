# Diagnostics and testing codelab

This document contains the codelab for debugging with diagnostics and tests.
It is currently a work in progress.

## Prerequisites

Set up your development environment.

This codelab assumes you have completed [Getting Started](/docs/get-started/README.md) and have:

1. A checked out and built Fuchsia tree.
2. A device or emulator (`ffx emu`) that runs Fuchsia.
3. A workstation to serve components (`fx serve`) to your Fuchsia device or emulator.

To build and run the examples in this codelab, add the following arguments
to your `fx set` invocation:

Note: Replace core.x64 with your product and board configuration.

```
fx set core.x64 \
--with //examples/diagnostics/workshop \
--with //examples/diagnostics/workshop:tests
```

## Introduction

There is an example component that serves a protocol called [ProfileStore][fidl-reverser]:

```fidl
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/diagnostics/workshop/fidl/profile_store.test.fidl" region_tag="profile_store_fidl" adjust_indentation="auto" %}
```

This protocol allows creation, deletion, and inspection of user profiles, which contain a
name and balance. The component has a bug - profile deletion does not work.

### Run the component

<!-- TODO -->

## Debugging with diganostics

<!-- TODO -->

## Verifying with tests

<!-- TODO -->

[profile-store]: /examples/diagnostics/workshop/fidl/profile_store.test.fidl