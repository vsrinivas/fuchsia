# fxfs-crypt

This module contains the implementation of the Crypt service, which manages wrapping and unwrapping
cryptographic keys for Fxfs instances out-of-process.

Generally, one fxfs-crypt instance will be running per unlocked volume.  A handle to this crypt
server will be passed to Fxfs as part of unlocking the volume.  The creator of the crypt instance
will use the CryptManagement protocol to control the state of the crypt service (adding new keys,
switching active keys, and removing old keys).

The algorithm used for key wrapping is [AES-GCM-SIV](https://en.wikipedia.org/wiki/AES-GCM-SIV).
