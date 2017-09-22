```
88                       88
88                       88
88                       88
88   ,adPPYba,   ,adPPYb,88   ,adPPYb,d8   ,adPPYba,  8b,dPPYba,
88  a8P_____88  a8"    `Y88  a8"    `Y88  a8P_____88  88P'   "Y8
88  8PP"""""""  8b       88  8b       88  8PP"""""""  88
88  "8b,   ,aa  "8a,   ,d88  "8a,   ,d88  "8b,   ,aa  88
88   `"Ybbd8"'   `"8bbdP"Y8   `"YbbdP"Y8   `"Ybbd8"'  88
                              aa,    ,88
                               "Y8bbdP"
```

# Ledger

[TOC]

## What is Ledger?

Ledger is a distributed storage system for Fuchsia.

Each application (or more precisely, each [component]) running on behalf of a
particular user has a separate data store provided and managed by Ledger, and
vended to a client application by Fuchsia [framework] through its [component
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

See [docs](docs/README.md) for documentation.

## Repository contents

 - [cloud_dashboard](cloud_dashboard) - a web dashboard for Ledger inspection 
 - [docs](docs) - documentation
 - [services](services) - FIDL API
 - [src](src) - implementation

[cloud_provider]: src/cloud_provider/public
[component]: https://fuchsia.googlesource.com/modular/+/master/services/component
[component context]: https://fuchsia.googlesource.com/modular/+/master/services/component/component_context.fidl
[framework]: https://fuchsia.googlesource.com/modular
