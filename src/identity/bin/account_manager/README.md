# Account Manager

## Overview

Account Manager is the core component of the user account system for Fuchsia.

The Account Manager maintains the set of Fuchsia accounts that are provisioned
on the device, launches and configures Authentication Provider components to
perform authentication via service providers, and launches and delegates to
Account Handler component instances to determine the detailed state and
authentication for each account.

Each device that supports Fuchsia accounts will have a single instance of the
Account Manager component, started on demand, that implements the discoverable
fuchsia.auth.account.AccountManager FIDL protocols and acts as the entry point
to the account system. Only a small number of core system components should
depend directly on Account Manager, the remaining components should only rely on
the least powerful protocol required to perform their role, typically
fuchsia.auth.account.Persona or fuchsia.auth.TokenManager.


## Key Dependencies

* */identity/bin/account_handler* - Account Manager launches a separate instance of
  the Account Handler component to handle the request for each Fuchsia account
* */identity/lib/account_common* - Account Manager uses error and identifier
  definitions from this crate
* */identity/lib/token_manager* - Account Manager uses the AuthProviderConnection
  struct to lazily launch and maintain connections to components implementing
  the fuchsia.auth.AuthProviderFactory FIDL protocol
* *Auth Providers* - Account Manager launches instances of components that
  implement the fuchsia.auth.AuthProviderFactory FIDL protocol. A single
  component is launched for each configured AuthProvider, serving all Fuchsia
  accounts


## Design

`AccountManager` implements the fuchsia.auth.account.AccountManager FIDL
protocol. The crate's main function creates a single instance of this struct
and uses it to handle all incoming requests.

`AccountManager` maintains a single `AccountHandlerContext` instance and a map
from the local identifiers of all accounts on the device to an associated
`AccountHandlerConnection`. These `AccountHandlerConnection` instances are
created lazily upon the first request pertaining to a particular account.

`AccountHandlerConnection` launches, initializes, and maintains a connection to
an instance of the Account Handler component.

`AccountHandlerContext` implements the
fuchsia.auth.account.internal.AccountHandlerContext FIDL protocol, using a map
from all configured auth_provider_type strings to an associated
`AuthProviderConnection`.


## Future Work

The Account Manager is not yet fully complete. In particular the change listener
protocols and local authentication state are not finished.

Currently the set of Auth Providers known to Account Manager is hard coded. In
the near future this will move to a config file based configuration that will
let different build configurations install different Auth Providers. In the
longer term we will support dynamic addition of Auth Providers at runtime.

