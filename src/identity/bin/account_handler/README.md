# Account Handler

## Overview

Account Handler manages the state of a single Fuchsia account (and its personae)
on a Fuchsia device. It also provides access to authentication tokens for the
Service Provider accounts associated with the Fuchsia account.

Account Handler component instances are launched by Account Manager and
implement the fuchsia.auth.account.internal.AccountHandlerControl FIDL protocols
so they may be controlled by Account Manager. Each Account Handler component
instance is responsible for handling a single Fuchsia account and only accesses
the persistent storage for that one account.

Account Handler also implements the fuchsia.auth.account.Account and
fuchsia.auth.account.Persona FIDL protocols. These protocols are not
discoverable; Account channels may only be obtained through Account Manager and
Persona channels may only be obtained from an Account channel.


## Key Dependencies

* */identity/lib/account_common* - Account Handler uses error and identifier
  definitions from this crate
* */identity/lib/token_manager* - Account Manager uses the TokenManager library
  to perform the authentication token management and implement the
  fuchsia.auth.TokenManager FIDL protocol


## Design

`AccountHandler` implements the
fuchsia.auth.account.internal.AccountHandlerControl FIDL protocol. The crate's
main function creates a single instance of `AccountHandler` and uses it to
handle all incoming requests.

The AccountHandlerControl protocol allows for delayed initialization of the
Account Handler; the first call received on the protocol must be one of the
initialization methods that bind the handler to a particular account (either an
existing account or a freshly created account). After this point no further
initialization calls are allowed.

When this initialization call is received `AccountHandler` creates a single
instance of the `Account` struct and uses this to serve subsequent
AccountHandlerControl.GetAccount calls.

`Account` implements the fuchsia.auth.account.Account FIDL protocol and stores
an instance of the `Persona` struct representing the default Persona.

`Persona` implements the fuchsia.auth.account.Persona FIDL protocol.


## Future Work

The Account Handler is not yet fully complete. In particular the change listener
protocols and local authentication state are not finished.

Currently the AccountHandlerContext is passed explicitly from Account Manager to
Account Handler in the initialization call. In the future this context will
instead be supplied in Account Handler's namespace as it is launched.

Currently Account Handler (and the associated FIDL protocol) only handles a
single persona for each account. Eventually we will support the creation and
management of multiple personae.

