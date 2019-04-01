// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! AuthCache manages an in-memory cache of recently used short-lived authentication tokens.
#![deny(warnings)]
#![deny(missing_docs)]

use chrono::offset::Utc;
use chrono::DateTime;
use failure::Fail;
use log::{info, warn};
use std::any::Any;
use std::collections::HashMap;
use std::hash::{Hash, Hasher};
use std::sync::Arc;
use std::time::{Duration, SystemTime};

/// Constant offset for the cache expiry. Tokens will only be returned from
/// the cache if they have at least this much life remaining.
const PADDING_FOR_TOKEN_EXPIRY: Duration = Duration::from_secs(600);

/// An enumeration of the possible failure modes for operations on a `TokenCache`.
#[derive(Debug, PartialEq, Eq, Fail)]
pub enum AuthCacheError {
    /// One or more inputs were not valid.
    #[fail(display = "invalid argument")]
    InvalidArguments,
    /// The supplied key could not be found in the cache.
    #[fail(display = "supplied key not found in cache")]
    KeyNotFound,
}

/// Trait for keys used in the cache.  `CacheKey` requires the `Any` trait for
/// dynamic typing.  As `Any` is not implemented for any struct containing a
/// non-'static reference, any valid implementation of `CacheKey` may not
/// contain references of non-'static lifetimes.
pub trait CacheKey: Any + Send + Sync {
    /// Returns the identity provider type, ex. 'google'
    fn auth_provider_type(&self) -> &str;
    /// Returns the account identifier as given by the identity provider.
    fn user_profile_id(&self) -> &str;
    /// Returns an identifier appropriate for differentiating CacheKeys of the
    /// same concrete type with the same auth_provider_type and user_profile_id.
    fn subkey(&self) -> &str;
}

impl Hash for CacheKey {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.auth_provider_type().hash(state);
        self.user_profile_id().hash(state);
        self.subkey().hash(state);
        self.type_id().hash(state);
    }
}

impl PartialEq for CacheKey {
    fn eq(&self, other: &(CacheKey + 'static)) -> bool {
        self.auth_provider_type() == other.auth_provider_type()
            && self.user_profile_id() == other.user_profile_id()
            && self.subkey() == other.subkey()
            && self.type_id() == other.type_id()
    }
}

impl Eq for CacheKey {}

/// Trait that specifies the concrete token type a token key can put or
/// retrieve from the cache.
pub trait KeyFor {
    /// Token type the key exclusively identifies.
    type TokenType;
}

/// Trait for tokens stored in the cache.  `CacheToken` requires the `Any`
/// trait for dynamic typing.  As `Any` is not implemented for any struct
/// containing a non-'static reference, any valid implementation of
/// `CacheToken` may not contain references of non-'static lifetimes.
pub trait CacheToken: Any + Send + Sync {
    /// Returns the time at which a token becomes invalid.
    fn expiry_time(&self) -> &SystemTime;
}

/// A cache of recently used authentication tokens. All tokens contain an
/// expiry time and are removed when this time is reached.
pub struct TokenCache {
    // TODO(satsukiu): Define and enforce a max size on the number of cache
    // entries
    /// A mapping holding cached tokens of arbitrary types.
    token_map: HashMap<Box<CacheKey>, Arc<Any + Send + Sync>>,
    /// `SystemTime` of the last cache operation.  Used to validate time progression.
    last_time: SystemTime,
}

impl TokenCache {
    /// Creates a new `TokenCache` with the specified initial size.
    pub fn new(size: usize) -> TokenCache {
        TokenCache { token_map: HashMap::with_capacity(size), last_time: SystemTime::now() }
    }

    /// Sanity check that system time has not jumped backwards since last time this method was
    /// called. If time jumped backwards, a warning is logged and the entire cache is cleared.
    /// If time is normal, this method does nothing.
    /// TODO(dnordstrom): Long term solution involving time service or monotonic clocks.
    fn validate_time_progression(&mut self) {
        let current_time = SystemTime::now();
        if current_time < self.last_time {
            warn!(
                "time jumped backwards from {:?} to {:?}, clearing {:?} token cache entries",
                <DateTime<Utc>>::from(current_time),
                <DateTime<Utc>>::from(self.last_time),
                self.token_map.len(),
            );
            self.token_map.clear();
        }
        self.last_time = current_time;
    }

    /// Returns a token for the specified key if present and not expired.
    pub fn get<K, V>(&mut self, key: &K) -> Option<Arc<V>>
    where
        K: CacheKey + KeyFor<TokenType = V>,
        V: CacheToken,
    {
        self.validate_time_progression();
        let uncast_token = self.token_map.get(key as &CacheKey)?;

        let downcast_token = if let Ok(downcast_token) = uncast_token.clone().downcast::<V>() {
            downcast_token
        } else {
            warn!("Error downcasting token in cache.");
            return None;
        };

        if Self::is_token_expired(downcast_token.as_ref()) {
            self.token_map.remove(key as &CacheKey);
            None
        } else {
            Some(downcast_token)
        }
    }

    /// Adds a token to the cache, replacing any existing token for the same key.
    pub fn put<K, V>(&mut self, key: K, token: Arc<V>)
    where
        K: CacheKey + KeyFor<TokenType = V>,
        V: CacheToken,
    {
        self.validate_time_progression();
        self.token_map.insert(Box::new(key), token);
    }

    /// Deletes all the tokens associated with the given auth_provider_type and
    /// user_profile_id.  Returns an error if no matching keys are found.
    pub fn delete_matching(
        &mut self,
        auth_provider_type: &str,
        user_profile_id: &str,
    ) -> Result<(), AuthCacheError> {
        self.validate_time_progression();
        // TODO(satsukiu): evict expired tokens first.  This gives consistent behavior when
        // the only matching tokens are expired.

        let entries_before_delete = self.token_map.len();
        self.token_map.retain(|key, _| {
            key.auth_provider_type() != auth_provider_type
                || key.user_profile_id() != user_profile_id
        });

        let entries_after_delete = self.token_map.len();
        if entries_after_delete < entries_before_delete {
            info!(
                "Deleted {:?} matching entries from the token cache.",
                entries_before_delete - entries_after_delete
            );
            Ok(())
        } else {
            Err(AuthCacheError::KeyNotFound)
        }
    }

    /// Returns true if the given token is expired.
    fn is_token_expired(token: &CacheToken) -> bool {
        let current_time = SystemTime::now();
        current_time > (*token.expiry_time() - PADDING_FOR_TOKEN_EXPIRY)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const CACHE_SIZE: usize = 3;

    const TEST_AUTH_PROVIDER: &str = "test_auth_provider";
    const TEST_USER_ID: &str = "test_auth_provider/profiles/user";
    const TEST_TOKEN_CONTENTS: &str = "Token contents";
    const LONG_EXPIRY: Duration = Duration::from_secs(3000);
    const ALREADY_EXPIRED: Duration = Duration::from_secs(1);

    #[derive(Clone, Debug, PartialEq, Eq)]
    struct TestToken {
        expiry_time: SystemTime,
        token: String,
    }

    impl CacheToken for TestToken {
        fn expiry_time(&self) -> &SystemTime {
            &self.expiry_time
        }
    }

    #[derive(Clone, Debug, PartialEq, Eq)]
    struct TestKey {
        auth_provider_type: String,
        user_profile_id: String,
        audience: String,
    }

    impl CacheKey for TestKey {
        fn auth_provider_type(&self) -> &str {
            &self.auth_provider_type
        }

        fn user_profile_id(&self) -> &str {
            &self.user_profile_id
        }

        fn subkey(&self) -> &str {
            &self.audience
        }
    }

    impl KeyFor for TestKey {
        type TokenType = TestToken;
    }

    #[derive(Clone, Debug, PartialEq, Eq)]
    struct AlternateTestToken {
        expiry_time: SystemTime,
        metadata: Vec<String>,
        token: String,
    }

    impl CacheToken for AlternateTestToken {
        fn expiry_time(&self) -> &SystemTime {
            &self.expiry_time
        }
    }

    #[derive(Clone, Debug, PartialEq, Eq)]
    struct AlternateTestKey {
        auth_provider_type: String,
        user_profile_id: String,
        scopes: String,
    }

    impl CacheKey for AlternateTestKey {
        fn auth_provider_type(&self) -> &str {
            &self.auth_provider_type
        }

        fn user_profile_id(&self) -> &str {
            &self.user_profile_id
        }

        fn subkey(&self) -> &str {
            &self.scopes
        }
    }

    impl KeyFor for AlternateTestKey {
        type TokenType = AlternateTestToken;
    }

    fn build_test_key(user_suffix: &str, audience: &str) -> TestKey {
        TestKey {
            auth_provider_type: TEST_AUTH_PROVIDER.to_string(),
            user_profile_id: TEST_USER_ID.to_string() + user_suffix,
            audience: audience.to_string(),
        }
    }

    fn build_alternate_test_key(user_suffix: &str, scopes: &str) -> AlternateTestKey {
        AlternateTestKey {
            auth_provider_type: TEST_AUTH_PROVIDER.to_string(),
            user_profile_id: TEST_USER_ID.to_string() + user_suffix,
            scopes: scopes.to_string(),
        }
    }

    fn build_test_token(time_until_expiry: Duration, suffix: &str) -> Arc<TestToken> {
        Arc::new(TestToken {
            expiry_time: SystemTime::now() + time_until_expiry,
            token: TEST_TOKEN_CONTENTS.to_string() + suffix,
        })
    }

    fn build_alternate_test_token(
        time_until_expiry: Duration,
        suffix: &str,
    ) -> Arc<AlternateTestToken> {
        Arc::new(AlternateTestToken {
            expiry_time: SystemTime::now() + time_until_expiry,
            token: TEST_TOKEN_CONTENTS.to_string() + suffix,
            metadata: vec![suffix.to_string()],
        })
    }

    #[test]
    fn test_get_and_put_token() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);

        // Verify requesting an entry from an cache that does not contain it fails.
        let key_1 = build_test_key("", "audience_1");
        assert_eq!(token_cache.get(&key_1), None);

        // Verify inserting then retrieving a token succeeds.
        let token_1 = build_test_token(LONG_EXPIRY, "1");
        token_cache.put(key_1.clone(), token_1.clone());
        assert_eq!(token_cache.get(&key_1), Some(token_1.clone()));

        // Verify a second token can be stored without conflict.
        let key_2 = build_test_key("", "audience_2");
        let token_2 = build_test_token(LONG_EXPIRY, "2");
        assert_eq!(token_cache.get(&key_2), None);
        token_cache.put(key_2.clone(), token_2.clone());
        assert_eq!(token_cache.get(&key_2), Some(token_2));
        assert_eq!(token_cache.get(&key_1), Some(token_1));
    }

    #[test]
    fn test_get_and_put_multiple_token_types() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);

        // Verify cache will get and put different datatypes
        let key = build_test_key("", "audience_1");
        let token = build_test_token(LONG_EXPIRY, "1");
        token_cache.put(key.clone(), token.clone());
        let alternate_key = build_alternate_test_key("", "scope");
        let alternate_token = build_alternate_test_token(LONG_EXPIRY, "2");
        token_cache.put(alternate_key.clone(), alternate_token.clone());
        assert_eq!(token_cache.get(&key), Some(token));
        assert_eq!(token_cache.get(&alternate_key), Some(alternate_token));

        // Verify keys of identical contents but different types do not clash.
        let key_2 = build_test_key("", "clash-test");
        let token_2 = build_test_token(LONG_EXPIRY, "3");
        token_cache.put(key_2.clone(), token_2.clone());
        let alternate_key_2 = build_alternate_test_key("", "clash-test");
        let alternate_token_2 = build_alternate_test_token(LONG_EXPIRY, "4");
        token_cache.put(alternate_key_2.clone(), alternate_token_2.clone());

        assert_eq!(token_cache.get(&key_2), Some(token_2));
        assert_eq!(token_cache.get(&alternate_key_2), Some(alternate_token_2));
    }

    #[test]
    fn test_delete_matching() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);
        let key = build_test_key("", "audience");

        // Verify deleting when cache is empty fails.
        assert_eq!(
            token_cache.delete_matching(key.auth_provider_type(), key.user_profile_id()),
            Err(AuthCacheError::KeyNotFound)
        );

        // Verify matching keys are removed.
        token_cache.put(key.clone(), build_test_token(LONG_EXPIRY, ""));
        assert_eq!(
            token_cache.delete_matching(key.auth_provider_type(), key.user_profile_id()),
            Ok(())
        );
        assert!(token_cache.get(&key).is_none());

        // Verify non-matching keys are not removed.
        let matching_key = build_test_key("matching", "");
        let non_matching_key = build_test_key("non-matching", "");
        let non_matching_token = build_test_token(LONG_EXPIRY, "non-matching");
        token_cache.put(matching_key.clone(), build_test_token(LONG_EXPIRY, "matching"));
        token_cache.put(non_matching_key.clone(), non_matching_token.clone());
        assert_eq!(
            token_cache
                .delete_matching(matching_key.auth_provider_type(), matching_key.user_profile_id()),
            Ok(())
        );
        assert!(token_cache.get(&matching_key).is_none());
        assert_eq!(token_cache.get(&non_matching_key), Some(non_matching_token));
    }

    #[test]
    fn test_remove_expired_tokens() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);

        // Insert one entry that's already expired and one that hasn't.
        let key = build_test_key("valid", "audience_1");
        let token = build_test_token(LONG_EXPIRY, "1");
        token_cache.put(key.clone(), token.clone());
        let expired_key = build_alternate_test_key("expired", "scope");
        let expired_token = build_alternate_test_token(ALREADY_EXPIRED, "2");
        token_cache.put(expired_key.clone(), expired_token.clone());

        // Getting the expired key should fail.
        assert_eq!(token_cache.get(&key), Some(token));
        assert_eq!(token_cache.get(&expired_key), None);
    }
}
