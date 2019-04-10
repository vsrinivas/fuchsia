# Token Store

## Overview

Token Store provides an on-disk database for long-lived authentication tokens
such as OAuth refresh tokens and credential keys. It is used by the
Token Manager library.


## Key Dependencies

None


## Design

Although the database is currently stored as a JSON file on disk, the design of
the crate is modular to enable an in-memory database during testing and to
enable the use of alternative serialization formats in the future.

The database is modelled as a key-value store. `lib.rs` defines `CredentialKey`
(composed of auth_provider_type and user_profile_id), and `CredentialValue` as
the key and value types, plus an `AuthDb` trait that all database
implementations must implement.

`serializer.rs` defines a `Serializer` trait and a single `JsonSerializer`
implementation of this trait that relies on the Rust Serde library to create and
parse JSON authentication databases.

`file.rs` defines an `AuthDbFile` implementation of `AuthDb`. This is generic
over its serialization and an implementation of `Serializer` must be supplied at
construction.


## Future Work

No significant future work is currently planned in this crate.

The store is a simple crate that is unlikely to ever be used outside of Token
Manager. Potentially it will be merged into Token Manager at some point.

