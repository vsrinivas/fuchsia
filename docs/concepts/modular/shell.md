## Shells

Shells are components which are responsible for composing UI. There are three
shells:

*   `BaseShell` displays UI associated with a device, prior to a session being
    started.
*   `SessionShell` displays the UI associated with a given session (e.g. list of
    stories, settings UI).
*   `StoryShell` displays a single story (i.e. the composition of the modules in
    a story, each story gets its own `StoryShell` instance).

### Environment

A shell is given access to two services provided by the modular framework in its
incoming namespace:

*   `fuchsia.modular.ComponentContext` gives the agent access to functionality
    which is shared across components run under the modular framework (e.g.
    modules, shells, agents).
*   `fuchsia.modular.[Base,Session,Story]ShellContext` gives access to shell
    specific functionality for each type of shell, respectively.

A shell is expected to provide two services to the modular framework in its
outgoing namespace:

*   `fuchsia.modular.[Base,Session,Story]Shell` the modular framework uses to
    communicate requests to display UI.
*   `fuchsia.modular.Lifecycle` allows the framework to signal the shell to
    terminate gracefully.

### Lifecycle

The three shells have varying lifecycles:

*   `BaseShell` runs between the time `basemgr` starts up until a session has
    been established, and on demand thereafter to faciliate authentication
    requests.
*   `SessionShell` runs for the duration of a session.
*   `StoryShell` runs while its associated story is running.
