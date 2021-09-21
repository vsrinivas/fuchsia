# Component Runners

<<../../_v2_banner.md>>

A runner is a protocol that provides a runtime environment for components; in
other words, a runner actually *runs* a component. Some example runners are:

-   The [ELF runner][elf-runner] runs binaries compiled to the ELF file format.
-   The Dart AOT runner provides a runtime for Dart programs, such as a VM.
-   The Chromium web runner provides a runtime for components implemented as web
    pages.

The component framework decouples _what_ to execute from _how_ to execute it.
The component manager identifies what to execute and the runner knows how to
execute it. The runner and component manager communicate through a well-defined
API.

As stated in the [introduction][intro], a component can be implemented in any
programming language (eg. Dart) and against any framework (eg. Flutter) for
which a suitable component runner exists. Thus, the component framework is
runtime-agnostic and can support new runtimes without requiring any changes to
the component manager _or_ to other components.

When the component manager decides to start a component, it loads information
describing the component into a
[`fuchsia.component.runner.ComponentStartInfo`][sdk-component-runner] and sends
that information to the runner when it invokes the runner's
[`Start`][sdk-component-runner] method. The
[`ComponentStartInfo`][sdk-component-runner] contains the following information:
the component's URL, the component's namespace, the contents of the component's
package, and more. The runner then starts the component in a way appropriate for
its runtime. To run the component the runner may choose a strategy such as
the following:

-   Start a new process for the component.
-   Isolate the component within a virtual machine.
-   Run the component in the same process as the runner.
-   Execute the component as a job on a remote computer.

The [`fuchsia.component.runner.ComponentController`][sdk-component-runner]
protocol represents the component's excution. The runner is the server of this
protocol, and the component manager is the client. This protocol allows the
component manager to tell the runner about actions it needs to take on the
component. For example, if the component manager decides a component needs to
stop running, the component manager uses the
[`ComponentController`][sdk-component-runner] to stop the component. Typically
the runner will serve the [`ComponentController`][sdk-component-runner]
protocol, and when the runner serves a request, it is free to communicate with
the component itself in whatever way is appropriate. For example, the ELF runner
sends a message over a channel to the component running in another process,
whereas the Dart runner might directly invoke a callback method in a Dart-based
component.

## Selecting a runner

A component specifies that it should be launched with a particular runner by
adding a [`program`][program] section to its manifest. The `program` section
designates the runner as well as any runner-specific options to pass to it. The
runner must be available in the component's [environment][environments-runners]
to be usable.

For example, a component which runs as a web page might have a `program` like
the following:

```json5
program: {
    runner: "web",
    mode: "incognito",
},
```

When the component manager attempts to launch this component, it will send a
request to the provider of the `web` runner to start it.

## Making a runner available {#available}

Runners are made available to components through
[environments][environments-runners]. A runner must be registered in a
component's environment for the component to `use` it.

Runners are a type of [capability][glossary.capability], which
can be [routed][routing]. Note that unlike other capabilities like protocols,
runners aren't routed to the components that `use` them, but to the
environment that registers them. Examples:

```json5
offer: [
    {
        runner: "web",
        from: "parent",
        to: [ "#user-shell" ],
    },
],
```

```json5
expose: [
    {
        runner: "web",
        from: "#chromium",
    },
],
```

```json5
environments: [
    {
        name: "user-env",
        extend: "realm",
        runners: [
            {
                runner: "web",
                from: "parent",
            },
        ],
    },
],
```

`expose`, `offer`, and `environment` may take an additional parameter `as` to
expose or offer the runner capability under a different name, such as the
following:

```json5
{
    expose: [
        {
            runner: "web",
            from: "#chromium",
            as: "web-chromium",
        },
    ],
}
```

## Implementing a runner

A runner can be implemented by:

1.  Providing a
    [`fuchsia.component.runner.ComponentRunner`][sdk-component-runner] protocol
    protocol from a component, and
2.  Declaring a runner capability backed by this protocol.

When the component manager is asked to launch a component that uses a particular
runner, it will send a `ComponentRunner.Start` request to the protocol. The
request will contain details about the resolved URL of the component, the
program name and arguments, and a namespace derived from the new component's
`use` declarations.

Once the component has launched, the component providing the runner protocol is
responsible for:

-   Serving a [`fuchsia.io.Directory`][sdk-directory] protocol for outgoing
    capabilities provided by the launched component.
-   Serving a [`fuchsia.io.Directory`][sdk-directory] protocol containing
    runtime information about the launched component, which will be visible in
    the [hub][hub].
-   Serving a
    [`fuchsia.component.runner.ComponentController`][sdk-component-controller]
    protocol, allowing the component manager to request the runner stop or kill
    the component.

For further details, see the
[`fuchsia.component.runner.ComponentRunner`][sdk-component-runner]
reference documentation.

For a runner to be routable, the runner component's manifest must declare the
`ComponentRunner` capability as follows:

```json5
{
    capabilities: [
        {
            // Name for the runner.
            runner: "web",

            // Path to the protocol in our outgoing directory.
            path: "/svc/fuchsia.component.runner.ComponentRunner",
        },
    ],
}
```

See [Making a runner available](#available) for further instructions on how to
route the runner capability so that it is available to launch other components.

[glossary.capability]: /docs/glossary/README.md#capability
[elf-runner]: ../elf_runner.md
[environments-runners]: ../environments.md#runners
[routing]: ../component_manifests.md#capability-routing
[expose]: ../component_manifests.md#expose
[hub]: ../hub.md
[intro]: ../introduction.md#a_component_is_a_hermetic_composable_isolated_unit_of_software
[offer]: ../component_manifests.md#offer
[program]: ../component_manifests.md#program
[sdk-component-controller]: /sdk/fidl/fuchsia.component.runner/component_runner.fidl
[sdk-component-runner]: /sdk/fidl/fuchsia.component.runner/component_runner.fidl
[sdk-directory]: /sdk/fidl/fuchsia.io/directory.fidl
[use]: ../component_manifests.md#use
