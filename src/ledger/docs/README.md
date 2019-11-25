# Ledger

[TOC]

## What is Ledger?

Ledger is a distributed storage system for Fuchsia.

Each application (or more precisely, each [component]) running on behalf of a
particular user has a separate data store provided and managed by Ledger, and
vended to a client application by Fuchsia framework through its [component
context].

The data store for the particular component/user combination is private - not
accessible to other apps of the same user, and not accessible to other users of
the same app.

Each data store is transparently **synchronized** across devices of its user
through a [cloud provider]. Any data operations are made **offline-first** with
no coordination with the cloud. If concurrent modifications result in a data
conflict, the conflict is resolved using an app-configurable merge policy.

Each data store is organized into collections exposing a **key-value store** API
called *pages*. Page API supports storing data of arbitrary size, atomic changes
across multiple keys, snapshots and modification observers.

## When should Ledger be used?

Ledger should be used by software storing data that is scoped to a single user,
and that needs to be synced on all of the users's devices.
Ledger should also be used to store data that needs to be restored after a user
resets a device.

The exception is if the data needs to be processed in the cloud or on a device
not owned by the user. Because Ledger transfers opaque encrypted data, the data
would need to be exchanged via a different channel.

There is a computation and disk space cost to storing data in Ledger, so if the
data does not need to be persisted and synced, do not use Ledger. For example,
Ledger is not the optimal for local caches.

## Ledger availability

Ledger is available to any software running under the [Modular] framework.

## Documentation

Documentation for using Ledger:

 - [User Guide](user_guide.md)

Documentation for integrating with Ledger in client apps:

 - [API Guide](api_guide.md)
 - [Data Organization](data_organization.md)
 - [Examples](examples.md)

Documentation for developing Ledger:

 - [C++ in Ledger](cpp.md)
 - [Field Data](field_data.md)
 - [Style Guide](style_guide.md)
 - [Inspection](inspection.md)
 - [Testing](testing.md)

Design documentation:

 - [Architecture](architecture.md)
 - [Conflict Resolution](conflict_resolution.md)
 - [Data in Storage](data_in_storage.md)
 - [Life of a Put](life_of_a_put.md)

[cloud provider]: /peridot/public/fidl/fuchsia.ledger.cloud/cloud_provider.fidl
[component context]: /peridot/public/fidl/fuchsia.modular/component/component_context.fidl
[Modular]: /peridot/docs/modular/overview.md
