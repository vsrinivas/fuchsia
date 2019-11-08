# Identity Key Manager

## Overview

Identity Key Manager defines an implementation of the
fuchsia.identity.keys.KeyManager and fuchsia.identity.keys.KeySet FIDL protocols.
It is used by the AccountHandler binary.

## Design

`lib.rs` defines important types that clients of the library must supply:
* The `KeyManagerContext` struct defines the context that a particular request
  to the token manager was received in. This contains the url of the component
  using the KeyManager channel.

`KeyManager` implements the fuchsia.identity.keys.KeyManager protocol and may
be instantiated by clients.

## Future Work

KeyManager is not yet complete.

As an initial milestone, which is not yet reached, KeyManager will provide a
set of keys which are local to the device and not synchronized.
