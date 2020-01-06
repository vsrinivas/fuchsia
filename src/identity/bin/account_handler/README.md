# Account Handler

## Overview

Account Handler manages the state of a single Fuchsia account (and its personae)
on a Fuchsia device. It also provides access to authentication tokens for the
Service Provider accounts associated with the Fuchsia account.

Account Handler component instances come in two variants, one for persistent,
and one for ephemeral accounts. Both are launched by Account Manager and
implement the fuchsia.identity.internal.AccountHandlerControl FIDL protocols
so they may be controlled by Account Manager. Each Account Handler component
instance is responsible for handling a single Fuchsia account. A persistent
account handler only accesses the persistent storage for that one account.
An ephemeral account handler has no persistent storage priveleges at all.

Account Handler also implements the fuchsia.identity.account.Account and
fuchsia.identity.account.Persona FIDL protocols. These protocols are not
discoverable; Account channels may only be obtained through Account Manager and
Persona channels may only be obtained from an Account channel.


## Key Dependencies

* */identity/lib/account_common* - Account Handler uses error and identifier
  definitions from this crate
* */identity/lib/identity_common* - Account Handler uses the TaskGroup type from
  this crate
* */identity/lib/token_manager* - Account Manager uses the TokenManager library
  to perform the authentication token management and implement the
  fuchsia.auth.TokenManager FIDL protocol
* *fuchsia.stash.Store* - The persistent Account Handler uses stash to store
  pre-authentication data such as what authentication mechanisms are enrolled.


## Design

`AccountHandler` implements the
fuchsia.identity.internal.AccountHandlerControl FIDL protocol. The crate's
main function creates a single instance of `AccountHandler` and uses it to
handle all incoming requests. An `AccountHandlerContext` is supplied in
Account Handler's namespace as it is launched. The `LocalAccountId` is also
supplied to the Account Handler at launch time, but is passed through a flag
instead, and parsed by in the main program entry point.

The AccountHandlerControl protocol drives a state machine in the Account
Handler, starting in an `Uninitialized` state. Once an account is unlocked or
created, the `AccountHandler` is considered `Initialized`. When initialized, the
`Account` serves subsequent `AccountHandlerControl.GetAccount` calls. Notably,
the `AccountHandler` can also be in the `Locked` state, where the `Account`
is not available. Unlocking may involve an authentication attempt if the
account was enrolled with an authentication mechanism upon creation.

`Account` implements the fuchsia.identity.account.Account FIDL protocol and
stores an instance of the `Persona` struct representing the default Persona.
When a persistent `Account` is constructed, it manages a database file using a
`StoredAccount`. The `Account` also stores an instance of `TokenManager`,
supplied with a path to the token database upon creation (if the account is
persistent).

`Persona` implements the fuchsia.identity.account.Persona FIDL protocol.

`StoredAccount` implements JSON serialization and deserialization of account
metadata.


## Future Work

The Account Handler is not yet fully complete. In particular the change listener
protocols and local authentication state are not finished.

Currently Account Handler (and the associated FIDL protocol) only handles a
single persona for each account. Eventually we will support the creation and
management of multiple personae.

