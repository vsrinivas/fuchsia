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

There is an example component that serves a protocol called [ProfileStore][profile-store]:

```fidl
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/diagnostics/workshop/fidl/profile_store.test.fidl" region_tag="profile_store_fidl" adjust_indentation="auto" %}
```

This protocol allows creation, deletion, and inspection of user profiles, which contain a
name and balance. The component has a bug - profile deletion does not work.

## Run the component

In addition to the main component serving ProfileStore, there are a number of components that
connect to and interact with ProfileStore. All components are in the
`fuchsia-pkg://fuchsia.com/profile_store_example` package.

  * `#meta/profile_store.cm` - serves ProfileStore
  * `#meta/add_olive.cm` - Connects to ProfileStore and adds a profile called 'Olive'
  * `#meta/add_balance_olive.cm` - Connects to ProfileStore and adds balance to the 'Olive' profile
  * `#meta/withdraw_balance_olive.cm` - Connects to ProfileStore and withdraws balance from the
  'Olive' profile
  * `#meta/add_jane.cm` - Connects to ProfileStore and adds a profile called 'Jane'
  * `#meta/delete_olive.cm` - Connects to ProfileStore and deletes the 'Olive' profile

Capabilities are routed by the `#meta/laboratory_server.cm` component.

You can interact with the components using the `ffx component` command, and inspect output from
components using `ffx log`.
First, run `ffx log --tags workshop` in a shell. This shell will contain all output from
components. In a different shell, run the toy components:

```bash
# setup server
ffx component create /core/ffx-laboratory:profile_store fuchsia-pkg://fuchsia.com/profile_store_example#meta/laboratory_server.cm

#setup first client
ffx component create /core/ffx-laboratory:profile_store/clients:add_olive fuchsia-pkg://fuchsia.com/profile_store_example#meta/add_olive.cm

ffx component show profile_store # to see the hierarchy

# add a profile key and read it
ffx component start /core/ffx-laboratory:profile_store/clients:add_olive
ffx component create /core/ffx-laboratory:profile_store/clients:reader fuchsia-pkg://fuchsia.com/profile_store_example#meta/profile_reader.cm
ffx component start /core/ffx-laboratory:profile_store/clients:reader

# demonstrate persistence
ffx component stop /core/ffx-laboratory:profile_store/profile_store
ffx component start /core/ffx-laboratory:profile_store/clients:reader

# update balance
ffx component create /core/ffx-laboratory:profile_store/clients:add_balance_olive fuchsia-pkg://fuchsia.com/profile_store_example#meta/add_balance_olive.cm
ffx component start /core/ffx-laboratory:profile_store/clients:add_balance_olive
ffx component start /core/ffx-laboratory:profile_store/clients:reader

# add second profile
ffx component create /core/ffx-laboratory:profile_store/clients:add_jane fuchsia-pkg://fuchsia.com/profile_store_example#meta/add_jane.cm
ffx component start /core/ffx-laboratory:profile_store/clients:add_jane
ffx component start /core/ffx-laboratory:profile_store/clients:reader

# update balance
ffx component create /core/ffx-laboratory:profile_store/clients:withdraw_balance_olive fuchsia-pkg://fuchsia.com/profile_store_example#meta/withdraw_balance_olive.cm
ffx component start /core/ffx-laboratory:profile_store/clients:withdraw_balance_olive
ffx component start /core/ffx-laboratory:profile_store/clients:reader

# delete olive (this will not work as there is a bug in the server code)
ffx component create /core/ffx-laboratory:profile_store/clients:delete_olive fuchsia-pkg://fuchsia.com/profile_store_example#meta/delete_olive.cm
ffx component start /core/ffx-laboratory:profile_store/clients:delete_olive
ffx component start /core/ffx-laboratory:profile_store/clients:reader
```

## Debugging with diganostics

<!-- TODO -->

## Verifying with tests

<!-- TODO -->

[profile-store]: /examples/diagnostics/workshop/fidl/profile_store.test.fidl