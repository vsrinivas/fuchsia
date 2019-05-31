# Token Manager

## Overview

Token Manager defines a common implementation of the auth.account.TokenManager
FIDL protocol and underlying database that may be used by both Token Manager
Factory and Account Handler.

Clients of this library must supply the path to use for the credential database
and an implementations of the `AuthProviderSupplier` trait that can supply
AuthProvider channels given an auth_provider_type. Each request to Token Manager
must also be associated with a `TokenManagerContext`.


## Key Dependencies

* */identity/lib/token_store* - Token Manager uses this crate to implement the
  database of long-lived credentials such as OAuth refresh tokens
* */identity/lib/token_cache* - Token Manager uses this crate to implement a
  cache of short-lived credentials such as OAuth access tokens
* *Auth Providers* - Token Manager communicates with components that implement
  the fuchsia.auth.AuthProvider FIDL protocol to establish and exchange
  credentials


## Design

`lib.rs` defines important types that clients of the library must supply:
* The `TokenManagerContext` struct defines the context that a particular request
  to the token manager was received in. This contains the url of the component
  using the TokenManager channel, and the client end of a
  fuchsia.context.AuthenticationContextProvider channel.
* The `AuthProviderSupplier` trait supplies the client end of a
  fuchsia.auth.AuthProvider channel given a particular `auth_provider_type`

`TokenManager` implements the fuchsia.auth.TokenManager FIDL protocol and may
be instantiated by clients of the library.

`TokenManagerError` defines an Error type implementing failure::Fail and
containing the most appropriate fuchsia.auth.Status to communicate the error
over FIDL.


## Future Work

Currently requests from different components are not isolated, i.e. the
TokenManagerContext is ignored. In the future it is likely that some isolation
will be introduced so that unrelated components from different vendors cannot
directly access each other's tokens. However, additional design work is required
to retain sharing in certain cases (e.g. between components from the same vendor
or between different vendors given explicit user consent).

The protocol between Token Manager and Auth Providers will be redesigned to
better enable additional authentication token types in the future.

