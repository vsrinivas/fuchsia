Entities
====
> Status: DRAFT

The Fuchsia entity model facilitates information interchange by defining a
common interface for **describing**, **referencing**, and **accessing** data
objects (entities) which are shared between components running on the Fuchsia
platform.  It consists of the following major concepts:

* [entities](../services/entity/entity.fidl): the data objects described by the
  model
* entity types: the way different kinds of entities are distinguished
* [entity references](../services/entity/entity_reference.fidl): a serializable
  token that can be used to retrieve an entity handle
* entity providers (TODO): the extensible means by which entities are made
  available to the system and to other components independent of their source
* [entity resolver](../services/entity/entity_resolver.fidl): a Fuchsia API for
  accessing an entity given a reference

Here are some ways entities can be used:

* copy/paste via the clipboard
* describing a contact card
* attaching images and files to an email
* providing a programmatic interface to create, view, and change calendar
  events independent of any specific calendar provider
* decoupling presentation components from data sources
* indicating the focused elements of a story and persisting them across
  restarts
* enabling the assistant to inspect and manipulate entities present in the
  current context or seen in the past

### What are Entities?

An entity is an **identifiable** person, place, thing, event, or concept which
is represented within the Fuchsia platform as a **structured data object**
which can be **referenced**, **retrieved**, **presented**, **manipulated**, or
**shared**.

This use of the term “entity” has parallels in the fields of
[databases](https://en.wikipedia.org/wiki/Entity%E2%80%93relationship_model),
[information extraction](https://en.wikipedia.org/wiki/Named_entity), and
[ontology](https://en.wikipedia.org/wiki/Ontology_(information_science)).

Fuchsia entities have the following characteristics:

* **types**: indicate the representations and intended interpretations of the
  entity’s content, often denoting a particular schema
* **content**: data which can be retrieved from the entity, often in the form
  of structured properties or binary data as required by the entity’s type
* **provider** (TODO): the provider of an entity is the component through which
  the content of the entity is accessed
* **reference**: a token which is used to locate the entity provider from which
  the content of the entity can be retrieved (or modified through actions)

Fuchsia entities come in two flavors:

* **transient entities**: represent objects which exist only at runtime as a
  means of transmitting structured data, examples:
  - a paragraph of text which has been copied to the clipboard and which
    retains no connection to its original source
  - a phone number which has been extracted from content
  - an image which has been captured but not saved anywhere
* **persistent entities**: represent objects for which a durable record is
  being maintained by an entity provider, examples:
  - an email thread
  - a photo in the camera roll or in the cloud
  - a contact card
  - a calendar event

### How to create an Entity

It is currently possible to create *transient entities* by using the
[`EntityStore`](../services/entity/entity_store.fidl) available in the
application namespace for both `Modules` and `Agents`.

`EntityStore` can create transient Entities from a list of **types** and
**content** arrays.

In Dart:

```dart
final EntityStoreProxy entityStore = new EntityStoreProxy();
connectToService(context.environmentServices, entityStore.ctrl);
final myType = <unique type identifier>;
final myContent = <uint8 array>;
entityStore.createEntity([myType], [myContent], (final EntityProxy entity) {
  // Do something with |entity|
});
```

In C++:
TODO

### How to retrieve an Entity

If you have an `EntityReference` (which is available by calling
`Entity.getReference()` on any `Entity` instance), you can retrieve a handle to
it by using the `EntityResolver` service available in the application
namespace. Both `Modules` and `Agents` have this service available.

In Dart:

```dart
final EntityProxy entity = new EntityProxy();
final EntityResolverProxy entityResolver = new EntityResolverProxy();
connectToService(context.environmentServices, entityResolver.ctrl);
entityResolver.getEntity(entityReference, entity.ctrl);
// Do something with |entity|
```

In C++:
TODO