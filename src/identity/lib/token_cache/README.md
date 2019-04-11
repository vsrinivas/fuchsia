# Token Cache

## Overview

Token Cache provides an ephemeral cache for short-lived authentication tokens
such as OAuth access and ID tokens and Firebase tokens. The cache contents are
only stored in memory so are not preserved across executions.

This crate is used by the Token Manager library.


## Key Dependencies

None


## Design

The `TokenCache` struct defines the cache as a map from `CacheKey` to
`TokenSet`. `CacheKey` uniquely identifies a Service Provider account and
the Auth Provider used to communicate with it, being composed of
auth_provider_type and credential_id (aka user_profile_id). `TokenSet` contains
a map for each of the possible short-lived token types.


## Future Work

The current nested cache structure is more complex and less flexible than
necessary. We plan to migrate to a simpler design where each cache entry defines
a single token.

Currently the cache does not handle expiry correctly if the maximum size is
reached. Given the number of accounts, auth providers, and token types within
the current products these limits are not being reached, but future work will
correctly handle the size constraint.

The cache is a simple crate that is unlikely to ever be used outside of Token
Manager. Potentially it will be merged into Token Manager at some point.
