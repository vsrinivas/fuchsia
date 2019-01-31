# Modular FIDL API

## Public SDK Approved APIs

Modular has a large surface area which is undergoing significant change.
For this reason, not all FIDL services are approved for external use.
Here are the currently approved APIs:

* fuchsia.modular.Agent
* fuchsia.modular.PuppetMaster (with the exception of method `PuppetMaster.WatchSession()`).
* fuchsia.modular.StoryPuppetMaster

> NOTE: All `struct`s and `table`s assocated with the services above are
> implicitly included.