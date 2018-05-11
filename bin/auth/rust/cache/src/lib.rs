// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::time::{Duration, Instant};


/// Constant offset for the cache expiry. Tokens will only be returned from
/// the cache if they have at least this much life remaining.
static PADDING_FOR_TOKEN_EXPIRY:Duration = Duration::from_secs(600);

pub enum Error {
    InvalidArguments,
    KeyNotFound,
    CacheExpired,
}

/// Representation of a single OAuth token including its expiry time.
pub struct OAuthToken {
    expiry_time: Instant,
    token: String,
}

/// Representation of a single Firebase token including its expiry time.
pub struct FirebaseAuthToken {
    expiry_time: Instant,
    id_token: String,
    local_id: String,
    email: String,
}

/// A collection of OAuth and Firebase tokens associated with an identity
/// provider and profile. These always include either an ID token, an access
/// token, or both and optionally include a list of Firebase tokens.
pub struct TokenSet {
    id_token: Option<OAuthToken>,
    access_token: Option<OAuthToken>,
    firebase_token_map: HashMap<String, FirebaseAuthToken>,
}

impl TokenSet {
    /// Removes any expired OAuth and Firebase tokens from this set and
    /// returns true iff no valid OAuth tokens remain.
    fn remove_expired_tokens(&mut self) -> bool {
        let current_time = Instant::now();
        let is_token_expired = |token: &Option<OAuthToken>| match token {
            Some(t) if current_time > (t.expiry_time - PADDING_FOR_TOKEN_EXPIRY) => true,
            _ => false
        };

        if is_token_expired(&self.id_token) {
            self.id_token = None;
        }
        if is_token_expired(&self.access_token) {
            self.access_token = None;
        }
        self.firebase_token_map.retain(
            |_, v| current_time > (v.expiry_time - PADDING_FOR_TOKEN_EXPIRY));
        !self.is_valid()
    }

    /// Returns true iff this set contains at least one valid Oauth token.
    fn is_valid(&self) -> bool {
        self.id_token.is_some() && self.access_token.is_some()
    }
}

/// A unique key for accessing the token cache.
#[derive(Clone, Hash, PartialEq, Eq)]
pub struct CacheKey {
    idp_provider: String,
    idp_credential_id: String
}

/// A cache of recently used OAuth and Firebase tokens. All tokens contain
/// an expiry time and are removed when this time is reached.
pub struct TokenCache {
    // TODO(jsankey): Define and enforce a max size on the number of cache
    // entries
    map: HashMap<CacheKey, TokenSet>,
}

impl TokenCache {

    /// Creates a new TokenCache with the specified initial size.
    pub fn new(size: usize) -> TokenCache {
        TokenCache{map: HashMap::with_capacity(size)}
    }

    /// Returns all unexpired tokens stored in the cache for the given key.
    /// Any expired tokens are also purged from the underlying cache.
    /// Returns an error if the key was not found or has expired.
    pub fn get(&mut self, cache_key: &CacheKey) -> Result<&TokenSet, Error> {
        // First remove any expired tokens from the value if it exists then
        // delete the entire entry if this now means it is invalid.
        let mut expired_cache = false;
        if let Some(token_set) = self.map.get_mut(cache_key) {
            expired_cache = token_set.remove_expired_tokens();
        }
        if expired_cache {
            self.map.remove(cache_key);
            return Err(Error::CacheExpired);
        }

        // Any remaining key is now valid
        match self.map.get(cache_key) {
            Some(token_set) => Ok(token_set),
            None => Err(Error::KeyNotFound)
        }
    }

    /// Adds a new cache entry, replacing any existing entry with the same key.
    pub fn put(&mut self, cache_key: CacheKey, token_set: TokenSet) -> Result<(), Error> {
        if !token_set.is_valid() {
            Err(Error::InvalidArguments)
        } else {
            self.map.insert(cache_key, token_set);
            Ok(())
        }
    }

    /// Removes all tokens associated with the supplied key, returning an error
    /// if none exist.
    pub fn delete(&mut self, cache_key: &CacheKey) -> Result<(), Error> {
        if !self.map.contains_key(cache_key) {
            Err(Error::KeyNotFound)
        } else {
            self.map.remove(cache_key);
            Ok(())
        }
    }

    /// Add a new firebase auth token to the supplied key, returning an error
    /// if the key is not found.
    pub fn add_firebase_token(
            &mut self,
            cache_key: &CacheKey,
            firebase_api_key: String,
            firebase_token: FirebaseAuthToken) -> Result<(), Error> {
        match self.map.get_mut(cache_key) {
            Some(token_set) => {
                token_set.firebase_token_map.insert(firebase_api_key, firebase_token);
                Ok(())
            },
            None => Err(Error::KeyNotFound)
        }
    }

    /// Returns true iff the supplied key is present in this cache.
    pub fn has_key(&self, cache_key: &CacheKey) -> bool {
        self.map.contains_key(cache_key)
    }
}

