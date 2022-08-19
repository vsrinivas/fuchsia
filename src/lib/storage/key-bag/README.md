# Key-bag

Key-bag is a library for managing the persistence of a set of cryptographic keys.  The keys are
AES256 keys wrapped using an AEAD, such as AES256-GCM-SIV.  A given key-bag can store an arbitrary
number of keys wrapped with an arbitrary number of wrapping keys, held in a specified slot.

Rust and C bindings are provided.

## Regenerating C bindings

See documentation in //src/lib/storage/key-bag/c/key_bag.h
