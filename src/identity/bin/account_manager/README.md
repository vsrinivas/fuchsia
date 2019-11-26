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
fuchsia.identity.account.AccountManager FIDL protocols and acts as the entry point
to the account system. Only a small number of core system components should
depend directly on Account Manager, the remaining components should only rely on
the least powerful protocol required to perform their role, typically
fuchsia.identity.account.Persona or fuchsia.auth.TokenManager.


## Key Dependencies

* */identity/bin/account_handler* - Account Manager launches a separate instance of
  the Account Handler component to handle the request for each Fuchsia account
* */identity/lib/account_common* - Account Manager uses error and identifier
  definitions from this crate
* */identity/lib/token_manager* - Account Manager uses the AuthProviderConnection
  struct to lazily launch and maintain connections to components implementing
  the fuchsia.auth.AuthProvider FIDL protocol
* *Auth Providers* - Account Manager launches instances of components that
  implement the fuchsia.auth.AuthProvider FIDL protocol. A single component is
  launched for each configured AuthProvider, serving all Fuchsia accounts
* *Overnet* If prototype account transfer is enabled, Overnet is used as the
  mechanism by which the source and target devices communicate.


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


### Prototype Account Transfer

Account Manager also contains a prototype mechanism for provisioning an account
that exists on some Fuchsia device onto another Fuchsia device. Account
transfer is disabled by default and can be enabled by passing
`--args="prototype_account_transfer=true"` to `fx set`.

When account transfer is enabled, the main function takes two additional
actions:
* Creates a single instance of `AccountManagerPeer` and uses it to serve
connections from Account Managers on other Fuchsia devices through Overnet
* Creates a single instance of `AccountTransferControl` and uses it to handle
account transfer requests

The entry point for triggering an account transfer is the
fuchsia.identity.prototype.PrototypeAccountTransferControl FIDL protocol, which
Account Manager publishes to its out/debug directory.  It is accessible by
inspecting Account Manager's output directory via the Hub.

`AccountManagerPeer` implements the
fuchsia.identity.transfer.AccountManagerPeer FIDL protocol, and is responsible
for instantiating and initializing an Account Handler for a transferred account
on the target device.

`AccountTransferControl` implements the
fuchsia.identity.prototype.PrototypeAccountTransferControl FIDL protocol, and
is responsible for connecting to an Account Manager on a remote device and
orchestrating the account transfer to it.


## Future Work

The Account Manager is not yet fully complete. In particular, local
authentication state is not finished.

Currently the set of Auth Providers known to Account Manager is hard coded. In
the near future this will move to a config file based configuration that will
let different build configurations install different Auth Providers. In the
longer term we will support dynamic addition of Auth Providers at runtime.

When component framework V2 is available, the lifecycle of component instances
launched by AccountManager is likely to be managed in a different way.
