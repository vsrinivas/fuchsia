# Token Manager Factory

## Overview

Token Manager Factory is the component currently used to manage authentication
tokens for Fuchsia. It maintains databases containing long-lived tokens for
sets of service provider accounts, where each database relates to a single user
of the device where the notion of user is defined by the client of Token Manager
Factory.

A single instance of the Token Manager Factory component is launched by Basemgr,
from which a set of Token Manager channels are created for use by different
components within Peridot.

The ongoing refactor of Peridot will include a switch from using Token Manager
Factory to using Account Manager. Even after this transition Token Manager
Factory may still serve a purpose on devices that do not support the full
Fuchsia account system.


## Key Dependencies

* */identity/lib/token_manager* - Token Manager Factory uses the TokenManager
  library to perform its authentication token management and implement the
  fuchsia.auth.TokenManager FIDL protocol
* *Auth Providers* - Token Manager Factory launches instances of components that
  implement the fuchsia.auth.AuthProviderFactory FIDL protocol. A single
  component is launched for each configured AuthProvider, serving all users


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

Account Manager is the replacement user identity system for Fuchsia. Once this
is complete and Peridot has been migrated from Token Manager Factory to Account
Manager, the role of Token Manager Factory will change. Design is still ongoing
here, but it is likely that Token Manager Factory will be downscoped to remove
the multi-user support and retained for devices that do not need the full
multi-user Fuchsia account system.

Currently the set of Auth Providers known to Token Manager Factory must be
passed by the caller in every request to create a Token Manager channel and
these must be consistent across calls. The need for this configuration knowledge
significantly constrained how Token Manager Factory was integrated in Peridot.
In the future we will move to a config file based configuration that will
let different build configurations install different Auth Providers and remove
the need for clients to supply configuration.  In the longer term we will
support dynamic addition of Auth Providers at runtime.
