# AppDriver

App driver is a small framework that supports life cycle management of fidl
components by implementing the Lifecycle FIDL service.

There are three flavors of app driver: for generic components (AppDriver), for
components that are run as Modules (ModuleDriver), and for components that are
run as Agents (AgentDriver). Agents and Modules have additional facets at
initialization in addition to the asynchronous teardown sequence that Lifecycle
imposes. See the .h files for details.



