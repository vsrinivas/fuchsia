# Introduction to the Fuchsia Component Framework

This document offers a brief conceptual overview of the component framework
along with links to more detailed documents on specific topics.

Note: The component framework is under active development. This document
only covers the [new architecture][glossary-components-v2] implemented by
`component_manager`. The [old architecture][glossary-components-v1] implemented
by `appmgr` is still in use but will be removed once the transition to the
new architecture is complete.

## Components and the Component Framework

A [component][glossary-component] is a program that runs in its own sandbox
on Fuchsia and that interacts with other components using inter-process
communication [channels][glossary-channel].

The component framework is a [framework][wiki-software-framework] for
developing [component-based software][wiki-component-based-software] for Fuchsia.

The component framework is responsible for running nearly all software on
Fuchsia so it is important for developers to learn how it works and how to
use it effectively.

## Purpose

The component framework emphasizes
[separation of concerns][wiki-separation-of-concerns] by helping developers
to write simpler programs as components that work together to support more
complex systems through composition.

Each component typically has a small number of responsibilities. For example,
an ethernet driver component exposes a hardware interface service that the
network stack component uses to send and receive ethernet frames. These
components can work together smoothly because they agree on a common set
of protocols even though they may have been authored by different parties
or distributed separately.

Software composition offers numerous advantages:

- Configurability: The behavior of the system can be changed easily by adding,
  upgrading, removing, or replacing individual components.
- Extensibility: As components are added, the functionality of the system grows.
- Reliability: The system can recover from faults gracefully by stopping or
  restarting individual components.
- Reuse: General purpose components can be reused and composed with other
  components to solve new problems.
- Testability: Prior to integration, each component can be verified separately
  so it is easier to isolate bugs.
- Uniformity: All components describe their capabilities in the same way
  independent of their origin, purpose, or implementation language.

Fuchsia takes software composition to its logical conclusion by building
almost the entire system from components (including device drivers). The
component framework makes it easier to update and improve the system
incrementally as new software becomes available.

## Everything is a Component (Almost)

Components are ubiquitous. They are governed by the same mechanisms and they
all work together seamlessly.

Almost all programs run as components on Fuchsia, including:

- Command-line tools
- Device drivers
- End-user applications
- Filesystems
- Media codecs
- Network stacks
- Tests
- Web pages

There are only a few exceptions, notably:

- Bootloaders
- Device firmware
- Kernels
- Bootstrap for the component manager itself
- Virtual machine guest operating systems

## A Component is a Hermetic Composable Isolated Program

A component is a **program**.

- It is a unit of executable software.
- It is identified by a URL from which its code can be loaded and instantiated.
- Its behavior can be implemented in any programming language for which a
  suitable component runner exists.
- It has a [declaration][glossary-component-declaration] that describes what
  it can do and how to run it.

A component is an **isolated** program.

- Each of its instances runs in its own separate "sandbox".
- It is granted limited [capabilities][glossary-capability] to perform its task
  according to the [Principle of Least Privilege][wiki-least-privilege].
- It cannot access capabilities other than those it has been granted.
- Its lifecycle and state are independent from that of other components.
- It primarily communicates with other components via IPC.
- Its faults and misbehavior cannot compromise the integrity of the entire
  system.

A component is a **composable** isolated program.

- It can be combined with other components to form more complex components.
- It can reuse the functionality of other components by adding instances of
  them as its children given their URL, both statically and dynamically.
- It grants capabilities to its children using
  [capability routing](#capability-routing).

A component is a **hermetic** composable isolated program.

- It represents an encapsulation boundary.
- Its implementation can be changed without affecting other components as
  long as it exposes the same capabilities to them.
- It is distributed in a form that includes everything its
  [component runner][doc-runners] needs to run it, including its shared libraries.

## Component Manager and the Boot Process {#component-manager}

The [component manager][glossary-component-manager] is the heart of the
component framework. It manages the lifecycle of all components, provides them
with the [capabilities][doc-capabilities] they require, and keeps them isolated
from one another.

The system starts the component manager very early in the boot process.
The component manager first starts the root component. The root component
then asks the component manager to start other components including the device
manager, filesystems, network stack, and other essential services.

As more and more components are started, the system springs to life. Eventually,
the [session framework][glossary-session-framework] starts the user interface
components and the user takes control.

## Component Instances {#component-instances}

A [component instance][glossary-component-instance] is a distinct copy of a
component running in its own sandbox with its own state that is separate from
that of any other component instance.

The terms component and component instance are often used interchangeably
when the context is clear. For example, it would be more precise to talk about
"starting a component instance" rather than "starting a component" but you
can easily infer that "starting a component" requires an instance of that
component to be created first so that the instance can be started.

## Component Lifecycle {#component-lifecycle}

Component instances progress through four major lifecycle events: create,
start, stop, and destroy.

Unlike [processes][glossary-process], component instances continue to exist
and can retain state even when they are not running thereby allowing them to
be stopped and restarted repeatedly while preserving the
[illusion of continuity](#continuity).

Refer to [lifecycle][doc-lifecycle] for more details.

### Create

When a component instance is created, the component frameworks assigns a
unique identity to the instance, adds it to the
[component topology](#component-topology), and makes its capabilities
available for other components to use.

Once created, a component instance can then be started or destroyed.

### Start

Starting a component instance loads and runs the component's program
and provides it access to the capabilities that it requires.

[Every component runs for a reason](#accountability). The component framework
only starts a component instance when it has work to do, such as when
another component requests to use its instance's capabilities.

Once started, a component instance continues to run until it is stopped.

### Stop

Stopping a component instance terminates the component's program but preserves
its [persistent state][doc-storage] so that it can continue where it left off
when subsequently restarted.

The component framework may stop a component instance for a variety of
reasons, such as:

- When all of its clients have disconnected.
- When its parent is being stopped.
- When its package needs to be updated.
- When there are insufficient resources to keep running the component.
- When other components need resources more urgently.
- When the component is about to be destroyed.
- When the system is shutting down.

A component can implement a [lifecycle handler][doc-lifecycle] to be notified
of its impending termination and other events on a best effort basis. Note
that a component can be terminated involuntarily and without notice in
circumstances such as resource exhaustion, crashes, or power failure.

Once stopped, a component instance can then be restarted or destroyed.

### Destroy

Destroying a component instance permanently deletes all of its associated
state and releases the system resources it consumed.

Once destroyed, a component instance ceases to exist and cannot be restarted.
New instances of the same component can still be created but they will each
have their own identity and state distinct from all prior instances.

## Component Declarations {#component-declarations}

A [component declaration][doc-manifests] is a machine-readable
description of what the component can do and how to run it. It contains
metadata that the component framework requires to instantiate the component
and to compose the component with others.

Every component has a declaration. For components that are distributed in
[packages][glossary-package], the declaration typically takes the form of
a [component manifest file][doc-manifests].

Components can also be distributed in other forms such as web applications
with the help of a suitable [resolver][doc-resolvers] and [runner][doc-runners]
which provide the necessary component declaration and take care of running the
component.

For example, the declaration for a calculator component might specify the
following information:

- The location of the calculator program within its package.
- The name of the [runner][doc-runners] used to run the program.
- A request for persistent storage to save the contents of the calculator's
  accumulator across restarts.
- A request to use capabilities to present a user interface.
- A request to expose capabilities to allow other components to access the
  calculator's accumulator register using inter-process communication.

## Component URLs {#component-urls}

A component URL specifies the location from which a component's declaration,
program, and assets are retrieved.

Components can be retrieved from many different sources as indicated by
the URL scheme. These are some common URL schemes you may encounter:

- `fuchsia-boot`: The component is resolved from the system boot image. This
  scheme is used for retrieving components that are essential to the system's
  operation during early boot before the package system is available.
  - Example: "fuchsia-boot:///#meta/devcoordinator.cm"
- [`fuchsia-pkg`][doc-package-url]: The component is resolved by the
  Fuchsia package resolver. This scheme is used for components that are
  distributed in the form of packages which can be downloaded on demand and
  kept up-to-date.
  - Example: "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cm"
- `http` and `https`: The component is resolved as a web application by a web
  resolver. This scheme is used to integrate web-based content with the
  component framework.
  - Example: "https://fuchsia.dev/"

Note: The set of URL schemes available in each [realm](#realms) is configured
with [capability routing](#capability-routing) in accordance with the realm's
need to access components from various sources. The examples presented above
are not universal.

Note: Use [monikers](#monikers) to identify specific instances of components
instead of their source.

## Component Topology {#component-topology}

The component topology is an abstract data structure that describes the
relationships among component instances. It is made of three parts:

- Component instance tree: Describes how component instances are
  [composed](#composition) together (their parent-child relationships).
- Capability routing graph: Describes how component instances gain access to
  [use capabilities](#capability-routing) exposed by other component instances
  (their provider-consumer relationships).
- Compartment tree: Describes how component instances are
  [isolated](#compartments) from one another and the resources their sandboxes
  may share at runtime (their isolation relationships).

TODO: Add a picture or a thousand words.

The structure of the component topology greatly influences component lifecycle
and use of capabilities.

### Hierarchical Composition {#composition}

Any number of components can be combined together to make more complex
components through hierarchical composition.

In hierarchical composition, a parent component creates instances of other
components which are known as its children. The newly created children belong
to the parent and are dependent upon the parent to provide them with the
capabilities that they need to run. Meanwhile, the parent gains access to the
capabilities exposed by its children through
[capability routing](#capability-routing).

Children can be created in two ways:

- Statically: The parent declares the existence of the child in its own
  [component declaration](#component-declarations). The child is destroyed
  automatically if the child declaration is removed in an updated version of
  the parent's software.
- Dynamically: The parent uses [realm services][doc-realms] to add
  a child to a [component collection][doc-collections] that the parent declared.
  The parent destroys the child in a similar manner.

Children remain forever dependent upon their parent; they cannot be reparented
and they cannot outlive their parent. When a parent is destroyed so are all of
its children.

The component topology represents the structure of these parent-child
relationships as a [component instance tree][glossary-component-instance-tree].

TODO: Add a diagram of a component instance tree.

### Encapsulation {#encapsulation}

The capabilities of child components cannot be directly accessed outside of the
scope of their parent; they are encapsulated.

This model resembles [composition][wiki-object-composition] in object-oriented
programming languages.

### Realms {#realms}

A realm is a subtree of component instances formed by
[hierarchical composition](#composition). Each realm is rooted by a component
instance and includes all of that instance's children and their descendants.

Realms are important [encapsulation](#encapsulation) boundaries in the
component topology. The root of each realm receives certain privileges to
influence the behavior of components, such as:

- Declaring how capabilities flow into, out of, and within the realm.
- Binding to child components to access their services.
- Creating and destroying child components.

See the [realms documentation][doc-realms] for more information.

### Monikers {#monikers}

A moniker identifies a specific component instance in the component tree
using a topological path. Monikers are collected in system logs and for
persistence.

See the [monikers documentation][doc-monikers] for details.

### Capability Routing {#capability-routing}

Components gain access to use capabilities exposed by other components through
capability routing.

TODO: Refactor existing [manifests][doc-manifests] and
[capabilities][doc-capabilities] to explain the basic concepts here.
Draw parallels with constructor dependency injection. Include links to
capability types.

### Compartments {#compartments}

A compartment is an isolation boundary for component instances. It is an
essential mechanism for preserving the
[confidentiality, integrity, and availability][wiki-infosec] of components.

Physical hardware can act as a compartment. Components running on the
same physical hardware share CPU, memory, persistent storage, and peripherals.
They may be vulnerable to side-channels, privilege elevation, physical attacks,
and other threats that are different from those faced by components running
on different physical hardware. System security relies on making effective
decisions about what capabilities to entrust to components.

A [job][glossary-job] can act as a compartment. Running a component in its
own job ensures that the component's [processes][glossary-process] cannot
access the memory or capabilities of processes belonging to other components
in other jobs. The component framework can also kill the job to kill all of
the component's processes (assuming the component could not create processes
in other jobs). The kernel strongly enforces this isolation boundary.

A [runner][doc-runners] provides a compartment for each component that it runs.
The runner is responsible for protecting itself and its runnees from each
other, particularly if they share a runtime environment (such as a process)
that limits the kernel's ability to enforce isolation.

Compartments nest: runner provided compartments reside in job compartments
which themselves reside in hardware compartments. This encapsulation clarifies
the responsibilities of each compartment: the kernel is responsible for
enforcing job isolation guarantees so a runner doesn't have to.

Some compartments offer weaker isolation guarantees than others. A job offers
stronger guarantees than a runner so sometimes it makes sense to run multiple
instances of the same runner in different job compartments to obtain those stronger guarantees on behalf of runnees. Similarly, running each component
on separate hardware might offer the strongest guarantees but would be
impractical. There are trade-offs.

TODO: Fill in more details when component framework APIs for assigning
components to compartments have been formalized.

## Framework Capabilities

Components use framework capabilities to interact with their environment:

- [Instrumentation Hooks][doc-hooks]: Diagnose and debug components.
- [Hub][doc-hub]: Examine the component topology at runtime.
- [Realm][doc-realms]: Manage and bind to child components.
- [Lifecycle][doc-lifecycle]: Listen and handle lifecycle events.
- [Shutdown][doc-shutdown]: Initiate an orderly shut down of the system.
- [Work Scheduler][doc-scheduler]: Schedule deferrable work.

## Framework Extensions

Components use framework extensions to integrate the component framework
with software ecosystems:

- [Runners][doc-runners]: Integrate programming language runtimes and
  software frameworks.
- [Resolvers][doc-resolvers]: Integrate software delivery systems.

## Component Development

TODO: Link to docs about how to build components, diagnostic tools,
and debugging features.

## Design Principles

### Accountability {#accountability}

System resources are finite. There's only so much memory, disk, or CPU time
available on a computing device. The component framework keeps track of how
resources are used by components to ensure they are being used efficiently
and that they can be reclaimed when no longer required or when they are more
urgently needed for other purposes if the system is oversubscribed.

Resources must be used for a reason.

For example, every running process must belong to at least one component
instance whose capabilities are currently in use, were recently of use, or will
soon be of use; any outliers are considered to be running for no reason and are
promptly stopped.

Similarly, the system may terminate processes if they exceed the resource
constraints of the components that are responsible for them.

Here are some more examples of accountability:

- Every component exists for a reason: Parent component instances are
  responsible for determining the existence of their children by destroying
  children that are no longer of use. Parents also play a role in setting
  resource constraints for their children.
- Every component runs for a reason: The component framework starts
  component instances when they have work to do, such as in response to
  incoming service requests from other components, and stops them when the
  demand is gone (or has lesser priority than other demands that contend for
  the same resources).
- Metrics: The component framework provides mechanisms for diagnostics tools
  to audit resource usage by components over time.

As a general rule, every resource in the system must be accounted for in
some way so the system can ensure they are being used effectively.

### The Illusion of Continuity {#continuity}

The component framework offers mechanisms to preserve the illusion of
continuity: the user should generally not be concerned about restarting their
software because it will automatically resume right where they left off,
even when they reboot or replace their devices.

The fidelity of the illusion depends on how well the following properties
are preserved across restarts:

- State: Preserving the user-visible state of component instances.
- Capabilities: Preserving the rights granted to component instances.
- Structure: Preserving the relationships between collaborating component
  instances such that they can reestablish communication as required.
- Behavior: Preserving the runtime behavior of component instances.

In practice, the illusion is imperfect. The system cannot guarantee faithful
reproduction in the presence of software upgrades, non-determinism, bugs,
faults, and external dependencies on network services.

While it might seem simpler to keep components running forever, eventually the
system will run out of resources so it needs a way to balance its working
set size by stopping less essential components at a moment's notice.

[doc-capabilities]: capabilities
[doc-collections]: realms.md#collections
[doc-hooks]: instrumentation_hooks.md
[doc-hub]: hub.md
[doc-lifecycle]: lifecycle.md
[doc-manifests]: component_manifests.md
[doc-monikers]: monikers.md
[doc-package-url]: /docs/concepts/storage/package_url.md
[doc-realms]: realms.md
[doc-runners]: runners.md
[doc-resolvers]: resolvers.md
[doc-scheduler]: work_scheduler.md
[doc-storage]: capabilities/storage.md
[doc-shutdown]: shutdown.md
[glossary-capability]: ../../glossary.md#capability
[glossary-channel]: ../../glossary.md#channel
[glossary-component]: ../../glossary.md#component
[glossary-component-collection]: ../../glossary.md#component-collection
[glossary-component-declaration]: ../../glossary.md#component-declaration
[glossary-component-instance]: ../../glossary.md#component-instance
[glossary-component-instance-tree]: ../../glossary.md#component-instance-tree
[glossary-component-manager]: ../../glossary.md#component-manager
[glossary-component-manifest]: ../../glossary.md#component-manifest
[glossary-components-v1]: ../../glossary.md#components-v1
[glossary-components-v2]: ../../glossary.md#components-v2
[glossary-handle]: ../../glossary.md#handle
[glossary-job]: ../../glossary.md#job
[glossary-package]: ../../glossary.md#package
[glossary-process]: ../../glossary.md#process
[glossary-realm]: ../../glossary.md#realm
[glossary-session-framework]: ../../glossary.md#session
[wiki-component-based-software]: https://en.wikipedia.org/wiki/Component-based_software_engineering
[wiki-infosec]: https://en.wikipedia.org/wiki/Information_security
[wiki-least-privilege]: https://en.wikipedia.org/wiki/Principle_of_least_privilege
[wiki-object-composition]: https://en.wikipedia.org/wiki/Object_composition
[wiki-separation-of-concerns]: https://en.wikipedia.org/wiki/Separation_of_concerns
[wiki-software-framework]: https://en.wikipedia.org/wiki/Software_framework
