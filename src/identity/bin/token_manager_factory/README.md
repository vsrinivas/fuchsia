# Token Manager Factory

## Overview

Token Manager Factory is a component used to manage authentication tokens for
Fuchsia. It maintains databases containing long-lived tokens for sets of service
provider accounts, where each database relates to a single user of the device
where the notion of user is defined by the client of Token Manager Factory.

In most Fuchsia configurations authentication tokens are managed through Account
Manager rather than Token Manager Factory, but Token Manager Factory is still
required for some fallback cases.


## Key Dependencies

* */identity/lib/token_manager* - Token Manager Factory uses the TokenManager
  library to perform its authentication token management and implement the
  fuchsia.auth.TokenManager FIDL protocol
* *Auth Providers* - Token Manager Factory launches instances of components that
  implement the fuchsia.auth.AuthProvider FIDL protocol. A single component is
  launched for each configured AuthProvider, serving all users


## Design

`TokenManagerFactory` implements the fuchsia.auth.TokenManagerFactory FIDL
protocol. The crate's main function creates a single instance of this struct
and uses it to handle all incoming requests.  `TokenManagerFactory` maintains a
single instance of `AuthProviderSupplier` and a map from user to instances of
`token_manager::TokenManager`, creating new entries in the map on the first
request for each user.

`AuthProviderSupplier` implements the `token_manager::AuthProviderSupplier`
trait, maintaining a map from auth_provider_type to instances of
`token_manager::AuthProviderConnection` in order to lazily launch auth provider
components on first request.


## Future Work

Once the current fallback cases requiring Token Manager Factory have been
resolved to use Account Manager, Token Manager Factory will either be removed
entirely or downscoped to remove the multi-user support and retained for devices
that do not need the full multi-user Fuchsia account system.

Currently the set of Auth Providers known to Token Manager Factory must be
passed by the caller in every request to create a Token Manager channel and
these must be consistent across calls. If Token Manager Factory is not removed
entirely, in the future it will move to a config file based configuration that
will let different build configurations install different Auth Providers and
remove the need for clients to supply configuration. In the longer term we will
support dynamic addition of Auth Providers at runtime.
