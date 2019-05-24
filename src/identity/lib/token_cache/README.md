# Token Cache

## Overview

Token Cache provides an ephemeral cache for short-lived authentication tokens
such as OAuth access and ID tokens and Firebase tokens. The cache contents are
only stored in memory so are not preserved across executions.

This crate is used by the Token Manager library.


## Key Dependencies

None


## Design

The `TokenCache` struct stores tokens as a mapping from `CacheKey` to
`CacheToken` implementors.  `CacheKey` and `CacheToken` are traits that define
the requirements for keys and tokens stored in the cache.  A `CacheToken`
implementation must provide an expiration time used for cache eviction.  A
`CacheKey` must provide the Service Provider account (user_profile_id) and the
Auth Provider used to communicate with it (auth_provider_type), as well as some
unique identifier.  A `CacheKey` is also statically tied to a specific
`CacheToken` type to enforce type safety.  `CacheKey` and `CacheToken`
implementations are provided by the user.


## Future Work

The cache is a simple crate that is unlikely to ever be used outside of Token
Manager. Potentially it will be merged into Token Manager at some point.
