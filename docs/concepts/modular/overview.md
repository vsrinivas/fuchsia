Warning: Modular was an experimental application framework for Fuchsia. Its
functionality is being ported to the Session Framework and the [Component
Framework](/docs/concepts/components).

The original documentation for Modular follows.

## Overview

Modular manages user experiences by composing UI, data, and users from a
diverse set of components into logical and visual containers called Stories.

The framework defines classes of components to extend user experiences and
provides software primitives for component composition, communication, task
delegation, state management and data sharing.

### Requirements to use Modular

Modular supports software written in any language (e.g. Flutter, C++) for any
Fuchsia supported runtime, as long as it is a Fuchsia Component.

The Modular Framework communicates with components it launches via FIDL, the
standard IPC mechanism for Fuchsia.

### Extension Points

The framework defines several different classes of components which can be
implemented by developers to extend the behavior of user experiences:

1.  [Modules](module.md) are components which display UI and are visually
    composed in a [Story](story.md).
1.  [Agents](agent.md) are components which run in the background to provide
    services and data to Modules and other Agents.
1.  [Shells](shell.md) manage system UI and mediate user interactions.
1.  [EntityProviders](entity.md) are components which provide access to data
    object (entities) which are shared between components running in modular.

### `basemgr` and `sessionmgr`

After Fuchsia device startup, `basemgr` and `sessionmgr` are processes that
provide session management, component lifecycle management and state management.

*   `basemgr` is responsible for user authentication and
    authorization. It leverages the Base Shell to present UI.

*   `sessionmgr` is responsible for the lifecycle of Stories,
    Modules and Agents, as well as service and state coordination between them.
    It leverages Session and Story Shells to manage the visual composition of
    these components.

### Read More

* [Configuring Modular](guide/config.md)
