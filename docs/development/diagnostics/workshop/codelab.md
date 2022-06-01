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

Diagnostics provides multiple products that help component authors debug their components both
while developing and in the field.

For this workshop we'll be exploring three core technologies:

- [Structured logging](#structured-logging)
- [Inspect](#inspect)
- [Triage](#triage)

### Structured logging

Diagnostics provides structured logging libraries to allow components to write logs.
To help find the bug, we'll be adding a few logs to the profile store component.

The first step when adding logging to a component, is to include the logging library
in your binary dependencies. To do this, update your [BUILD.gn][profile-store-build] as follows:

```
source_set("lib") {
  ...
  public_deps = [
    ...
    "//sdk/lib/syslog/cpp",
  ]
}
```

Logging is initialized the moment we call one of the logging macros. However, the libraries provide
some utilities that should be called in `main()` such as configuring the tags (if desired only, this
is optional).

Tags can be useful to later query the logs of a group of components. For our purposes we can add
the `workshop` tag:

```
#include <lib/syslog/cpp/log_settings.h>
...
syslog::SetTags({"workshop", "profile_store_server"});
```

Now, it's time to write some logs. We'll be using the `FX_SLOG` macro which allows to write
structured keys and values.

For example, we can add the following log when we get a request on `ProfileStore::Open` but
the profile file doesn't exist:

```
#include <lib/syslog/cpp/macros.h>
...
FX_SLOG(WARNING, "Profile doesn't exist", KV("key", key.c_str()));
```

Try adding that log, build (`fx build`), relaunch your component (`ffx component start ...`) and
then run: `ffx log --tags workshop`.

What other logs could we add that would help identify the log? Please experiment!

A solution can be found in this patch: https://fuchsia-review.googlesource.com/c/fuchsia/+/684632

### Inspect

Inspect allows components to expose state about themselves. Unlike logs, which are a stream,
inspect represents a live view into the component current state.

Reading through the [Inspect quickstart][inspect-quickstart] would be a good first step. If you'd
like to dive deeper into Inspect, you can also follow the [Inspect codelab][inspect-codelab].

Once those introductory documents have been read, what intrumentation could be useful to add to
help prevent/find the bug in this component?

A possible solution can be found in this patch:
https://fuchsia-review.googlesource.com/c/fuchsia/+/682671


### Triage

Triage allows to write rules to automatically process inspect snapshots and find potential issues
or gather stats that the snapshots might contain.

Reading through the [Triage codelab][triage-codelab] would be a good first step as well as reading
through the [triage config guide][triage-config-guide].

Try writing a triage configuration that could have help spot the bug in snapshots gathered in the
field.

A possible solution (built on top of the inspect solution) can be found in this patch:
https://fuchsia-review.googlesource.com/c/fuchsia/+/684762

## Verifying with tests

<!-- TODO -->

[inspect-codelab]: /docs/development/diagnostics/inspect/codelab/codelab.md
[inspect-quickstart]: /docs/development/diagnostics/inspect/quickstart.md
[profile-store]: /examples/diagnostics/workshop/fidl/profile_store.test.fidl
[profile-store-build]: /examples/diagnostics/workshop/BUILD.gn
[triage-codelab]: /docs/development/diagnostics/triage/codelab.md
[triage-config-guide]: /src/diagnostics/triage/config.md
