// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
extern crate failure;
extern crate fidl_fuchsia_auth;

use failure::Error;
use std::borrow::Cow;
use std::collections::HashMap;
use std::ops::Deref;
use std::sync::Arc;
use std::time::{Duration, Instant};

/// Constant offset for the cache expiry. Tokens will only be returned from
/// the cache if they have at least this much life remaining.
const PADDING_FOR_TOKEN_EXPIRY: Duration = Duration::from_secs(600);

#[derive(Debug, PartialEq, Eq, Fail)]
pub enum AuthCacheError {
    #[fail(display = "invalid argument")]
    InvalidArguments,
    #[fail(display = "supplied key not found in cache")]
    KeyNotFound,
}

/// Representation of a single OAuth token including its expiry time.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct OAuthToken {
    expiry_time: Instant,
    token: String,
}

impl Deref for OAuthToken {
    type Target = str;

    fn deref(&self) -> &str {
        &*self.token
    }
}

impl From<fidl_fuchsia_auth::AuthToken> for OAuthToken {
    fn from(auth_token: fidl_fuchsia_auth::AuthToken) -> OAuthToken {
        OAuthToken {
            expiry_time: Instant::now() + Duration::from_secs(auth_token.expires_in),
            token: auth_token.token,
        }
    }
}

/// Representation of a single Firebase token including its expiry time.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FirebaseAuthToken {
    id_token: String,
    local_id: Option<String>,
    email: Option<String>,
    expiry_time: Instant,
}

impl From<fidl_fuchsia_auth::FirebaseToken> for FirebaseAuthToken {
    fn from(firebase_token: fidl_fuchsia_auth::FirebaseToken) -> FirebaseAuthToken {
        FirebaseAuthToken {
            id_token: firebase_token.id_token,
            local_id: firebase_token.local_id,
            email: firebase_token.email,
            expiry_time: Instant::now() + Duration::from_secs(firebase_token.expires_in),
        }
    }
}

impl FirebaseAuthToken {
    /// Returns a new FIDL `FirebaseToken` using data cloned from our
    /// internal representation.
    pub fn to_fidl(&self) -> fidl_fuchsia_auth::FirebaseToken {
        let now = Instant::now();
        fidl_fuchsia_auth::FirebaseToken {
            id_token: self.id_token.clone(),
            local_id: self.local_id.clone(),
            email: self.email.clone(),
            expires_in: if self.expiry_time > now {
                (self.expiry_time - now).as_secs()
            } else {
                0
            },
        }
    }
}

/// A collection of OAuth and Firebase tokens associated with an identity
/// provider and profile. Any combination of ID tokens, access tokens,
/// and Firebase tokens may be present in a cache entry.
#[derive(Clone, Debug, PartialEq, Eq)]
struct TokenSet {
    /// A map from audience strings to cached OAuth ID tokens.
    id_token_map: HashMap<String, Arc<OAuthToken>>,
    /// A map from concatenatations of OAuth scope strings to cached OAuth Access
    /// tokens.
    access_token_map: HashMap<String, Arc<OAuthToken>>,
    /// A map from firebase API keys to cached Firebase tokens.
    firebase_token_map: HashMap<String, Arc<FirebaseAuthToken>>,
}

impl TokenSet {
    /// Constructs a new, empty, `TokenSet`.
    fn new() -> Self {
        TokenSet {
            id_token_map: HashMap::new(),
            access_token_map: HashMap::new(),
            firebase_token_map: HashMap::new(),
        }
    }

    /// Returns true iff this set contains at least one valid OAuth or Firebase
    /// token.
    fn is_valid(&self) -> bool {
        !self.id_token_map.is_empty() || !self.access_token_map.is_empty()
            || !self.firebase_token_map.is_empty()
    }

    /// Removes any expired OAuth and Firebase tokens from this set and
    /// returns true iff no valid OAuth tokens remain.
    fn remove_expired_tokens(&mut self) -> bool {
        let current_time = Instant::now();
        self.id_token_map
            .retain(|_, v| current_time <= (v.expiry_time - PADDING_FOR_TOKEN_EXPIRY));
        self.access_token_map
            .retain(|_, v| current_time <= (v.expiry_time - PADDING_FOR_TOKEN_EXPIRY));
        self.firebase_token_map
            .retain(|_, v| current_time <= (v.expiry_time - PADDING_FOR_TOKEN_EXPIRY));
        !self.is_valid()
    }
}

/// A unique key for accessing an entry in the token cache.
#[derive(Clone, Hash, PartialEq, Eq)]
pub struct CacheKey {
    idp_provider: String,
    idp_credential_id: String,
}

impl CacheKey {
    /// Create a new `CacheKey`, or returns `Error` if any input is empty.
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
    /// Creates a new `TokenCache` with the specified initial size.
    pub fn new(size: usize) -> TokenCache {
        TokenCache {
            map: HashMap::with_capacity(size),
        }
    }

    /// Returns the OAuth ID token for the specified `cache_key` and `audience` if present and
    /// not expired. This will cause any expired tokens on the same key to
    /// be purged from the underlying cache.
    pub fn get_id_token(
        &mut self,
        cache_key: &CacheKey,
        audience: &str,
    ) -> Option<Arc<OAuthToken>> {
        self.get_token_set(cache_key)
            .and_then(|ts| ts.id_token_map.get(audience).map(|t| t.clone()))
    }

    /// Returns an OAuth Access token for the specified `cache_key` and `scopes`,
    /// if present and not expired. This will cause any expired tokens on
    /// the same key to be purged from the underlying cache.
    pub fn get_access_token<T: Deref<Target = str>>(
        &mut self,
        cache_key: &CacheKey,
        scopes: &[T],
    ) -> Option<Arc<OAuthToken>> {
        self.get_token_set(cache_key).and_then(|ts| {
            ts.access_token_map
                .get(&*Self::scope_key(scopes))
                .map(|t| t.clone())
        })
    }

    /// Returns a Firebase token for the specified `cache_key` and `firebase_api_key`, if present and
    /// not expired. This will cause any expired tokens on the same key to
    /// be purged from the underlying cache.
    pub fn get_firebase_token(
        &mut self,
        cache_key: &CacheKey,
        firebase_api_key: &str,
    ) -> Option<Arc<FirebaseAuthToken>> {
        self.get_token_set(cache_key).and_then(|ts| {
            ts.firebase_token_map
                .get(firebase_api_key)
                .map(|t| t.clone())
        })
    }

    /// Returns the set of unexpired tokens stored in the cache for the given
    /// `cache_key`. Any expired tokens are also purged from the underlying cache.
    fn get_token_set(&mut self, cache_key: &CacheKey) -> Option<&TokenSet> {
        // First remove any expired tokens from the value if it exists then
        // delete the entire entry if this now means it is invalid.
        let mut expired_token_set = false;
        if let Some(token_set) = self.map.get_mut(cache_key) {
            expired_token_set = token_set.remove_expired_tokens();
        }
        if expired_token_set {
            self.map.remove(cache_key);
            return None;
        }

        // Any remaining key is now valid
        self.map.get(cache_key)
    }

    /// Adds an OAuth ID token to the cache, replacing any existing token for the same `cache_key` and `audience`.
    pub fn put_id_token(&mut self, cache_key: CacheKey, audience: String, token: Arc<OAuthToken>) {
        self.put_token(cache_key, |ts| {
            ts.id_token_map.insert(audience, token);
        });
    }

    /// Adds an OAuth Access token to the cache, replacing any existing token for the same `cache_key` and `scopes`.
    pub fn put_access_token<T: Deref<Target = str>>(
        &mut self,
        cache_key: CacheKey,
        scopes: &[T],
        token: Arc<OAuthToken>,
    ) {
        self.put_token(cache_key, |ts| {
            ts.access_token_map
                .insert(Self::scope_key(scopes).into_owned(), token);
        });
    }

    /// Adds a Firebase token to the cache, replacing any existing token for
    /// the same `cache_key` and `firebase_api_key`.
    pub fn put_firebase_token(
        &mut self,
        cache_key: CacheKey,
        firebase_api_key: String,
        token: Arc<FirebaseAuthToken>,
    ) {
        self.put_token(cache_key, |ts| {
            ts.firebase_token_map.insert(firebase_api_key, token);
        });
    }

    /// Adds a token to the cache, using a supplied fn to perform the token set
    /// manipulation.
    fn put_token<F>(&mut self, cache_key: CacheKey, update_fn: F)
    where
        F: FnOnce(&mut TokenSet),
    {
        if let Some(token_set) = self.map.get_mut(&cache_key) {
            update_fn(token_set);
            return;
        }
        let mut token_set = TokenSet::new();
        update_fn(&mut token_set);
        self.map.insert(cache_key, token_set);
    }

    /// Removes all tokens associated with the supplied `cache_key`, returning an error
    /// if none exist.
    pub fn delete(&mut self, cache_key: &CacheKey) -> Result<(), AuthCacheError> {
        if !self.map.contains_key(cache_key) {
            Err(AuthCacheError::KeyNotFound)
        } else {
            self.map.remove(cache_key);
            Ok(())
        }
    }

    /// Returns true iff the supplied `cache_key` is present in this cache.
    pub fn has_key(&self, cache_key: &CacheKey) -> bool {
        self.map.contains_key(cache_key)
    }

    /// Constructs an access token hashing key based on a vector of OAuth scope
    /// strings.
    fn scope_key<'a, T: Deref<Target = str>>(scopes: &'a [T]) -> Cow<'a, str> {
        // Use the scope strings concatenated with a newline as the key. Note that this
        // is order dependent; a client that reqested the same scopes with two
        // different orders would create two cache entries. We argue that the
        // harm of this is limited compared to the cost of sorting scopes to
        // create a canonical ordering on every access. Most clients are likely
        // to use a consistent order anyway and we request this behaviour in the
        // interface. TODO(jsankey): Consider a zero-copy solution for the
        // simple case of a single scope.
        match scopes.len() {
            0 => Cow::Borrowed(""),
            1 => Cow::Borrowed(scopes.first().unwrap()),
            _ => Cow::Owned(scopes.iter().fold(String::new(), |acc, el| {
                let sep = if acc.is_empty() { "" } else { "\n" };
                acc + sep + el
            })),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_auth::TokenType;

    const CACHE_SIZE: usize = 3;

    const TEST_IDP: &str = "test.com";
    const TEST_CREDENTIAL_ID: &str = "test.com/profiles/user";
    const TEST_EMAIL: &str = "user@test.com";
    const TEST_AUDIENCE: &str = "test_audience";
    const TEST_SCOPE_A: &str = "test_scope_a";
    const TEST_SCOPE_B: &str = "test_scope_b";
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

    fn build_test_id_token(time_until_expiry: Duration, suffix: &str) -> Arc<OAuthToken> {
        Arc::new(OAuthToken {
            expiry_time: Instant::now() + time_until_expiry,
            token: TEST_ID_TOKEN.to_string() + suffix,
        })
    }

    fn build_test_access_token(time_until_expiry: Duration, suffix: &str) -> Arc<OAuthToken> {
        Arc::new(OAuthToken {
            expiry_time: Instant::now() + time_until_expiry,
            token: TEST_ACCESS_TOKEN.to_string() + suffix,
        })
    }

    fn build_test_firebase_token(
        time_until_expiry: Duration,
        suffix: &str,
    ) -> Arc<FirebaseAuthToken> {
        Arc::new(FirebaseAuthToken {
            expiry_time: Instant::now() + time_until_expiry,
            id_token: TEST_FIREBASE_ID_TOKEN.to_string() + suffix,
            local_id: Some(TEST_FIREBASE_LOCAL_ID.to_string() + suffix),
            email: Some(TEST_EMAIL.to_string()),
        })
    }

    #[test]
    fn test_oauth_from_fidl() {
        let fidl_type = fidl_fuchsia_auth::AuthToken {
            token_type: TokenType::AccessToken,
            expires_in: LONG_EXPIRY.as_secs(),
            token: TEST_ACCESS_TOKEN.to_string(),
        };

        let time_before_conversion = Instant::now();
        let native_type = OAuthToken::from(fidl_type);
        let time_after_conversion = Instant::now();

        assert_eq!(&native_type.token, TEST_ACCESS_TOKEN);
        assert!(native_type.expiry_time >= time_before_conversion + LONG_EXPIRY);
        assert!(native_type.expiry_time <= time_after_conversion + LONG_EXPIRY);

        // Also verify our implementation of the Deref trait
        assert_eq!(&*native_type, TEST_ACCESS_TOKEN);
    }

    #[test]
    fn test_firebase_from_fidl() {
        let fidl_type = fidl_fuchsia_auth::FirebaseToken {
            id_token: TEST_FIREBASE_ID_TOKEN.to_string(),
            local_id: Some(TEST_FIREBASE_LOCAL_ID.to_string()),
            email: Some(TEST_EMAIL.to_string()),
            expires_in: LONG_EXPIRY.as_secs(),
        };

        let time_before_conversion = Instant::now();
        let native_type = FirebaseAuthToken::from(fidl_type);
        let time_after_conversion = Instant::now();

        assert_eq!(&native_type.id_token, TEST_FIREBASE_ID_TOKEN);
        assert_eq!(native_type.local_id, Some(TEST_FIREBASE_LOCAL_ID.to_string()));
        assert_eq!(native_type.email, Some(TEST_EMAIL.to_string()));
        assert!(native_type.expiry_time >= time_before_conversion + LONG_EXPIRY);
        assert!(native_type.expiry_time <= time_after_conversion + LONG_EXPIRY);
    }

    #[test]
    fn test_firebase_to_fidl() {
        let time_before_conversion = Instant::now();
        let native_type = FirebaseAuthToken {
            id_token: TEST_FIREBASE_ID_TOKEN.to_string(),
            local_id: Some(TEST_FIREBASE_LOCAL_ID.to_string()),
            email: Some(TEST_EMAIL.to_string()),
            expiry_time: time_before_conversion + LONG_EXPIRY,
        };

        let fidl_type = native_type.to_fidl();
        let elapsed_time_during_conversion = Instant::now().duration_since(time_before_conversion);

        assert_eq!(&fidl_type.id_token, TEST_FIREBASE_ID_TOKEN);
        assert_eq!(fidl_type.local_id, Some(TEST_FIREBASE_LOCAL_ID.to_string()));
        assert_eq!(fidl_type.email, Some(TEST_EMAIL.to_string()));
        assert!(fidl_type.expires_in <= LONG_EXPIRY.as_secs());
        assert!(fidl_type.expires_in >= (LONG_EXPIRY.as_secs() - elapsed_time_during_conversion.as_secs()) - 1);
    }

    #[test]
    fn test_get_and_put_id_token() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);

        // Verify requesting an entry from an cache that does not contain it fails.
        let key = build_test_cache_key("");
        assert_eq!(token_cache.get_id_token(&key, TEST_AUDIENCE), None);

        // Verify inserting then retrieving a token succeeds.
        let token_1 = build_test_id_token(LONG_EXPIRY, "1");
        token_cache.put_id_token(key.clone(), TEST_AUDIENCE.to_string(), token_1.clone());
        assert_eq!(
            token_cache.get_id_token(&key, TEST_AUDIENCE),
            Some(token_1.clone())
        );

        // Verify a second token on a different audience can be stored in the key
        // without conflict.
        let audience_2 = "";
        let token_2 = build_test_id_token(LONG_EXPIRY, "2");
        assert_eq!(token_cache.get_id_token(&key, audience_2), None);
        token_cache.put_id_token(key.clone(), audience_2.to_string(), token_2.clone());
        assert_eq!(token_cache.get_id_token(&key, audience_2), Some(token_2));
    }

    #[test]
    fn test_get_and_put_access_token() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);

        // Verify requesting an entry from an cache that does not contain it fails.
        let key = build_test_cache_key("");
        let scopes = vec![TEST_SCOPE_A, TEST_SCOPE_B];
        assert_eq!(token_cache.get_access_token(&key, &scopes), None);

        // Verify inserting then retrieving a token succeeds.
        let token_1 = build_test_access_token(LONG_EXPIRY, "1");
        token_cache.put_access_token(key.clone(), &scopes, token_1.clone());
        assert_eq!(
            token_cache.get_access_token(&key, &scopes),
            Some(token_1.clone())
        );

        // We don't create a canonical ordering of scopes, so can store a different
        // token with the same scopes in reverse order.
        let reversed_scopes = vec![TEST_SCOPE_B, TEST_SCOPE_A];
        let token_2 = build_test_id_token(LONG_EXPIRY, "2");
        token_cache.put_access_token(key.clone(), &reversed_scopes, token_2.clone());
        assert_eq!(
            token_cache.get_access_token(&key, &reversed_scopes),
            Some(token_2)
        );

        // Check that storing with a single scope and an empty scope vector also work.
        let single_scope = vec![TEST_SCOPE_A];
        let token_3 = build_test_id_token(LONG_EXPIRY, "3");
        token_cache.put_access_token(key.clone(), &single_scope, token_3.clone());
        assert_eq!(
            token_cache.get_access_token(&key, &single_scope),
            Some(token_3)
        );
        let no_scopes: Vec<String> = vec![];
        let token_4 = build_test_id_token(LONG_EXPIRY, "4");
        token_cache.put_access_token(key.clone(), &no_scopes, token_4.clone());
        assert_eq!(
            token_cache.get_access_token(&key, &no_scopes),
            Some(token_4)
        );

        // And finally check that we didn't dork up the original entry.
        assert_eq!(
            token_cache.get_access_token(&key, &scopes),
            Some(token_1.clone())
        );
    }

    #[test]
    fn test_has_key() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);
        let key = build_test_cache_key("");
        assert_eq!(token_cache.has_key(&key), false);
        token_cache.put_id_token(
            key.clone(),
            TEST_AUDIENCE.to_string(),
            build_test_id_token(LONG_EXPIRY, ""),
        );
        assert_eq!(token_cache.has_key(&key), true);
    }

    #[test]
    fn test_delete() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);
        let key = build_test_cache_key("");
        assert_eq!(token_cache.delete(&key), Err(AuthCacheError::KeyNotFound));
        token_cache.put_access_token(
            key.clone(),
            &vec![TEST_SCOPE_A],
            build_test_access_token(LONG_EXPIRY, ""),
        );
        assert_eq!(token_cache.has_key(&key), true);
        assert_eq!(token_cache.delete(&key), Ok(()));
        assert_eq!(token_cache.has_key(&key), false);
    }

    #[test]
    fn test_remove_oauth_on_expiry() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);

        // Insert one entry thats already expired and one that hasn't.
        let scopes = vec![TEST_SCOPE_A, TEST_SCOPE_B];
        let key_1 = build_test_cache_key("1");
        let access_token_1 = build_test_access_token(LONG_EXPIRY, "1");
        token_cache.put_access_token(key_1.clone(), &scopes, access_token_1.clone());
        let key_2 = build_test_cache_key("2");
        let access_token_2 = build_test_access_token(ALREADY_EXPIRED, "2");
        token_cache.put_access_token(key_2.clone(), &scopes, access_token_2.clone());

        // Both keys should be present.
        assert_eq!(token_cache.has_key(&key_1), true);
        assert_eq!(token_cache.has_key(&key_2), true);

        // Getting the expired key should fail and remove it from the cache.
        assert_eq!(
            token_cache.get_access_token(&key_1, &scopes),
            Some(access_token_1)
        );
        assert_eq!(token_cache.get_access_token(&key_2, &scopes), None);
        assert_eq!(token_cache.has_key(&key_1), true);
        assert_eq!(token_cache.has_key(&key_2), false);
    }

    #[test]
    fn test_get_and_put_firebase_token() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);

        // Create a new entry in the cache without any firebase tokens.
        let key = build_test_cache_key("1");
        token_cache.put_id_token(
            key.clone(),
            TEST_AUDIENCE.to_string(),
            build_test_access_token(LONG_EXPIRY, ""),
        );
        assert_eq!(token_cache.get_firebase_token(&key, TEST_API_KEY), None);

        // Add a firebase token and verify it can be retreived.
        let firebase_token = build_test_firebase_token(LONG_EXPIRY, "");
        token_cache.put_firebase_token(
            key.clone(),
            TEST_API_KEY.to_string(),
            firebase_token.clone(),
        );
        assert_eq!(
            token_cache.get_firebase_token(&key, TEST_API_KEY),
            Some(firebase_token)
        );
    }

    #[test]
    fn test_remove_firebase_on_expiry() {
        let mut token_cache = TokenCache::new(CACHE_SIZE);

        // Create a new entry in the cache without any firebase tokens.
        let key = build_test_cache_key("1");

        // Add two firebase tokens, one expired, one not.
        let firebase_token_1 = build_test_firebase_token(LONG_EXPIRY, "1");
        let firebase_api_key_1 = TEST_API_KEY.to_string() + "1";
        token_cache.put_firebase_token(
            key.clone(),
            firebase_api_key_1.clone(),
            firebase_token_1.clone(),
        );
        let firebase_token_2 = build_test_firebase_token(ALREADY_EXPIRED, "2");
        let firebase_api_key_2 = TEST_API_KEY.to_string() + "2";
        token_cache.put_firebase_token(key.clone(), firebase_api_key_2.clone(),
         firebase_token_2.clone());

        // Verify only the not expired token is accessible.
        assert_eq!(
            token_cache.get_firebase_token(&key, &firebase_api_key_1),
            Some(firebase_token_1)
        );
        assert_eq!(token_cache.get_firebase_token(&key, &firebase_api_key_2), None);
    }
}
