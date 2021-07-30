# Introduction to Fuchsia components

<<../_v2_banner.md>>

## Overview

This document offers a brief conceptual overview of Components and the
Component Framework.

In Fuchsia, _[component][glossary.component]_ is the term for the common
abstraction that defines how all software[^1] (regardless of source,
programming language, or runtime) is described, sandboxed, and executed on a
Fuchsia system.

[^1]: With the exception of early-boot software necessary to run components.

### What is sandboxing?

Sandboxing is a security mechanism to isolate programs from each other at
runtime. In Fuchsia, all software is sandboxed. When a program is initially
created, it does not have the ability to do anything -- not even to allocate
memory. The program relies on its creator to provide the capabilities needed
for it to execute. This isolation property allows Fuchsia to employ the
_principle of least privilege_: programs are provided only the minimal set
of capabilities needed to execute.

## Component Framework

The Component Framework (CF) consists of the core concepts, tools, APIs,
runtime, and libraries necessary to describe and run components and to
coordinate communication and access to resources between components.

The Component Framework includes:

-   CF concepts, including _component_, _component manifest_,
    _runner_, _realm_, _environment_, _capabilities_, and _resolver_.
-   The [`component_manager`][doc-component-manager] process, which coordinates
    the communication and sharing of resources between components.
-   [FIDL APIs](#fidl-apis) implemented by `component_manager`, or implemented
    by other components and used by `component_manager`, for the purposes of
    coordination.
-   Developer tools to build, execute, and test components.
-   Language-specific libraries for components to use to
    interact with the system. ([example](/sdk/lib/sys))
-   Testing tools and libraries to write unit and integration tests that
    exercise one or many components.
    ([example](/docs/development/components/v2/realm_builder.md))

## Capabilities

Since Fuchsia is a capability-based operating system, software on Fuchsia
interacts with other software through the use of
_[capabilities][glossary.capability]_. A capability combines both access to a
resource and a set of rights, providing both a mechanism for access control and
a means by which to interact with the resource. In Fuchsia, capabilities
typically have an underlying [kernel object][glossary.kernel-object] and
programs hold [handles][glossary.handle] to reference those underlying objects.

A common representation of a capability is a [channel][glossary.channel] that
speaks a particular [FIDL][glossary.fidl] protocol. The "server end" and the
"client end" of the channel each hold a handle allowing them to communicate
with each other. The [`fuchsia.io.Directory`][fidl-directory] protocol allows a
client to discover additional capabilities by name. To support the complex
composition of software present in today's products, the Component Framework
provides more [complex capabilities][doc-capabilities] built upon the Zircon
objects. For example, [storage][doc-storage-capability] capabilities are
represented as channels speaking the `Directory` protocol that provide access
to a unique persistent directory created for each component.

Fuchsia processes receive both named and numbered handles at launch. The named
handles always speak the `Directory` protocol. The term for the collection of
named handles is the _[namespace][glossary.namespace]_.

The Component Framework assembles the namespace for a component by consulting
[component declarations][glossary.component-declaration] that describe how the
capabilities in the namespace should be delegated at runtime. The process of
following a chain of delegation from a consuming component to a providing
component is called _[capability routing][glossary.capability-routing]_.
_Component topology_ is the term for the component instance tree and the
collective capability routes over that tree.

Note: In the Fuchsia process layer, "having a capability" means the process
holds a handle to the kernel object capability in its handle table. In the
Component Framework, we often use "having a capability" to mean that the
capability is discoverable through the component's namespace at runtime.

Further reading:

* [Zircon kernel objects][doc-kernel-objects]
* [Component Framework capabilities][doc-capabilities]
* [Capability routing][doc-capability-routing]

## Components

A _Component_ is the fundamental unit of executable software on Fuchsia.
Components are composable, meaning that components can be selected and
assembled in various combinations to create new components. A component and its
children are referred to as a _[realm][glossary.realm]_. The collective
[parent][glossary.parent-component-instance] and
[child][glossary.child-component-instance]
relationships of many individual components are referred to as the _[component
instance tree][glossary.component-instance-tree]_. A
_[moniker][glossary.moniker]_ is a topological path that identifies a specific
component instance within a component instance tree. You will often see
monikers represented as POSIX-like path strings. At its core, a component
consists of the following:

* A [Component URL][glossary.component-url], which uniquely identifies that
  component.
* A [Component manifest][glossary.component-manifest], which describes how to
  launch the component, as well as any capability routes.

Components are retrieved from a variety of origins, and can run in any runtime
(such as a native process, or in a virtual machine). To support origin and
runtime variability, the Component Framework can be extended through the use of
_resolvers_ and _[runners][glossary.runner]_. Resolvers and runners are
themselves capabilities and interact directly with the framework to extend its
functionality.

* Resolvers take a component URL as an input and produce a component manifest
  and (optionally) an access mechanism to the bytes of a software package as
  output.
* Runners consume parts of the manifest and the package, and provide the
  component's binary with a way to execute.

Note: To bootstrap the system, `component_manager` includes a built-in
resolver, the `boot-resolver`, which resolves `fuchsia-boot://` URLs to
manifests on the boot image, as well as a built-in runner, the ELF runner,
which executes ELF binaries stored in signed Fuchsia packages.

Further reading:

* [Component manager][doc-component-manager]
* [Component topology][doc-topology]
* [Realms][doc-realms]
* [Resolver capability][doc-resolvers]
* [Runner capability][doc-runners]
* [Fuchsia packages][doc-packages]

### Component lifecycle

Components move through the following lifecycle states:

* Discovered
* Started
* Stopped
* Destroyed

Components are discovered either a) by virtue of being statically declared as a
child of another component in a component manifest, or b) by being added to a
[component collection][glossary.component-collection] at runtime. Similarly,
components are destroyed implicitly by being removed from the list of static
children in a component manifest, or explicitly by being removed from a
component collection at runtime.

When a component is started or stopped, `component_manager` coordinates with
the appropriate runner to execute or terminate the component's executable.

Further reading:

* [Component lifecycle][doc-lifecycle]

### Components and capability routing

When started, every component receives its namespace as well as a handle to the
server end of a `Directory` channel. This `Directory` channel is called the
the _[outgoing directory][glossary.outgoing-directory]_. Through the
outgoing directory, the component's executable makes discoverable any
capabilities that it serves directly. The Component Framework brokers discovery
from one component's namespace to another's outgoing directory.

A component can interact with the system and other components only via the
capabilities discoverable through its namespace and the few [numbered
handles][src-processargs] it receives. The namespace for the component is
assembled from declarations in the component's manifest. However, these
component manifest declarations alone are not sufficient to gain access to the
capabilities at runtime. For these capabilities to be available, there must
also be a valid capability route from from the consuming component to a
provider. Since capabilities are most often routed through parent components to
their children, parent components play an important role in defining the
sandboxes for their child components.

Namespaces use directory path semantics, so many components use a POSIX-style
interface that treats their namespace as a local file system.

While most capabilities are routed to and from component instances, _runner_
and _resolver_ capabilities are routed to
_[environments][glossary.environment]_. Environments configure the behavior of
the framework for the realms to which they are assigned. Capabilities routed to
these environments are accessed and used by the framework. Component instances
themselves do not have runtime access to the capabilities in their
environments.

Further reading:

* [Environments][doc-environments]

[fidl-directory]: https://fuchsia.dev/reference/fidl/fuchsia.io#Directory
[glossary.capability]: /docs/glossary#capability
[glossary.handle]: /docs/glossary#handle
[glossary.channel]: /docs/glossary#channel
[glossary.realm]: /docs/glossary#realm
[glossary.environment]: /docs/glossary#environment
[glossary.outgoing-directory]: /docs/glossary#outgoing-directory
[glossary.moniker]: /docs/glossary#moniker
[glossary.runner]: /docs/glossary#runner
[glossary.parent-component-instance]: /docs/glossary#parent-component-instance
[glossary.child-component-instance]: /docs/glossary#child-component-instance
[glossary.component-collection]: /docs/glossary#component-collection
[glossary.component-manifest]: /docs/glossary#component-manifest
[glossary.component-url]: /docs/glossary#component-url
[glossary.component-instance-tree]: /docs/glossary#component-instance-tree
[glossary.namespace]: /docs/glossary#namespace
[glossary.component-declaration]: /docs/glossary#component-declaration
[glossary.kernel-object]: /docs/glossary#kernel-object
[glossary.capability-routing]: /docs/glossary#capability-routing
[glossary.fidl]: /docs/glossary#fidl
[src-processargs]: /zircon/system/public/zircon/processargs.h
[doc-capability-routing]: /docs/concepts/components/v2/topology.md#capability-routing
[doc-capabilities]: /docs/concepts/components/v2/capabilities/README.md
[doc-kernel-objects]: /docs/reference/kernel_objects/objects.md
[doc-storage-capability]: /docs/concepts/components/v2/capabilities/storage.md
[doc-component-manager]: /docs/concepts/components/v2/component_manager.md
[doc-declarations]: /docs/concepts/components/v2/component_manifests.md#component-declaration
[doc-design-principles]: /docs/concepts/components/v2/design_principles.md
[doc-environments]: /docs/concepts/components/v2/environments.md
[doc-instances]: /docs/concepts/components/v2/topology.md#component-instances
[doc-lifecycle]: /docs/concepts/components/v2/lifecycle.md
[doc-realm-builder]: /docs/development/components/v2/realm_builder.md
[doc-realms]: /docs/concepts/components/v2/realms.md
[doc-runners]: /docs/concepts/components/v2/capabilities/runners.md
[doc-resolvers]: /docs/concepts/components/v2/capabilities/resolvers.md
[doc-topology]: /docs/concepts/components/v2/topology.md
[doc-component-urls]: /docs/concepts/components/component_urls.md
[doc-packages]: /docs/concepts/packages/package.md
