// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! AuthCache manages an in-memory cache of recently used short-lived authentication tokens.
#![deny(missing_docs)]

use failure::Fail;
use fuchsia_zircon::{ClockId, Duration, Time};
use log::{info, warn};
use std::any::Any;
use std::cmp::Ordering;
use std::collections::{BinaryHeap, HashMap};
use std::hash::{Hash, Hasher};
use std::sync::Arc;

/// Constant offset for the cache expiry. Tokens will only be returned from
/// the cache if they have at least this much life remaining.
const PADDING_FOR_TOKEN_EXPIRY: Duration = Duration::from_seconds(600);

/// Number of invalid entries tolerated in the expiry queue before invalid
/// entries are purged.  This is expressed as a fraction of the cache capacity.
const INVALID_ENTRY_FLUSH_THRESHOLD: f32 = 0.5;

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

impl Hash for dyn CacheKey {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.auth_provider_type().hash(state);
        self.user_profile_id().hash(state);
        self.subkey().hash(state);
        self.type_id().hash(state);
    }
}

impl PartialEq for dyn CacheKey {
    fn eq(&self, other: &(dyn CacheKey + 'static)) -> bool {
        self.auth_provider_type() == other.auth_provider_type()
            && self.user_profile_id() == other.user_profile_id()
            && self.subkey() == other.subkey()
            && self.type_id() == other.type_id()
    }
}

impl Eq for dyn CacheKey {}

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
    fn expiry_time(&self) -> &Time;
}

/// An entry in the `TokenCache` expiry queue.
struct ExpiryQueueEntry {
    /// Cache entry this entry tracks.
    cache_key: Arc<dyn CacheKey>,
    /// Time at which the referenced token expires.
    expiry_time: Time,
}

impl ExpiryQueueEntry {
    /// Construct a new `ExpiryQueueEntry`.
    fn new(cache_key: Arc<dyn CacheKey>, expiry_time: Time) -> ExpiryQueueEntry {
        ExpiryQueueEntry { cache_key: cache_key, expiry_time: expiry_time }
    }
}

impl Ord for ExpiryQueueEntry {
    fn cmp(&self, other: &ExpiryQueueEntry) -> Ordering {
        self.expiry_time.cmp(&other.expiry_time).reverse()
    }
}

impl PartialOrd for ExpiryQueueEntry {
    fn partial_cmp(&self, other: &ExpiryQueueEntry) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl PartialEq for ExpiryQueueEntry {
    fn eq(&self, other: &ExpiryQueueEntry) -> bool {
        self.expiry_time == other.expiry_time
    }
}

impl Eq for ExpiryQueueEntry {}

/// A wrapper struct around a `CacheToken` that allows it to be used for
/// dynamic dispatch as both `CacheToken` and `Any` trait objects.
/// This approach becomes unnecessary if casting to and from a supertrait
/// (&CacheToken <-> &Any) becomes possible.  Upcasting is tracked as a
/// requirement in RFC issue https://github.com/rust-lang/rfcs/issues/349.
/// An alternative is to store Arc<CacheToken + Any> if multiple-trait trait
/// objects are added (https://github.com/rust-lang/rfcs/issues/2035)
struct TokenReference {
    /// token stored as `CacheToken` trait object
    cache_token: Arc<dyn CacheToken>,
    /// token stored as `Any` trait object
    any: Arc<dyn Any + Send + Sync>,
}

impl TokenReference {
    /// Create a new `TokenReference`
    fn new<T: CacheToken>(token: Arc<T>) -> TokenReference {
        TokenReference {
            cache_token: Arc::clone(&token) as Arc<dyn CacheToken>,
            any: token as Arc<dyn Any + Send + Sync>,
        }
    }

    /// Borrow as a `CacheToken` trait object.
    fn as_cache_token(&self) -> &Arc<dyn CacheToken> {
        &self.cache_token
    }

    /// Borrow as an `Any` trait object.
    fn as_any(&self) -> &Arc<dyn Any + Send + Sync> {
        &self.any
    }
}

/// A cache of recently used authentication tokens. All tokens contain an
/// expiry time and are removed when this time is reached.
pub struct TokenCache {
    /// A mapping holding cached tokens of arbitrary types.
    token_map: HashMap<Arc<dyn CacheKey>, TokenReference>,
    /// A priority queue used to evict expired tokens.
    expiry_queue: BinaryHeap<ExpiryQueueEntry>,
    /// Maximum number of tokens the cache will hold.
    capacity: usize,
    /// `Time` of the last cache operation.  Used to validate time progression.
    last_time: Time,
}

impl TokenCache {
    /// Creates a new `TokenCache` with the specified capacity.
    pub fn new(capacity: usize) -> TokenCache {
        let expiry_queue_capacity = capacity + Self::num_tolerated_invalid_entries(capacity);
        TokenCache {
            token_map: HashMap::with_capacity(capacity),
            expiry_queue: BinaryHeap::with_capacity(expiry_queue_capacity),
            capacity: capacity,
            last_time: Self::get_current_time(),
        }
    }

    /// Sanity check that system time has not jumped backwards since last time this method was
    /// called. If time jumped backwards, a warning is logged and the entire cache is cleared.
    /// If time is normal, this method does nothing.
    /// TODO(dnordstrom): Long term solution involving time service or monotonic clocks.
    fn validate_time_progression(&mut self) {
        let current_time = Self::get_current_time();
        if current_time < self.last_time {
            warn!(
                "time jumped backwards from {:?} to {:?}, clearing {:?} token cache entries",
                self.last_time,
                current_time,
                self.token_map.len(),
            );
            self.token_map.clear();
            self.expiry_queue.clear();
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
        self.evict_expired();

        let uncast_token = self.token_map.get(key as &dyn CacheKey)?.as_any();
        if let Ok(downcast_token) = Arc::clone(uncast_token).downcast::<V>() {
            Some(downcast_token)
        } else {
            warn!("Error downcasting token in cache.");
            None
        }
    }

    /// Adds a token to the cache, replacing any existing token for the same key.
    pub fn put<K, V>(&mut self, key: K, token: Arc<V>)
    where
        K: CacheKey + KeyFor<TokenType = V>,
        V: CacheToken,
    {
        self.validate_time_progression();
        self.evict_expired();
        if self.token_map.len() == self.capacity
            && !self.token_map.contains_key(&key as &dyn CacheKey)
        {
            self.evict_random();
        }

        let arc_key = Arc::new(key) as Arc<dyn CacheKey>;
        self.expiry_queue.push(ExpiryQueueEntry::new(Arc::clone(&arc_key), *token.expiry_time()));
        self.token_map.insert(arc_key, TokenReference::new(token));

        self.flush_invalid();
    }

    /// Deletes all the tokens associated with the given auth_provider_type and
    /// user_profile_id.  Returns an error if no matching, unexpired tokens are found.
    pub fn delete_matching(
        &mut self,
        auth_provider_type: &str,
        user_profile_id: &str,
    ) -> Result<(), AuthCacheError> {
        self.validate_time_progression();
        self.evict_expired();

        let entries_before_delete = self.token_map.len();
        self.token_map.retain(|key, _| {
            key.auth_provider_type() != auth_provider_type
                || key.user_profile_id() != user_profile_id
        });
        self.flush_invalid();

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

    /// Evicts all expired tokens from the cache.  Invalid entries in the expiry
    /// queue are silently discarded.
    fn evict_expired(&mut self) {
        let current_time = Self::get_current_time();
        while let Some(expiry_entry) = pop_if(&mut self.expiry_queue, |entry| {
            current_time > (entry.expiry_time - PADDING_FOR_TOKEN_EXPIRY)
        }) {
            let ExpiryQueueEntry { cache_key, expiry_time } = expiry_entry;
            match self.token_map.get(&cache_key) {
                Some(token) if *token.as_cache_token().expiry_time() == expiry_time => {
                    self.token_map.remove(&cache_key);
                }
                _ => (), // silently discard if key doesn't exist or expiry has been updated
            }
        }
    }

    /// Evicts a random token from the cache.
    fn evict_random(&mut self) {
        // TODO(satsukiu): Use a method to get a key that is actually random.
        match self.token_map.keys().next().map(|arc| arc.clone()) {
            Some(random_key) => {
                self.token_map.remove(&random_key);
            }
            None => warn!("Tried to evict random token from empty cache."),
        };
    }

    /// Flushes invalid entries from the eviction queue if there are excessive
    /// numbers of invalid entries.  Entries are invalid if the corresponding
    /// token has been updated with a new expiration time or the token has been
    /// removed from the cache.
    fn flush_invalid(&mut self) {
        let num_invalid_entries = self.expiry_queue.len() - self.token_map.len();
        if num_invalid_entries >= Self::num_tolerated_invalid_entries(self.capacity) {
            // This rebuilds the queue, which has the same end result as
            // filtering out invalid entries but is much slower.
            // TODO(satsukiu): flush out invalid entries inplace in the queue.
            self.expiry_queue.clear();
            self.expiry_queue.extend(self.token_map.iter().map(|(key, token)| {
                ExpiryQueueEntry::new(Arc::clone(key), *token.as_cache_token().expiry_time())
            }));
        }
    }

    /// Defines the number of invalid entries allowed in the expiry queue.
    fn num_tolerated_invalid_entries(capacity: usize) -> usize {
        ((capacity as f32) * INVALID_ENTRY_FLUSH_THRESHOLD) as usize
    }

    fn get_current_time() -> Time {
        Time::get(ClockId::UTC)
    }
}

/// Pop and return top element in a heap if it passes some condition.
fn pop_if<T, F>(heap: &mut BinaryHeap<T>, condition: F) -> Option<T>
where
    T: Ord,
    F: FnOnce(&T) -> bool,
{
    match condition(heap.peek()?) {
        true => heap.pop(),
        false => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const CACHE_SIZE: usize = 10;

    const TEST_AUTH_PROVIDER: &str = "test_auth_provider";
    const TEST_USER_ID: &str = "test_auth_provider/profiles/user";
    const TEST_TOKEN_CONTENTS: &str = "Token contents";
    const LONG_EXPIRY: Duration = Duration::from_seconds(3000);
    const ALREADY_EXPIRED: Duration = Duration::from_seconds(1);

    #[derive(Clone, Debug, PartialEq, Eq)]
    struct TestToken {
        expiry_time: Time,
        token: String,
    }

    impl CacheToken for TestToken {
        fn expiry_time(&self) -> &Time {
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
        expiry_time: Time,
        metadata: Vec<String>,
        token: String,
    }

    impl CacheToken for AlternateTestToken {
        fn expiry_time(&self) -> &Time {
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
            expiry_time: Time::get(ClockId::UTC) + time_until_expiry,
            token: TEST_TOKEN_CONTENTS.to_string() + suffix,
        })
    }

    fn build_alternate_test_token(
        time_until_expiry: Duration,
        suffix: &str,
    ) -> Arc<AlternateTestToken> {
        Arc::new(AlternateTestToken {
            expiry_time: Time::get(ClockId::UTC) + time_until_expiry,
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
    fn test_get_and_put_duplicate_key() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);
        let key = build_test_key("", "key");
        let original_token = build_test_token(LONG_EXPIRY, "original");
        token_cache.put(key.clone(), original_token.clone());
        assert_eq!(token_cache.get(&key), Some(original_token));

        // Verify most recently inserted token is returned.
        let duplicate_token = build_test_token(LONG_EXPIRY, "duplicate");
        token_cache.put(key.clone(), duplicate_token.clone());
        assert_eq!(token_cache.get(&key), Some(duplicate_token));

        // Verify no token returned if most recently inserted is expired.
        token_cache.put(key.clone(), build_test_token(ALREADY_EXPIRED, "expired"));
        assert!(token_cache.get(&key).is_none());
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
    fn test_expired_tokens_evicted() {
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

    #[test]
    fn test_cache_put_at_capacity() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);
        // Populate cache to capacity
        let original_keys: Vec<TestKey> =
            (0..CACHE_SIZE).map(|key_num| build_test_key(&key_num.to_string(), "")).collect();
        for key in original_keys.iter() {
            token_cache.put(key.clone(), build_test_token(LONG_EXPIRY, ""));
        }

        // Add one token past capacity
        let new_key = build_test_key("extra", "");
        token_cache.put(new_key.clone(), build_test_token(LONG_EXPIRY, ""));

        // New token should be present, one random original token should be evicted.
        assert!(token_cache.get(&new_key).is_some());
        assert_eq!(original_keys.iter().filter(|key| token_cache.get(*key).is_none()).count(), 1);
        assert_eq!(token_cache.token_map.len(), CACHE_SIZE);

        // Adding a duplicate key when cache is at capacity shouldn't evict anything.
        let mut token_cache = TokenCache::new(CACHE_SIZE);
        for key in original_keys.iter() {
            token_cache.put(key.clone(), build_test_token(LONG_EXPIRY, ""));
        }
        let dup_key = original_keys.iter().next().unwrap().clone();
        token_cache.put(dup_key, build_test_token(LONG_EXPIRY, "duplicate"));
        assert!(original_keys.iter().all(|key| token_cache.get(key).is_some()));
        assert_eq!(token_cache.token_map.len(), CACHE_SIZE);
    }

    #[test]
    fn test_cache_enforces_maximum_size() {
        let keys: Vec<TestKey> =
            (0..CACHE_SIZE).map(|key_num| build_test_key(&key_num.to_string(), "")).collect();

        // Verify cache after forcing eviction queue flush by replacing every key.
        let mut token_cache = TokenCache::new(CACHE_SIZE);
        for key in keys.iter() {
            token_cache.put(key.clone(), build_test_token(LONG_EXPIRY, "original"));
        }
        for key in keys.iter() {
            token_cache.put(key.clone(), build_test_token(LONG_EXPIRY, "overwrite"));
        }
        assert_eq!(token_cache.token_map.len(), CACHE_SIZE);
        assert!(keys.iter().all(|key| token_cache.get(key).is_some()));

        // Verify cache contains only CACHE_SIZE entries after trying to overfill cache.
        let mut token_cache = TokenCache::new(CACHE_SIZE);
        let too_many_keys: Vec<TestKey> =
            (0..CACHE_SIZE * 2).map(|key_num| build_test_key(&key_num.to_string(), "")).collect();
        for key in too_many_keys.iter() {
            token_cache.put(key.clone(), build_test_token(LONG_EXPIRY, ""));
        }
        assert_eq!(token_cache.token_map.len(), CACHE_SIZE);
        assert_eq!(
            too_many_keys.iter().filter(|key| token_cache.get(*key).is_some()).count(),
            CACHE_SIZE
        );
    }
}
