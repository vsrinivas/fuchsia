# Account Manager

## Overview

Account Manager is the core component of the user account system for Fuchsia.

The Account Manager maintains the set of system accounts that are provisioned
on the device, launches and configures Authentication Provider components to
perform authentication via service providers, and launches and delegates to
Account Handler component instances to determine the detailed state and
authentication for each account.

Each device that supports system accounts will have a single instance of the
Account Manager component, started on demand, that implements the discoverable
fuchsia.identity.account.AccountManager FIDL protocols and acts as the entry
point to the account system. Only a small number of core system components
should depend directly on Account Manager, the remaining components should only
rely on the least powerful protocol required to perform their role, typically
fuchsia.identity.account.Persona or fuchsia.auth.TokenManager.


## Key Dependencies

* */identity/bin/account_handler* - Account Manager launches a separate instance
  of the Account Handler component to handle the requests for each system
  account
* */identity/lib/account_common* - Account Manager uses error and identifier
  definitions from this crate
* */identity/lib/token_manager* - Account Manager uses the AuthProviderConnection
  struct to lazily launch and maintain connections to components implementing
  the fuchsia.auth.AuthProvider FIDL protocol
* *Auth Providers* - Account Manager launches instances of components that
  implement the fuchsia.auth.AuthProvider FIDL protocol. A single component is
  launched for each configured AuthProvider, serving all system accounts


## Design

`AccountManager` implements the fuchsia.identity.account.AccountManager FIDL
protocol. The crate's main function creates a single instance of this struct
and uses it to handle all incoming requests.

`AccountManager` maintains a single `AccountHandlerContext` instance and an
`AccountMap` which contains the existing accounts and opens connections to them
on demand. New accounts are created by the AccountManager, and then the
`AccountHandlerConnection` is handed over to the `AccountMap`.

`AccountMap` is the source of truth for active accounts on the device, keyed by
local account id. It is responsible for storing the set of persistent accounts
to disk. It lazily estalishes `AccountHandlerConnection` instances upon the
first request pertaining to existing accounts.

The `AccountHandlerConnection` trait defines static- and instance methods for
intializing and maintaining a connection to an Account Handler component. It is
implemented by `AccountHandlerConnectionImpl` in business logic, where each
component instance is launched in a separate environment based on the local
account ID.

`AccountHandlerContext` implements the
fuchsia.identity.internal.AccountHandlerContext FIDL protocol, using a map
from all configured auth_provider_type strings to an associated
`AuthProviderConnection`.

`AccountEventEmitter` serves clients implementing the
fuchsia.identity.account.AccountListener FIDL protocol.


## Future Work

The Account Manager is not yet fully complete. In particular, local
authentication state is not finished.

Currently the set of Auth Providers known to Account Manager is hard coded. In
the near future this will move to a config file based configuration that will
let different build configurations install different Auth Providers. In the
longer term we will support dynamic addition of Auth Providers at runtime.

When component framework V2 is available, the lifecycle of component instances
launched by AccountManager is likely to be managed in a different way.
