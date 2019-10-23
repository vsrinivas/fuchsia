## Modules

A `Module` is a component which displays UI and runs as part of a `Story`.

Multiple modules can be composed into a single story, and modules can add other
modules to the story they are part of. Module's can either embed other modules
within their own content, or they can delegate visual composition to the
`StoryShell`.

### Environment

A module is given access to two services provided by the modular framework in
its incoming namespace:

*   `fuchsia.modular.ComponentContext` which gives the agent access to
    functionality which is shared across components run under the modular
    framework (e.g. modules, shells, agents).
*   `fuchsia.modular.ModuleContext` which gives modules access to module
    specific functionality, like adding other modules to its story and creating
    entities.

A module is expected to provide three services to the modular framework in its
outgoing namespace:

*   `fuchsia.ui.app.ViewProvider` which is used to display the module's UI.
*   `fuchsia.modular.Lifecycle` which allows the framework to signal the module
    to terminate gracefully.
*   `fuchsia.modular.IntentHandler` which allows the framework to send
    [intents](intent.md) to the module.

### Lifecycle

A module's lifecycle is bound to the lifecycle of the story it is part of. In
addition, a given module can have multiple running instances in a single story.

When a module starts another module it is given a module controller which it can
use to control the lifecycle of the started module.

### Communication Mechanisms

Modules communicate with other modules via intents and entities, and with agents
via FIDL and message queues.
