# Component Topology

The component topology is an abstract data structure that represents the
relationships among [component instances](#component-instances).

It is made of the following:

- Component instance tree: Describes how component instances are
  [composed](#composition) together (their parent-child relationships).
- Capability routing graph: Describes how component instances gain access to
  use capabilities exposed by other component instances (their
  provider-consumer relationships).

The structure of the component topology greatly influences how components
run and obtain capabilities.

## Component Instances {#component-instances}

A component instance is a distinct embodiment of a component running in its own
sandbox that is isolated from other instances of the same component and of
other components.

You can often use the terms component and component instance interchangeably
when the context is clear. For example, it would be more precise to talk about
"starting a component instance" rather than "starting a component" but you
can easily infer that "starting a component" requires an instance of that
component to be created first so that the instance can be started.

<br>![Diagram of component instances](images/topology_instances.png)<br>

## Component Instance Tree {#composition}

The component instance tree expresses how components are assembled together
to make more complex components.

Using hierarchical composition, a parent component creates instances of other
components which are known as its children. The newly created children belong
to the parent and are dependent upon the parent to provide them with the
capabilities that they need to run. Meanwhile, the parent gains access to the
capabilities exposed by its children through
[capability routing](#capability-routing).

Children can be created in two ways:

- Statically: The parent declares the existence of the child in its own
  [component declaration][doc-component-declaration]. The child is destroyed
  automatically if the child declaration is removed in an updated version of
  the parent's software.
- Dynamically: The parent uses [realm services][doc-realms] to add
  a child to a [component collection][doc-collections] that the parent declared.
  The parent destroys the child in a similar manner.

The component topology represents the structure of these parent-child
relationships as a [component instance tree][glossary-component-instance-tree].

<br>![Diagram of component instance tree](images/topology_instance_tree.png)<br>

## Monikers {#monikers}

A moniker identifies a specific component instance in the component tree
using a topological path. Monikers are collected in system logs and for
persistence.

See the [monikers documentation][doc-monikers] for details.

<br>![Diagram of component instance child monikers](images/topology_child_monikers.png)<br>

## Encapsulation {#encapsulation}

The capabilities of child components cannot be directly accessed outside of the
scope of their parent; they are fully encapsulated.

Children remain forever dependent upon their parent; they cannot be reparented
and they cannot outlive their parent. When a parent is destroyed so are all
of its children.

This model resembles [composition][wiki-object-composition]{:.external} in object-oriented
programming languages.

<br>![Diagram of component instance encapsulation](images/topology_encapsulation.png)<br>

## Realms {#realms}

A realm is a subtree of component instances formed by
[hierarchical composition](#composition). Each realm is rooted by a component
instance and includes all of that instance's children and their descendants.

Realms are important [encapsulation](#encapsulation) boundaries in the
component topology. The root of each realm receives certain privileges to
influence the behavior of components, such as:

- Declaring how capabilities flow into, out of, and within the realm.
- Binding to child components to access their services.
- Creating and destroying child components.

Certain behavior of a realm can be configured through its [environment][doc-environments].

See the [realms documentation][doc-realms] for more information.

<br>![Diagram of component instance realms](images/topology_realms.png)<br>

## Capability Routing Graph {#capability-routing}

The capability routing graph describes how components gain access to use
capabilities exposed and offered by other components in the component topology.

See the [capability routing documentation][doc-capability-routing]
for more information.

<br>![Diagram of component instance realms](images/topology_capability_routing.png)<br>

[doc-collections]: /docs/concepts/components/v2/realms.md#collections
[doc-environments]: /docs/concepts/components/v2/environments.md
[doc-manifests]: /docs/concepts/components/v2/component_manifests.md
[doc-realms]: /docs/concepts/components/v2/realms.md
[doc-monikers]: /docs/concepts/components/v2/monikers.md
[doc-capability-routing]: /docs/concepts/components/v2/component_manifests.md#capability-routing
[doc-component-declaration]: /docs/concepts/components/v2/declarations.md
[glossary-component-instance-tree]: /docs/glossary.md#component-instance-tree
[wiki-least-privilege]: https://en.wikipedia.org/wiki/Principle_of_least_privilege
[wiki-object-composition]: https://en.wikipedia.org/wiki/Object_composition
