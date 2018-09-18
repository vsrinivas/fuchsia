Module Resolution
===

Resolution is the process by which a Fuchsia [`fuchsia::modular::Intent`](intent.md), which
represents an abstract or loosely specified action, is expanded into a set of
concrete Module implementations for execution and ranked by relevance to a
provided context.

Resolution is provided indirectly by the Modular Framework through calls on
either the [`fuchsia::modular::ModuleContext`](../../public/lib/module/fidl/module_context.fidl)
(for Module clients) or the
[`fuchsia::modular::StoryController`](../../public/lib/story/fidl/story_controller.fidl) (for
privileged platform clients). Specifically, clients would call either
`fuchsia::modular::ModuleContext.StartModule()` or `fuchsia::modular::StoryController.fuchsia::modular::AddModule()`.

This document outlines what happens behind the scenes of those two calls.

## The fuchsia::modular::ModuleResolver
The
[`fuchsia::modular::ModuleResolver`](../../public/lib/module_resolver/fidl/module_resolver.fidl)
is the FIDL service that provides Module Resolution to its clients. It is only
accessible directly by the Framework and other privileged platform components.
Nonetheless, the process is fundamental to the user experience in Fuchsia and
warrants its own public documentation.

### Inputs
TODO

* A `fuchsia::modular::Intent`: defines `Module` constraints based on desired action and/or
  instances of runtime data.
* A `ScoringInfo` struct: informs the fuchsia::modular::ModuleResolver on how to score and rank
  the results, including the scope of context signals that may affect ranking.

### Outputs
TODO

A ranked list of `fuchsia::modular::ModuleResolverResult`. Contains all the data necessary to
initialize a specific Module instance backed by an executable.


### Resolution Steps

#### 1. Noun translation/extraction
TODO
#### 2. Retrieval
TODO
#### 3. Filtering
TODO
#### 4. Ranking
TODO
