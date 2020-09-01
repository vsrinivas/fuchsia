# Sandboxing

This document describes how sandboxing works in Fuchsia.

## An empty process has nothing

On Fuchsia, a newly created process has nothing. A newly created process cannot
access any kernel objects, cannot allocate memory, and cannot even execute code.
Of course, such a process isn't very useful, which is why we typically create
processes with some initial resources and capabilities.

Most commonly, a process starts executing some code with an initial stack, some
command line arguments, environment variables, a set of initial handles. One of
the most important initial handles is the `PA_VMAR_ROOT`, which the process can
use to map additional memory into its address space.

## Namespaces are the gateway to the world

Some of the initial handles given to a process are directories that the process
mounts into its _namespace_. These handles let the process discover and
communicate with other processes running on the system, including file systems
and other servers. See [Namespaces](/docs/concepts/framework/namespaces.md) for more details.

The namespace given to a process strongly influences how much of the system the
process can influence. Therefore, configuring the sandbox in which a process
runs amounts to configuring the process's namespace.

## Package namespace

A [component](/docs/glossary.md#component) run from a package is given access to
`/pkg`, which is a read-only view of the package containing the component. To
access these resources at runtime, a process can use the `/pkg` namespace. For
example, the `root_presenter` can access `cursor32.png` using the absolute path
`/pkg/data/cursor32.png`.

## Services

Processes that are [components](/docs/glossary.md#component) receive an `/svc`
directory in their namespace. The services available through `/svc` are a
subset of the services provided by the component's
[environment](/docs/glossary.md#environment). This subset is determined by the
[`sandbox.services`](/docs/concepts/components/v1/component_manifests.md#sandbox) allowlist in the
component's [manifest file](/docs/concepts/components/v1/component_manifests.md).

A typical component will interact with a number of services from `/svc` in
order to play some useful role in the system. For example, the service
`fuchsia.sys.Launcher` is required if a component wishes to launch other
components.

Processes that are not components may or may not have `/svc`. These processes
receive whatever `/svc` their creator decided to provide to them.

## Configuring additional namespaces

If a process requires access to additional resources (e.g., device drivers),
the package can request access to additional names by including the `sandbox`
property in its  [Component Manifest](/docs/concepts/components/v1/component_manifests.md)
for the package. For example, to request direct access the input drive,
include the following `dev` array in your `sandbox`:

```
{
    "dev": [ "class/input" ]
}
```

In the current implementation, the [AppMgr](/docs/glossary.md#appmgr) grants all such
requests, but that is likely to change as the system evolves.

## Building a package

To build a package, use the `package()` macro in `gn` defined in
[`//build/package.gni`](/build/package.gni).
See the documentation for the `package()` macro for details about including resources.

For examples, see [/garnet/packages/prod/fortune]
and [/examples/fortune/BUILD.gn].
