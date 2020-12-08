# Fuchsia roadmap

The Fuchsia project values transparency with our community. We are sharing
our roadmap to give our community insight into the projects that are being
actively explored by Fuchsia teams.

The projects listed in this roadmap are subject to change and could
be modified based on a change in priorities.

While this list of projects is not exhaustive, it provides a high-level overview
of active projects that inform the way that we’re currently thinking about
Fuchsia.

## FIDL

The [Fuchsia Interface Definition Language (FIDL)](/docs/glossary.md#fidl)
is a language for defining protocols that are typically used over [channels](/docs/glossary.md#channel).

The FIDL team is actively exploring the following projects:

### Unifying FIDL C++ bindings

Currently, there are two FIDL binding implementations, [high-level C++ bindings (HLCPP)](/docs/reference/fidl/bindings/hlcpp-bindings.md)
and [low-level C++ bindings (LLCPP)](/docs/reference/fidl/bindings/llcpp-bindings.md).
The FIDL team is exploring a solution that augments the LLCPP API surface to
allow the use of high-level domain objects wherever low-level domain objects
are currently used.

### Implementing FIDL versioning

This project helps Fuchsia evolve its APIs through a platform
versioning strategy, which involves annotating FIDL elements with version ranges
and providing tooling to use a FIDL API at a user-specified version.

### FIDL syntax revamp

The FIDL team is working to revamp the syntax for the FIDL language to help
developers understand when changes to a FIDL definition break downstream code.

## Migrating to fuchsia.io2

The Process Framework team is actively exploring how to migrate Fuchsia
libraries and applications from [fuchsia.io](/sdk/fidl/fuchsia.io/)
to [fuchsia.io2](/sdk/fidl/fuchsia.io2/)
in order to increase type safety and client reliability.

## Migrating to fuchsia.hardware.network

The Connectivity team is actively exploring how to migrate existing Fuchsia
drivers and clients from [fuchsia.hardware.ethernet](/sdk/fidl/fuchsia.hardware.ethernet/)
to [fuchsia.hardware.network](/sdk/fidl/fuchsia.hardware.network/)
in order to improve network performance.

## Components v2

[Components v2](/docs/glossary.md#components-v2) is
Fuchsia’s component architecture that replaces [Components v1](/docs/glossary.md#components-v1).

The Component Framework team is actively exploring the following projects:

### Continuing migration to Components v2

The goal of the [Component Framework](/docs/glossary.md#component-framework)
is to define Fuchsia’s units of software execution as components, which are
singular abstractions throughout the Fuchsia system.

In keeping with this goal, the Component Framework team is exploring ways to
migrate those dependent on Components v1 to Components v2, including Netstack.
In Netstack, the migration would allow for capability routing and a shift to the
Fuchsia Testing Framework from Netemul Runner.

### Implementing drivers as components

The Driver Framework team is exploring how to use Components v2 to represent
drivers as components, so that drivers can interact with the rest of Fuchsia
in a uniform way.

## Miscellaneous

The following projects are under consideration by several different Fuchsia
teams:

### Implementing storage enhancements

Teams are evaluating potential improvements that could be
made to [MinFS](/docs/concepts/filesystems/minfs.md)
and [VFS](/docs/concepts/system/life_of_an_open.md#vfs_layer),
including generating a system for benchmarking and implementing paging within
the VFS layer.

### Implementing accessibility and input improvements

Multiple teams are collaborating to explore more inclusive handling of user
input events on workstations that are running Fuchsia.

