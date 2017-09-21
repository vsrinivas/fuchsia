Module
======

A module is an application that implements the Module interface, whose lifecycle
is tightly bound to the story in which it was started and may implement a UI. A
module is started in a story by another module or by the system from which it
receives a Link service . It can also receive / provide other services via
ServiceProviders. A module is terminated if the story in which it is running
becomes inactive, or the module that started it decides to terminate it or it
decides to terminate itself. A module can start other modules, create,
send / receive messages and call FIDL interfaces.

## See also:
[Module](../services/module/module.fidl)
[ModuleContext](../services/module/module_context.fidl) (formerly Story)
[ModuleController](../services/module/module_controller.fidl)
[Link](../services/story/link.fidl)
[MessageQueue](../services/component/message_queue.fidl)
