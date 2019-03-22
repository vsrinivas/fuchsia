# Guide to the Entity APIs

This document is a guide for using the Entity APIs. See the main
[entity](../entity.md) document for a conceptual overview.

## What is an Entity?
Conceptually, an Entity is a blob of data, accompanied with a type, that Agents
can manufacture and pass to other components (modules and agents). The data
behind an Entity is owned by the Agent that created it, and the Agent is
responsible for serving an Entity’s data when a component tries to access it.

Entities are the primary mechanism by which components share semantic data with
each other.  For instance, a Google Contacts agent could manufacture an entity
describing a contact by tagging the entity with a type com.fuchsia.Contact and
passing it around to other components that want an Entity of type
com.fuchsia.Contact. When a component requests data out of the Entity, the
framework will bring up the agent that created the Entity and ask it to provide
the data for it. This means that agents ultimately own and supply the data
backing an Entity.

## How does an Agent make an Entity?
An Agent can create an Entity by calling
`AgentContext.GetReferenceFactory().CreateReference(cookie)` and supplying it a
`cookie` id. This cookie is how the Agent will identify this particular Entity.
For instance, `joe@domain.com` could be the cookie for a Contact whose name is
Joe. When this call is made, the framework will return an opaque, persistable
string reference meant for this Entity. This reference is used by a component to
get the data out of the Entity. Entity references can be persisted to disk, the
ledger, passed around to other components which may also do the same. Note that
because these references may be persisted to the ledger, it means that you can
create an Entity on one device, but dereference it on another device to get the
data out.

C++ example snippet of an Agent creating an entity.
```
  auto startup_context = sys::ComponentContext::Create();
  auto agent_ctx = startup_context->svc()
        ->Connect<fuchsia::modular::AgentContext>();

  fuchsia::modular::EntityReferenceFactory factory;
  agent_ctx->GetEntityReferenceFactory(factory.NewRequest());
  factory->CreateReference("iamaperson@google.com", [] (std::string entity_reference) {
    // Pass the |entity_reference| to a Module or Agent for consumption.
  });
```

## How does a Module make an Entity?
A module can make an Entity by invoking `ModuleContext.CreateEntity()` with a
type and data. This entity's data will then be the framework and its reference
is valid for the duration for the Story; once the Story is deleted, all
entity references manufactured by its modules also become invalid.

C++ example snippet of a Module creating an entity.
```
  auto startup_context = sys::ComponentContext::Create();
  auto module_ctx = startup_context->svc()
        ->Connect<fuchsia::modular::ModuleContext>();

  fuchsia::mem::Buffer data;
  fsl::StringFromVmo("iamaperson@google.com", &data);
  module_ctx->CreateEntity("com.fuchsia.Contact", std::move(data).ToTransport(),
                           entity.NewRequest(), [] (std::string entity_reference) {
    // Pass the |entity_reference| to a Module or Agent for consumption.
  });
```

## How does a Module or Agent get data out of an Entity?
A component can get data out of an entity reference by first resolving the
reference into an `Entity` interface, and then requesting data from it. It may do
so by calling `ComponentContext.GetEntityResolver().ResolveEntity(reference)`
which will give you an `Entity` interface. You can then get the supported types or
data out of the Entity by calling `Entity.GetTypes()` or
`Entity.GetData(typename)`, respectively.

C++ Example snippet getting data out of an `entity_reference`:
```
  auto startup_context = sys::ComponentContext::Create();
  auto component_ctx = startup_context->svc()
        ->Connect<fuchsia::modular::ComponentContext>();

  fuchsia::modular::EntityResolverPtr resolver;
  fuchsia::modular::EntityPtr entity;
  component_ctx->GetEntityResolver(resolver.NewRequest());

  resolver->ResolveEntity(entity_reference, entity.NewRequest());
  entity->GetData("com.fuchsia.Contact", [] (fuchsia::mem::BufferPtr data) {
      // ...
  });
```

## How does an Agent provide data for an Entity?
An agent provides data for an Entity it created by implementing and exposing the
`EntityProvider` interface to the framework. When a component calls
`Entity.GetData(type)`, the framework will first bring up the agent that created
the entity reference and calls the corresponding `EntityProvider.GetData(cookie,
type)`. The cookie supplied here is the same cookie that the Agent was supplied
in its `EntityReferenceFactory.CreateReference()` call.
