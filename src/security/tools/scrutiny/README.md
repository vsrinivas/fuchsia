# Scrutiny
The Scrutiny Framework provides an extensible plugin based architecture
for building security auditing tools for Fuchsia. The primary goal is to have a
common base for these tools to prevent code duplication.

## Data Model
The core of the framework revolves around the `DataModel` and is split into
three categories:

1. DataCollectors: Objects that collect data from the host or target and
   populate the DataModel. These transform raw data from the target or host
   device into the abstract data model that `DataControllers` consume to
   analyze the system.
2. DataModel: This is an object relational mapping of Fuchsia. It provides a
   simple machine readable abstraction of the component topology along with
   other properties of the system state collected both statically and
   dynamically by the `DataCollectors`.
3. DataControllers: Objects that analyze and collate data from the `DataModel`.
   This is where the complex analysis of the `DataModel` occurs completely
   independent of how the underlying data is collected. This provides
   flexability and compatability even if the underlying system changes.

Source: `src/model` for the implementation of these three categories.

## Plugin Management
Plugins take a set of `DataCollectors` and `DataControllers` and register them
with the `PluginManager`. This is the primary interface you will work with
when adding new features to the framework. Currently plugins must be built into
the core binary.

A Scrutiny plugin has three core functions:
1. Provide a mapping between a namespace and a `DataController`.
2. Provide the set of `DataCollectors` required so that the `DataModel` is
   populated with all the information the `DataControllers` need to service
   queries.
3. Provide a set of dependencies this plugin requires to operate correctly.
   This allows developers to build lightweight plugins that build ontop of the
   work of other plugins.

The abstract interface defined in `src/engine/plugin.rs` allows the developer to
focus on developing the plugin and can benefit from a shared architecture for
integrating their plugin into both a REST service and a DataCollector worker pool.

## REST Service
The REST service provides a way for `DataVisualizers` to access `DataControllers`
over the network through REST JSON. This provides the greatest flexability as
`DataVisualizers` can be written in any language or framework as long as they
can communicate over the network to the Scrutiny service.

The REST service is populated automatically by the `PluginManager`. If a
`Plugin` is registered and loaded and provides a `PluginHook` for a
`DataController` mapped to the namespace "/foo/bar" the REST service will then
call that `DataController` when the URL "http://localhost:8080/foo/bar" is accessed.
It handles passing through the query string which is the HTTP body of the
request.

This allows the developer to just work on the plugin and take advantage of the
shared infrastructure for hooking into `DataVisualizers`.
