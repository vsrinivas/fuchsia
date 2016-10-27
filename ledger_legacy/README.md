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

Ledger is a distributed storage system for Fuchsia.

## Development

For development workflow, see [Developer Workflow](docs/workflow.md).

## Directory contents

Highlights: [mojom api](api), [documentation](docs).

Full contents:

 - [abax](abax) is a partial implementation of the Ledger interface, without
   persistance or sync
 - [api](api) is the Ledger interface
 - [cloud_provider](cloud_provider) encapsulates the features provided by the
   cloud that enable cloud sync
 - [convert](convert) is a helper type-conversion library
 - [docs](docs) contains documentation
 - [fake_network_service](fake_network_service) contains a fake network service
   implementation to be used in tests
 - [firebase](firebase) is a client for the REST api of Firebase Realtime
   Database
 - [gcs](gcs) is a client for Google Cloud Storage
 - [storage](storage) implements persistant representation of data held in
   Ledger
