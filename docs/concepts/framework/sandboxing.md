# Sandboxing

This document describes how sandboxing works for a [component](/docs/glossary.md#component)
in Fuchsia.

The sandbox defines the addressable resources for a component. The sandbox is
not shared with other components. It's is empty by default, and additions are
declared in the component manifest.

## Newly created components are empty

In Fuchsia, a newly created component is empty. It cannot
access any kernel objects, allocate memory, or execute code.
Because of this, components are usually created with some
initial resources and capabilities.

Most commonly, a component starts executing some code with an initial stack, some
command line arguments, environment variables, and a set of initial handles.
[Zircon program loading and dynamic linking](/docs/concepts/booting/program_loading.md) describes
the resources provided to programs when starting.

## Namespaces are the gateway to the world

Some of the initial handles given to a component are directories that the component
mounts into its _namespace_. These handles allow the component to discover and
communicate with other components running on the system, including file systems.

The namespace given to a component indicates how much of the system the
component can influence. Therefore, configuring the sandbox in which a component
runs amounts to configuring the component's namespace.

See [Namespaces](/docs/concepts/framework/namespaces.md) for more details.

## Package namespace

A component run from a package is given access to
`/pkg`, which is a read-only view of the package containing the component. To
access these resources at runtime, a component uses the `/pkg` namespace. For
example, the `root_presenter` can access `cursor32.png` using the absolute path
`/pkg/data/cursor32.png`.

## Services

Processes that are components receive an `/svc`
directory in their namespace. The services available through `/svc` are a
subset of the services provided by the component's
[environment](/docs/glossary.md#environment). This subset of services is determined by the
[`sandbox.services`](/docs/concepts/components/v1/component_manifests.md#sandbox) allowlist in the
component's [manifest file](/docs/concepts/components/v1/component_manifests.md).

A typical component will interact with a number of services from `/svc` in
order to play some useful role in the system. For example, the service
`fuchsia.sys.Launcher` is required if a component wishes to launch other
components.

The [AppMgr](/docs/glossary.md#appmgr), which launches components and manages namespaces,
grants requests for resources and capabilities defined in the component's sandbox.

## Configuring additional namespaces

If a component requires access to additional resources (for example, device drivers),
the package can request access to additional names by including the `sandbox`
property in its [Component Manifest](/docs/concepts/components/v1/component_manifests.md)
for the package.

For example, to request direct access to the input drive,
include the following `dev` array in your `sandbox`:

```
{
    "dev": [ "class/input" ]
}
```
