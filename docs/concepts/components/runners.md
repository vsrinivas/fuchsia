# Component Runners

A runner is a protocol that provides a runtime environment for components; in
other words, a runner actually *runs* a component.
Some example runners are:

-   The component manager comes with an built in [ELF runner][elf-runner] which
    launches binaries using the ELF file format.
-   The Dart AOT runner provides a runtime for Dart programs, such as a VM.
-   The Chromium web runner provides a runtime for components implemented as web
    pages.

The component framework decouples _what_ to execute from _how_ to execute it.
The component manager identifies what to execute and the runner knows how to
execute it. The runner and component manager communicate through a
well-defined API.

As stated in the [introduction][intro], a component can be implemented in any
programming language (eg. Dart) and against any framework (eg. Flutter) for
which a suitable component runner exists. Thus, the component framework is
runtime-agnostic and can support new runtimes without requiring any changes to
the component manager.

When the component manager decides to start a component, it loads information
describing the component into a
[`fuchsia.sys2.ComponentStartInfo`][sdk-component-runner] and sends that
information to the runner when it invokes the runner's
[`Start`][sdk-component-runner] method. The
[`ComponentStartInfo`][sdk-component-runner]
contains the following information: the component's URL, the component's
namespace, the contents of the component's package, and more. Then the runner
starts the component in a way appropriate for that component. To run the
component the runner may choose a strategy such as the following:

-  Start a new process for the component.
-  Locate the component together in the same process as other components.
-  Run the component in the same process as the runner.
-  Execute the component as a job on a remote computer.

The [`fuchsia.sys2.ComponentController`][sdk-component-runner] protocol
represents the component's excution. The runner is the server of this protocol,
and the component manager is the client. This protocol allows the component
manager to tell the runner about actions it needs to take on the component. For
example, if the component manager decides a component needs to stop running, the
component manager uses the [`ComponentController`][sdk-component-runner] to stop
the component. Typically the runner will serve the
[`ComponentController`][sdk-component-runner] protocol, and when the runner
serves a request, it is free to communicate with the component itself in
whatever way is appropriate. For example, the ELF runner might send a message
over a channel to the component running in another process, whereas the Dart
runner might directly invoke a callback method in a Dart-based component.

## Using a runner

A component can specify that it should be launched with a particular runner by
[using][use] a runner capability that has been offered to the component from its
containing realm. For example, a component can use the `web` runner by including
the following stanza in its `cml` file:

```
{
    "use": [{
        "runner": "web",
    }],
}
```

When the component manager attempts to launch this component, it will send a
request to the provider of the `web` runner capability to start it.

If a component's children need to use a particular runner capability, that
runner must be explicitly [offered][offer] to the children:

```
{
    "offer": [{
        "runner": "web",
        "from": "realm",
        "to": ["#child-a", "#child-b"],
    }],
}
```

Finally, a runner capability may be [exposed][expose] to its containing realm as
follows:

```
{
    "expose": [{
        "runner": "web",
        "from": "#child",
    }],
}
```

Both the `expose` and `offer` blocks may take an additional parameter `as` to
expose or offer the runner capability under a different name, such as the
following:

```
{
    "expose": [{
        "runner": "web",
        "from": "#child",
        "as": "web-chromium",
    }],
}
```

## Implementing a runner

A runner can be implemented by:

1.  Providing a [`fuchsia.sys2.ComponentRunner`][sdk-component-runner] protocol
    protocol from a component, and
2.  Declaring a runner capability backed by this protocol.

When the component manager is asked to launch a component that uses a particular
runner, it will send a `ComponentRunner.Start` request to the protocol. The
request will contain details about the resolved URL of the component, the
program name and arguments, and a namespace derived from the new component's
`use` declarations.

Once the component has launched, the component providing the runner protocol is
responsible for:

-   Providing a [`fuchsia.io.Directory`][sdk-directory] protocol for outgoing
    protocols provided by the launched component;
-   Providing a [`fuchsia.io.Directory`][sdk-directory] protocol containing
    runtime information about the launched component, which will be visible in
    the [hub][hub];
-   Providing a [`fuchsia.sys2.ComponentController`][sdk-component-controller]
    protocol, allowing the component manager to request the runner stop or kill
    the component.

Further details are in the
[`fuchsia.sys2.ComponentRunner`][sdk-component-runner] documentation.

For a protocol offered by a component to be used as a runner, it must also
declare a runner capability in its component manifest, as follows:

```
{
    "runners": [{
        // Name for the runner.
        "name": "web",

        // Indicate this component provides the protocol.
        "from": "self",

        // Path to the protocol in our outgoing directory.
        "path": "/svc/fuchsia.sys2.ComponentRunner",
    }],
}
```

The created runner capability may then be [offered][offer] to children, or
[exposed][expose] to the containing realm, as follows:

```
{
    "offer": [{
        "runner": "web",
        "from": "self",
        "to": ["#child-a", "#child-b"],
    }],
    "expose": [{
        "runner": "web",
        "from": "self",
    }],
}
```

[elf-runner]: elf_runner.md
[expose]: component_manifests.md#expose
[hub]: hub.md
[intro]: introduction.md#a-component-is-a-hermetic-composable-isolated-program
[offer]: component_manifests.md#offer
[sdk-component-controller]: /sdk/fidl/fuchsia.sys2/runtime/component_runner.fidl
[sdk-component-runner]: /sdk/fidl/fuchsia.sys2/runtime/component_runner.fidl
[sdk-directory]: /zircon/system/fidl/fuchsia-io/io.fidl
[use]: component_manifests.md#use
