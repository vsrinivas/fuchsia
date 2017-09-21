Fuchsia Component Manager
=========================

The component manager provides support for discovering, inspecting and invoking
_components_.

Components
==========

Components are a mechanism for associating mojo apps with metadata and
resources.  Components are referred to by URLs.

Eventually the component system will provide packaging and signing. Currently
the URL of a component is the URL of its manifest. Eventually it may be the URL
a signed package containing its manifest.

Component Manifests
-------------------

Component manifets are JSON dictionaries. Each dictionary entry represents a
component facet.  The key is the facet name and the value is the facet data.

Facets describe a component's capabilities and interface. Facet names are URLs.
Facets defined by Fuchsia itself have the form `fucsia:...` but developers can
declare their own facets.

`fuchsia:component` Facet
-------------------------

The `fuchsia:component` facet declares some basic metadata about a component.
`fuchsia:component` facet data will be a dictionary. A `name` key with a string
value declares the name of the component. More fields will be defined later.

`fuchsia:program` Facet
-----------------------

The `fuchsia:program` facet describes the mojo application associated with the
component. Its data is a dictionary containing an `url` entry wih the location
(relative to the component) of the mojo executable.

`fuchsia:resources` Facet
-------------------------

The `fuchsia:resources` facet lists and describes resources that are needed by a
component. It's currently just a list of URLs, relative to the component.

Component Manager
=================

The component manager is a mojo application that implements the
[ComponentManager](../services/component/component.fidl) interface. It can:
 * get the manifest of a component (by URL)
 * invoke the application described in the `fuchsia:program` facet of a
   component (by URL) It will allow:
 * accessing the resources of a component
 * querying which components match specific data patterns

Build and Deploy
================

Eventually components will be fetched off the network. We don't have a network
yet. Currently components and their resources are stored under
`/boot/components` in a pattern representing their URL.

Future Work
===========

Future improvements and extensions include:
 * Better build system integration.
 * Fetching from the network, deploying to the network.
 * Packaging components and signing packages.
 * Scoping resources (and perhaps other metadata) to specific architectures (eg:
   for arm64 vs amd64 binaries), device properties (screen resolution, etc) and
   user properties (locale, accessibility).
