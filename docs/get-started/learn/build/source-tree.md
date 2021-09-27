# Fuchsia source tree

In this section, you will learn about the organization of the Fuchsia source
code and the tools used to manage the open source project.

## Source code management

Fuchsia uses the [jiri](https://fuchsia.googlesource.com/jiri) tool to manage
git repositories in the Fuchsia project. It synchronizes a local checkout of the
source code with the
[Global Integration manifest](https://fuchsia.googlesource.com/integration) and
provides the necessary facilities to contribute changes back to Fuchsia. Global
Integration is the central ledger that defines the current state of the various
projects in the Fuchsia tree.

<aside class="key-point">
The bootstrap script performs the step described in this section for you when
you <a href="/docs/get-started/get_fuchsia_source.md">download the source</a>.
</aside>

You initialize a local jiri checkout using the `import` command with an XML
manifest that declares all the repositories and how they are organized. The
import for the default Global Integration manifest is as follows:


```posix-terminal
jiri import -name=integration flower https://fuchsia.googlesource.com/integration
```

This command adds the manifest to a local `.jiri_manifest` file at the root of
your local checkout.


```xml {:.devsite-disable-click-to-copy}
<manifest>
  <imports>
    <import manifest="flower" name="integration"
            remote="https://fuchsia.googlesource.com/integration" />
  </imports>
</manifest>
```

<aside class="key-point">
  <b>Fuchsia is a flower</b>
  <p>Notice that the default integration manifest is named
  <a href="https://fuchsia.googlesource.com/integration/+/refs/heads/main/flower">flower</a>.
  This metaphor is often applied to the Fuchsia source code, where the core
  Fuchsia platform is considered the
  <a href="https://fuchsia.googlesource.com/integration/+/refs/heads/main/stem">stem</a>
  with additional external dependencies and related projects are the
  <strong>petals</strong>.</p>
  <p>The flower manifest is a single aggregation point for the stem and various
  petal projects.</p>
</aside>

Once a local checkout is initialized on your development machine, jiri can pull
the latest changes from Global Integration at any time with one command:

```posix-terminal
jiri update
```

## Source code layout

Fuchsia is a large open source project. As with any large software project, it
can be easy to get lost without a roadmap to guide you. This section contains
an overview of a local Fuchsia checkout, with a summary of the various elements
you can expect to find along the way:

* `boards`: Contains all the default
  [board configurations](/docs/concepts/build_system/boards_and_products.md)
  supported and maintained by the Fuchsia team.
* `build`: Shared configurations and default templates for the
  [Fuchsia build system](/docs/concepts/build_system/index.md).
* `bundles`: Top-level groupings of build target labels typically included
  together in a build configuration. See
  [Bundles](/docs/concepts/build_system/bundles.md) for more details.
* `docs`: The Fuchsia documentation, including the source material for the
  [Fuchsia.dev](https://fuchsia.dev/) developer site.
* `examples`: Sample software components showcasing various aspects of the
  Fuchsia platform.
* `products`: Contains all the default
  [product configurations](/docs/concepts/build_system/boards_and_products.md)
  supported and maintained by the Fuchsia team.
* `scripts`: Various developer tools to simplify working with the Fuchsia
  source tree, including the subcommands used in
  [fx workflows](/docs/development/build/fx.md).
* `sdk`: The [Integrators Development Kit](/docs/development/idk/README.md),
   including the
  [FIDL protocol definitions](https://fuchsia.dev/reference/fidl/README.md)
  for Fuchsia services.
* `src`: Source code of Fuchsia, including components, services, and tools
  running on the target device. **This is the stem of the flower**.
* `tools`: [Fuchsia developer tools](/docs/reference/tools/sdk/README.md)
  running on the host machine.
* `vendor`: Reserved location for vendor-specific binaries and customizations
  for product builds. The build system supports discovery of configuration
  files under `vendor/products` and `vendor/boards` to build Fuchsia for
  vendor-specific device targets.
* `zircon`: Source code for Fuchsia's
  [Zircon core](/docs/concepts/kernel/README.md), including the kernel.

The source code of the Fuchsia platform breaks down further into the various
components and services running on the device. Below is not a complete list,
but may provide some interesting places to begin exploring:

* `bringup`: Core system binaries used to bring up the system's user space
  environment.
* `camera`: Support services for camera device drivers.
* `cobalt`: Fuchsia service used to log, collect and analyze metrics.
* `connectivity`: Networking protocol support and device drivers.
* `developer`: Developer tools running on the target, including
  [ffx](/docs/development/tools/ffx/overview.md).
* `devices`: Device driver support libraries for common hardware subsystems.
* `diagnostics`: Diagnostic support services such as logging, crash reporting,
  snapshots, and statistics.
* `factory`: Components implementing access to factory config data storage.
* `fonts`: Provider for built-in system fonts.
* `graphics`: Support services for display device drivers.
* `identity`: User account handling and identity token management.
* `media`: Media codecs and playback services.
* `power`: Power management services.
* `proc`: POSIX compatibility libraries.
* `recovery`: Recovery system and factory reset services.
* `security`: Security policies and analysis tools.
* `session`: [Session framework](/docs/concepts/session/introduction.md).
* `storage`: Support for [filesystems](/docs/concepts/filesystems/filesystems.md)
  and volume management.
* `sys`: [Component framework](/docs/concepts/components/v2/README.md) and
  services for [package management](/docs/concepts/packages/package.md).
* `tests`: Platform end to end (E2E) integration tests.
* `ui`: Services to support graphical user interface (GUI), including
  [Scenic](/docs/concepts/graphics/scenic/README.md).
* `virtualization`: Hypervisor support for VM guests.
* `zircon`: Libraries for interacting with the Zircon kernel.

Note: For more details on how projects are structured in the Fuchsia tree, see
[Source code layout](/docs/concepts/source_code/layout.md).


## Exercise: Navigate the source tree

In this exercise, you'll explore your local checkout of the Fuchsia source tree
using the command line tools available in the environment. Becoming familiar
with these tools will make you more productive as you begin to contribute to the
codebase.

<aside class="key-point">
If you prefer a more graphical interface, you can use
<a href="https://cs.opensource.google/fuchsia">Google Code Search</a> to explore
the Fuchsia tree as well.
</aside>

### Search the tree

If you're not sure where to start, you can use the `fd` utility to perform fuzzy
searches for directories, then navigate to the location of a search result.

Run the following command to run an `fd` search for `component_manager`:


```posix-terminal
fd component_manager
```

<aside class="key-point">
This tool is configured in your environment using <code>fx-env.sh</code>. If you
are unable to access the <code>fd</code> command, ensure you have
<a href="/docs/get-started/get_fuchsia_source.md#set-up-environment-variables">
set up your environment</a>.
</aside>

The utility prints a few possible options for you to choose from. Select option
2 to navigate to `src/sys/component_manager`:

```none {:.devsite-disable-click-to-copy}
[1] src/session/bin/component_manager
[2] src/sys/component_manager

```

This enables you to easily find and navigate the piece of code where you want to
work. If the search is specific enough to return a single result, `fd` will
navigate you there automatically.

Run the following command to perform a search for `archivist` — Fuchsia's
diagnostics service for collecting log data, snapshots, and lifecycle events:

```posix-terminal
fd archivist
```

Notice that the command didn't actually print any results, but your working
directory was automatically set to `src/diagnostics/archivist`!


<aside class="key-point">
  <b>Tip:</b> You can run <code>fd</code> without any arguments to jump back to
  the source root from anywhere.
</aside>

This is helpful to get you started, but there are several things you may want to
search for in the Fuchsia tree that require **searching inside files**.


### Search within source files

To search the tree for patterns within specific source files, use the
`fx grep` command.

Run a search in the tree looking for references to the `hello-world` example
using `fx grep`:

```posix-terminal
fx grep hello-world
```

This returns a long list of references from across the tree, because this
example is referenced in documentation, build files, and other sources.

You can refine the search using filters to help narrow in on the protocol
definition. Perform the same search again, but this time only in GN build files
using a filter:

Note: For a complete list of available filters, see the
[`fx grep` reference](https://fuchsia.dev/reference/tools/fx/cmd/grep).

```posix-terminal
fx grep hello-world -- build
```


The results indicate that the protocol definition is located at
`examples/hello_world`. You can combine this information with `fd` to
navigate there:


```posix-terminal
fd hello_world
```

<aside class="key-point">
  <b>Extra credit</b>
  <p>Use <code>fx grep</code> to find components that implement the
  <code>fuchsia.component.runner</code> FIDL protocol? How many are there?</p>
</aside>
