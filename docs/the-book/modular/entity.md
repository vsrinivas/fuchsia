## Entities

The Fuchsia entity model facilitates information interchange by defining a
common interface for _describing_, _referencing_, _accessing_, and _mutating_
data objects (entities) which are shared between components (modules, agents,
shells) running in the modular framework. It consists of the following major
concepts:

*   Entities: the data objects shared between components.
*   Entity references: a serializable token which can be used to retrieve an
    `Entity` handle.
*   `Entity`: An interface which gives access to a shared data object.
*   `EntityProvider`: the interface which allows agents to expose entities to
    the system.
*   `EntityResolver`: the Fuchsia API for requesting an `Entity` handle for a
    given reference.

### Lifecycle

The lifecycle of the data backing an `Entity` is controlled by the
`EntityProvider`. The lifecycle of the `Entity` handle is controlled by
`sessionmgr`.

#### Entities owned by Agents

Agents don't create `Entity` handles directly. Agents connect to the
`EntityReferenceFactory` which provides the agent with an entity reference in
exchange for a `cookie`. The entity reference can then be shared with other
modular components which can use their `EntityResolver` to dereference it into
an `Entity` interface.

Calls on that `Entity` interface will then be forwarded to the agent, along with
the associated cookie.

The agent is thus responsible for storing and providing the entity data,
associating it with the correct cookie, and optionally handling requests for
mutating the entity data.

#### Entities owned by a Story

Modules can create entities explicitly via their `ModuleContext` by providing a
`fuchsia.mem.Buffer` and an associated type. The framework manages the lifecycle
of such entities by storing them in the story's record. For this reason, when
the story is deleted, so is the entity. Agents and modules outside the story can
dereference the entity so long as the story still exists.

#### Entity Resolution

This section describes the internals of entity resolution.

`EntityProviderRunner` implements the `fuchsia::modular::EntityResolver`
interface, and is also responsible for creating entity references by
implementing the `fuchsia::modular::EntityReferenceFactory` interface. A single
instance of the `EntityProviderRunner` manages all the entity providers running
in the system.

The first step in entity resolution (i.e. the first thing which happens when
`ResolveEntity` is called) is the runner determines whether the entity is
provided by an agent or by the modular framework by inspecting the entity
reference. The runner then asks an `EntityProviderLauncher` to launch the
appropriate entity provider.

If the entity provider is an agent, an `AgentController` is passed to the
launcher, and the runner keeps the agent controller alive until the client
closes the `Entity`.

Each `Entity` request has an associated `EntityController` which the entity
runner owns. The `EntityController` owns the `AgentController` if the entity
provider was an agent, and is responsible for forwarding the entity interface
methods to the entity provider.

### Read More

* [API Guide](guide/entity_provider.md)
