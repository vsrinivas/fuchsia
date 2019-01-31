## Stories

A story is a logical container for composing a set of modules.

Stories and their associated state are stored in the user's Ledger.

### Presentation

The modular framework uses the `fuchsia.modular.StoryShell` interface to display
the UI for stories.

### Lifecycle

Stories can be created, deleted, started, and stopped. Created and deleted refer
to the existence of the story in the ledger, whereas started and stopped refer
to whether or not the story and its associated modules are currently running.
