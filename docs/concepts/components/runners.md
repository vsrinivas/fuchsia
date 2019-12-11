# Component Runners

A runner is a service that provides a runtime environment for other components.
For example:

-   The component manager comes with an built in [ELF runner][elf-runner] which
    launches binaries using the ELF file format.
-   The Dart AOT runner provides a runtime for Dart programs, such as a VM.
-   The Chromium web runner provides a runtime for components implemented as web
    pages.

The following sections give details about how components can use a particular
runner, and how new runners can be implemented.

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

1.  Providing a [`fuchsia.sys2.ComponentRunner`][sdk-component-runner] service
    protocol from a component, and
2.  Declaring a runner capability backed by this service protocol.

When the component manager is asked to launch a component that uses a particular
runner, it will send a `ComponentRunner.Start` request to the service. The
request will contain details about the resolved URL of the component, the
program name and arguments, and a namespace derived from the new component's
`use` declarations.

Once the component has launched, the component providing the runner service is
responsible for:

-   Providing a [`fuchsia.io.Directory`][sdk-directory] service for outgoing
    services provided by the launched component;
-   Providing a [`fuchsia.io.Directory`][sdk-directory] service containing
    runtime information about the launched component, which will be visible in
    the [hub][hub];
-   Providing a [`fuchsia.sys2.ComponentController`][sdk-component-controller]
    service, allowing the component manager to request the runner stop or kill
    the component.

Further details are in the
[`fuchsia.sys2.ComponentRunner`][sdk-component-runner] documentation.

For a service offered by a component to be used as a runner, it must also
declare a runner capability in its component manifest, as follows:

```
{
    "runners": [{
        // Name for the runner.
        "name": "web",

        // Indicate this component provides the service.
        "from": "self",

        // Path to the service in our outgoing directory.
        "path": "/svc/fuchsia.io.ComponentRunner",
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
[offer]: component_manifests.md#offer
[sdk-component-controller]: /sdk/fidl/fuchsia.sys2/runtime/component_runner.fidl
[sdk-component-runner]: /sdk/fidl/fuchsia.sys2/runtime/component_runner.fidl
[sdk-directory]: /zircon/system/fidl/fuchsia-io/io.fidl
[use]: component_manifests.md#use
