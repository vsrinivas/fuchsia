// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
extern crate failure;

use failure::Error;
use std::collections::HashMap;
use std::time::{Duration, Instant};

/// Constant offset for the cache expiry. Tokens will only be returned from
/// the cache if they have at least this much life remaining.
const PADDING_FOR_TOKEN_EXPIRY: Duration = Duration::from_secs(600);

#[derive(Debug, PartialEq, Eq, Fail)]
pub enum CacheError {
    #[fail(display = "invalid argument")]
    InvalidArguments,
    #[fail(display = "supplied key not found in cache")]
    KeyNotFound,
    #[fail(display = "supplied key was found in cache but expired")]
    CacheExpired,
}

/// Representation of a single OAuth token including its expiry time.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct OAuthToken {
    expiry_time: Instant,
    token: String,
}

/// Representation of a single Firebase token including its expiry time.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FirebaseAuthToken {
    expiry_time: Instant,
    id_token: String,
    local_id: String,
    email: String,
}

/// A collection of OAuth and Firebase tokens associated with an identity
/// provider and profile. These always include either an ID token, an access
/// token, or both and optionally include a list of Firebase tokens.
#[derive(Clone, Debug, PartialEq, Eq)]
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
            _ => false,
        };

        if is_token_expired(&self.id_token) {
            self.id_token = None;
        }
        if is_token_expired(&self.access_token) {
            self.access_token = None;
        }
        self.firebase_token_map
            .retain(|_, v| current_time <= (v.expiry_time - PADDING_FOR_TOKEN_EXPIRY));
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
    idp_credential_id: String,
}

impl CacheKey {
    /// Create a new CacheKey, or returns an Error if any input is empty.
    pub fn new(idp_provider: String, idp_credential_id: String) -> Result<CacheKey, Error> {
        if idp_provider.is_empty() {
            Err(format_err!("idp_provider cannot be empty"))
        } else if idp_credential_id.is_empty() {
            Err(format_err!("idp_credential_id cannot be empty"))
        } else {
            Ok(CacheKey {
                idp_provider,
                idp_credential_id,
            })
        }
    }
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
        TokenCache {
            map: HashMap::with_capacity(size),
        }
    }

    /// Returns all unexpired tokens stored in the cache for the given key.
    /// Any expired tokens are also purged from the underlying cache.
    /// Returns an error if the key was not found or has expired.
    pub fn get(&mut self, cache_key: &CacheKey) -> Result<&TokenSet, CacheError> {
        // First remove any expired tokens from the value if it exists then
        // delete the entire entry if this now means it is invalid.
        let mut expired_cache = false;
        if let Some(token_set) = self.map.get_mut(cache_key) {
            expired_cache = token_set.remove_expired_tokens();
        }
        if expired_cache {
            self.map.remove(cache_key);
            return Err(CacheError::CacheExpired);
        }

        // Any remaining key is now valid
        match self.map.get(cache_key) {
            Some(token_set) => Ok(token_set),
            None => Err(CacheError::KeyNotFound),
        }
    }

    /// Adds a new cache entry, replacing any existing entry with the same key.
    pub fn put(&mut self, cache_key: CacheKey, token_set: TokenSet) -> Result<(), CacheError> {
        if !token_set.is_valid() {
            Err(CacheError::InvalidArguments)
        } else {
            self.map.insert(cache_key, token_set);
            Ok(())
        }
    }

    /// Removes all tokens associated with the supplied key, returning an error
    /// if none exist.
    pub fn delete(&mut self, cache_key: &CacheKey) -> Result<(), CacheError> {
        if !self.map.contains_key(cache_key) {
            Err(CacheError::KeyNotFound)
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
        firebase_api_key: &str,
        firebase_token: FirebaseAuthToken,
    ) -> Result<(), CacheError> {
        match self.map.get_mut(cache_key) {
            Some(token_set) => {
                token_set
                    .firebase_token_map
                    .insert(firebase_api_key.to_owned(), firebase_token);
                Ok(())
            }
            None => Err(CacheError::KeyNotFound),
        }
    }

    /// Returns true iff the supplied key is present in this cache.
    pub fn has_key(&self, cache_key: &CacheKey) -> bool {
        self.map.contains_key(cache_key)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const CACHE_SIZE: usize = 3;

    const TEST_IDP: &str = "test.com";
    const TEST_CREDENTIAL_ID: &str = "test.com/profiles/user";
    const TEST_EMAIL: &str = "user@test.com";
    const TEST_ID_TOKEN: &str = "ID token for test user";
    const TEST_ACCESS_TOKEN: &str = "Access token for test user";
    const TEST_API_KEY: &str = "Test API key";
    const TEST_FIREBASE_ID_TOKEN: &str = "Firebase token for test user";
    const TEST_FIREBASE_LOCAL_ID: &str = "Local ID for test firebase token";
    const LONG_EXPIRY: Duration = Duration::from_secs(3000);
    const ALREADY_EXPIRED: Duration = Duration::from_secs(1);

    fn build_test_cache_key(suffix: &str) -> CacheKey {
        CacheKey {
            idp_provider: TEST_IDP.to_string(),
            idp_credential_id: TEST_CREDENTIAL_ID.to_string() + suffix,
        }
    }

    fn build_test_token_set(time_until_expiry: Duration, suffix: &str) -> TokenSet {
        let expiry_time = Instant::now() + time_until_expiry;
        TokenSet {
            id_token: Some(OAuthToken {
                expiry_time,
                token: TEST_ID_TOKEN.to_string() + suffix,
            }),
            access_token: Some(OAuthToken {
                expiry_time,
                token: TEST_ACCESS_TOKEN.to_string() + suffix,
            }),
            firebase_token_map: HashMap::new(),
        }
    }

    fn build_test_firebase_token(time_until_expiry: Duration, suffix: &str) -> FirebaseAuthToken {
        FirebaseAuthToken {
            expiry_time: Instant::now() + time_until_expiry,
            id_token: TEST_FIREBASE_ID_TOKEN.to_string() + suffix,
            local_id: TEST_FIREBASE_LOCAL_ID.to_string() + suffix,
            email: TEST_EMAIL.to_string(),
        }
    }

    #[test]
    fn test_get_and_put() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);

        // Verify requesting an entry from an cache that does not contain it fails.
        let key = build_test_cache_key("");
        assert_eq!(token_cache.get(&key), Err(CacheError::KeyNotFound));

        // Verify inserting then retrieving a token succeeds.
        let token_set = build_test_token_set(LONG_EXPIRY, "");
        assert_eq!(token_cache.put(key.clone(), token_set.clone()), Ok(()));
        assert_eq!(token_cache.get(&key), Ok(&token_set));
    }

    #[test]
    fn test_has_key() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);
        let key = build_test_cache_key("");
        assert_eq!(token_cache.has_key(&key), false);
        assert_eq!(
            token_cache.put(key.clone(), build_test_token_set(LONG_EXPIRY, "")),
            Ok(())
        );
        assert_eq!(token_cache.has_key(&key), true);
    }

    #[test]
    fn test_delete() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);
        let key = build_test_cache_key("");
        assert_eq!(token_cache.delete(&key), Err(CacheError::KeyNotFound));
        assert_eq!(
            token_cache.put(key.clone(), build_test_token_set(LONG_EXPIRY, "")),
            Ok(())
        );
        assert_eq!(token_cache.has_key(&key), true);
        assert_eq!(token_cache.delete(&key), Ok(()));
        assert_eq!(token_cache.has_key(&key), false);
    }

    #[test]
    fn test_remove_oauth_on_expiry() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);

        // Insert one entry thats already expired and one that hasn't.
        let key_1 = build_test_cache_key("1");
        let token_set_1 = build_test_token_set(LONG_EXPIRY, "1");
        assert_eq!(token_cache.put(key_1.clone(), token_set_1.clone()), Ok(()));
        let key_2 = build_test_cache_key("2");
        let token_set_2 = build_test_token_set(ALREADY_EXPIRED, "2");
        assert_eq!(token_cache.put(key_2.clone(), token_set_2.clone()), Ok(()));

        // Both keys should be present.
        assert_eq!(token_cache.has_key(&key_1), true);
        assert_eq!(token_cache.has_key(&key_2), true);

        // Getting the expired key should fail and remove it from the cache.
        assert_eq!(token_cache.get(&key_1), Ok(&token_set_1));
        assert_eq!(token_cache.get(&key_2), Err(CacheError::CacheExpired));
        assert_eq!(token_cache.has_key(&key_1), true);
        assert_eq!(token_cache.has_key(&key_2), false);
    }

    #[test]
    fn test_add_firebase_token() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);

        // Create a new entry in the cache without any firebase tokens.
        let key_1 = build_test_cache_key("1");
        let mut token_set = build_test_token_set(LONG_EXPIRY, "");
        assert_eq!(token_cache.put(key_1.clone(), token_set.clone()), Ok(()));

        // Add a firebase token and verify it can be retreived.
        let firebase_token = build_test_firebase_token(LONG_EXPIRY, "");
        assert_eq!(
            token_cache.add_firebase_token(&key_1, TEST_API_KEY, firebase_token.clone()),
            Ok(())
        );
        token_set
            .firebase_token_map
            .insert(TEST_API_KEY.to_string(), firebase_token.clone());
        assert_eq!(token_cache.get(&key_1), Ok(&token_set));

        // Very we can't add a firebase token on a non-existant key.
        let key_2 = build_test_cache_key("2");
        assert_eq!(
            token_cache.add_firebase_token(&key_2, TEST_API_KEY, firebase_token.clone()),
            Err(CacheError::KeyNotFound)
        );
    }

    #[test]
    fn test_remove_firebase_on_expiry() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);

        // Create a new entry in the cache without any firebase tokens.
        let key = build_test_cache_key("");
        let mut token_set = build_test_token_set(LONG_EXPIRY, "");
        assert_eq!(token_cache.put(key.clone(), token_set.clone()), Ok(()));

        // Add two firebase tokens, one expired, one not.
        let firebase_token_1 = build_test_firebase_token(LONG_EXPIRY, "1");
        assert_eq!(
            token_cache.add_firebase_token(
                &key,
                &(TEST_API_KEY.to_string() + "1"),
                firebase_token_1.clone()
            ),
            Ok(())
        );
        let firebase_token_2 = build_test_firebase_token(ALREADY_EXPIRED, "2");
        assert_eq!(
            token_cache.add_firebase_token(
                &key,
                &(TEST_API_KEY.to_string() + "2"),
                firebase_token_2.clone()
            ),
            Ok(())
        );

        // Verify only the not expired token is present when we retrieve the token set.
        token_set
            .firebase_token_map
            .insert(TEST_API_KEY.to_string() + "1", firebase_token_1.clone());
        assert_eq!(token_cache.get(&key), Ok(&token_set));
    }
}
