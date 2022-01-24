// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    lru_cache::LruCache,
    parking_lot::Mutex,
    rustls::{
        internal::msgs::{
            codec::{Codec, Reader},
            enums::{NamedGroup, ProtocolVersion},
            persist::ClientSessionValue,
        },
        StoresClientSessions,
    },
    std::{collections::VecDeque, fmt::Debug, sync::Arc},
};

// BoundedStack implements a stack that produces and consumes from the back, and evicts from the
// front when full.
#[derive(Debug)]
struct BoundedStack {
    jar: VecDeque<Vec<u8>>,
    size: usize,
}

impl BoundedStack {
    fn new(size: usize) -> Self {
        BoundedStack { jar: VecDeque::with_capacity(size), size }
    }

    fn push(&mut self, val: Vec<u8>) {
        if self.jar.len() == self.size {
            let _ = self.jar.pop_front();
        }

        self.jar.push_back(val)
    }

    fn pop(&mut self) -> Option<Vec<u8>> {
        self.jar.pop_back()
    }
}

enum CacheEntry {
    C4(BoundedStack),
    RFC5077(Vec<u8>),
    KX(Vec<u8>),
}

/// `C4CapableSessionCache` implements `StoresClientSessions` while supporting TLS 1.3 Client
/// Tracking Prevention as described in [RFC8446 Appendix C.4].
///
/// The `StoresClientSessions` API is overloaded in that it stores TLS 1.3 tickets, TLS 1.2
/// sessions, and TLS 1.3 key exchange hints. While the key exchange hints have guaranteed
/// different keys than tickets and sessions, the latter two share keyspace.
///
/// This is worked around by removing any overlapping keys. This is safe because this API provides
/// ownership of all values resolved through it, and they also must be refreshed via this API.
///
/// This cache is sized and items are evicted per LRU policy.
///
/// [RFC8446 Appendix C.4]: https://tools.ietf.org/html/rfc8446#appendix-C.4
pub struct C4CapableSessionCache {
    cache: Mutex<LruCache<Vec<u8>, CacheEntry>>,
}

impl C4CapableSessionCache {
    // Value chosen based on suggestion in C.4 that an HTTP/1.1 client might open 6 concurrent
    // connections. This value is also amenable to exhaustion in Happy Eyeballs races within 2
    // seconds (using default connection intervals).
    const TLS13_TICKET_COUNT: usize = 6;

    /// Create a new `C4CapableSessionCache` constrained by the supplied size. Note that this size
    /// includes TLS 1.3 tickets, TLS 1.2 sessions, and TLS 1.3 key exchange hints.
    pub fn new(size: usize) -> Arc<C4CapableSessionCache> {
        debug_assert!(size > 0);
        Arc::new(C4CapableSessionCache { cache: Mutex::new(LruCache::new(size)) })
    }
}

impl StoresClientSessions for C4CapableSessionCache {
    // Our `put` implementation evicts occupants on collision. This removes the need to have any
    // logic in `get` other than unwrapping `CacheEntry` types.
    fn put(&self, key: Vec<u8>, value: Vec<u8>) -> bool {
        let csv = ClientSessionValue::read(&mut Reader::init(&value));
        let mut locked_cache = self.cache.lock();
        match csv.map(|v| v.version) {
            Some(ProtocolVersion::TLSv1_3) => {
                // We need to do a lookup for TLS 1.3 tickets to determine whether to insert to an
                // existing C.4-compatible storage or whether we need to create a new one and evict
                // the current occupant.
                match locked_cache.get_mut(&key) {
                    Some(CacheEntry::C4(entry)) => {
                        let () = entry.push(value);
                    }
                    // Create a new LIFO for TLS 1.3 tickets at this cache entry, dropping whatever
                    // was there before.
                    Some(CacheEntry::RFC5077(_)) | Some(CacheEntry::KX(_)) | None => {
                        let mut c4 = BoundedStack::new(C4CapableSessionCache::TLS13_TICKET_COUNT);
                        let () = c4.push(value);
                        let _: Option<CacheEntry> = locked_cache.insert(key, CacheEntry::C4(c4));
                    }
                }
                true
            }

            // TLS versions prior to TLS 1.3 may resume with RFC5077 semantics.
            Some(ProtocolVersion::TLSv1_2)
            | Some(ProtocolVersion::TLSv1_1)
            | Some(ProtocolVersion::TLSv1_0) => {
                let _: Option<CacheEntry> = locked_cache.insert(key, CacheEntry::RFC5077(value));
                true
            }

            None => {
                // This didn't match a ProtocolVersion, probably because it isn't actually a
                // ClientSessionValue. Try to read it as a NamedGroup and, if successful, insert a
                // kx hint.
                match NamedGroup::read_bytes(&value) {
                    Some(_) => {
                        let _: Option<CacheEntry> = locked_cache.insert(key, CacheEntry::KX(value));
                        true
                    }
                    None => false,
                }
            }

            // These should never happen, but there's no need to panic about it.
            Some(ProtocolVersion::SSLv2)
            | Some(ProtocolVersion::SSLv3)
            | Some(ProtocolVersion::Unknown(_)) => false,
        }
    }

    // Because `put` evicts on key collision, `get` may directly return whatever occupant it finds.
    fn get(&self, key: &[u8]) -> Option<Vec<u8>> {
        let mut locked_cache = self.cache.lock();
        return locked_cache.get_mut(key).and_then(|entry| match entry {
            CacheEntry::C4(entry) => entry.pop(),
            CacheEntry::RFC5077(entry) | CacheEntry::KX(entry) => Some(entry.clone()),
        });
    }
}

#[cfg(test)]
mod test {
    use {
        crate::C4CapableSessionCache,
        assert_matches::assert_matches,
        rand::{thread_rng, Rng},
        rustls::{
            internal::msgs::{
                codec::{Codec, Reader},
                enums::{CipherSuite, ProtocolVersion},
                handshake::SessionID,
                persist::{ClientSessionKey, ClientSessionValue},
            },
            StoresClientSessions,
        },
        webpki::DNSNameRef,
    };

    enum KeyType {
        ForKX,
        ForTLS12,
        ForTLS13,
    }

    // Helper function to make a key/value pair to store in the session cache.
    fn make_kv_pair(dns_name: &str, key_type: KeyType) -> (Vec<u8>, Vec<u8>) {
        let sn = DNSNameRef::try_from_ascii_str(dns_name).expect("to parse SN");
        let csk = match key_type {
            KeyType::ForKX => ClientSessionKey::hint_for_dns_name(sn).get_encoding(),
            _ => ClientSessionKey::session_for_dns_name(sn).get_encoding(),
        };

        let mut session_id = [0u8; 32];
        thread_rng().fill(&mut session_id[..]);

        let csv = match key_type {
            KeyType::ForKX => vec![0u8, 29u8],
            KeyType::ForTLS12 => ClientSessionValue::new(
                ProtocolVersion::TLSv1_2,
                CipherSuite::TLS_NULL_WITH_NULL_NULL,
                &SessionID::new(&session_id),
                Vec::new(),
                Vec::new(),
                &Vec::new(),
            )
            .get_encoding(),
            KeyType::ForTLS13 => ClientSessionValue::new(
                ProtocolVersion::TLSv1_3,
                CipherSuite::TLS_NULL_WITH_NULL_NULL,
                &SessionID::new(&session_id),
                Vec::new(),
                Vec::new(),
                &Vec::new(),
            )
            .get_encoding(),
        };

        (csk, csv)
    }

    // This test checks that we can insert a session key for TLS 1.3, read the same value out, and
    // that this causes the cache to be empty for that key.
    #[test]
    fn test_tls13key_session_roundtrip() {
        let (csk, csv) = make_kv_pair("example.com", KeyType::ForTLS13);
        let csv_in = ClientSessionValue::read(&mut Reader::init(&csv))
            .expect("to parse initial ClientSessionValue");
        let cache = C4CapableSessionCache::new(1);
        assert_eq!(cache.put(csk.clone(), csv), true);

        let v0 = cache.get(&csk).expect("to get a value for the key");
        let csv_out = ClientSessionValue::read(&mut Reader::init(&v0))
            .expect("to parse retrieved ClientSessionValue");
        assert_eq!(csv_in.get_encoding(), csv_out.get_encoding());

        assert_eq!(true, cache.get(&csk).is_none());
    }

    // This test checks the various eviction semantics of the session cache. In particular, we're
    // looking to see that a full cache evicts with LRU semantics, we can round-trip items we
    // insert, and that key collision is resolved via eviction.
    #[test]
    fn test_eviction() {
        let (tls13_key, tls13_val) = make_kv_pair("example.com", KeyType::ForTLS13);
        let tls13_csv_in = ClientSessionValue::read(&mut Reader::init(&tls13_val))
            .expect("to parse initial TLS 1.3 ClientSessionValue");

        let (tls12_key, tls12_val) = make_kv_pair("example.com", KeyType::ForTLS12);
        let tls12_csv_in = ClientSessionValue::read(&mut Reader::init(&tls12_val))
            .expect("to parse initial TLS 1.2 ClientSessionValue");

        let (kx_key, kx_val) = make_kv_pair("example.com", KeyType::ForKX);

        let cache = C4CapableSessionCache::new(3);

        // These keys should be equivalent.
        assert_eq!(tls13_key, tls12_key);

        // Test we can read out the TLS 1.2 value we put in.
        assert_eq!(cache.put(tls12_key.clone(), tls12_val), true);
        let v1 = cache.get(&tls12_key).expect("to get a TLS 1.2 value for the key");
        let tls12_csv_out = ClientSessionValue::read(&mut Reader::init(&v1))
            .expect("to parse retrieved TLS 1.2 ClientSessionValue");
        assert_eq!(tls12_csv_in.get_encoding(), tls12_csv_out.get_encoding());

        // Test that inserting the TLS 1.3 value round-trips, and that we read it out even if we
        // use the key supplied for the TLS 1.2 value.
        assert_eq!(cache.put(tls13_key.clone(), tls13_val), true);
        let v0 = cache.get(&tls12_key).expect("to get a TLS 1.3 value for the key");
        let tls13_csv_out = ClientSessionValue::read(&mut Reader::init(&v0))
            .expect("to parse retrieved TLS 1.3 ClientSessionValue");
        assert_eq!(tls13_csv_in.get_encoding(), tls13_csv_out.get_encoding());

        // Test that insertion of the KX hint doesn't evict the TLS 1.3 key.
        assert_eq!(cache.put(kx_key.clone(), kx_val.clone()), true);
        let tls13_csv_out = ClientSessionValue::read(&mut Reader::init(&v0))
            .expect("to parse retrieved TLS 1.3 ClientSessionValue");
        assert_eq!(tls13_csv_in.get_encoding(), tls13_csv_out.get_encoding());
        let v3 = cache.get(&kx_key).expect("to get KX hint");
        assert_eq!(kx_val, v3);

        // Test that inserting more than three elements starts to evict the least-recently-used
        // ones.
        let (tls12p_key, tls12p_val) = make_kv_pair("example.net", KeyType::ForTLS12);
        let (kxp_key, kxp_val) = make_kv_pair("example.net", KeyType::ForKX);
        assert_eq!(cache.put(tls12p_key.clone(), tls12p_val), true);
        assert_eq!(cache.put(kxp_key.clone(), kxp_val.clone()), true);
        // The TLS 1.3 entry was the fourth last thing used, so it's gone.
        assert_matches!(cache.get(&tls12_key), None);

        // The KX hint, and KX' and TLS12' are all in the cache.
        assert_matches!(cache.get(&kx_key), Some(_));
        assert_matches!(cache.get(&tls12p_key), Some(_));
        assert_matches!(cache.get(&kxp_key), Some(_));
    }

    // Ensure that the BoundedStack returns values in LIFO order, and that it stores exactly
    // C4CapableSessionCache::TLS13_TICKET_COUNT items. This is tested via the
    // C4CapableSessionCache API to ensure those semantics are visible to rustls.
    #[test]
    fn test_tls13_cache_depth() {
        let mut tickets: Vec<(Vec<u8>, Vec<u8>)> = Vec::new();
        let cache = C4CapableSessionCache::new(1);

        // First, overfill the container.
        for _i in 0..=C4CapableSessionCache::TLS13_TICKET_COUNT {
            let (k, v) = make_kv_pair("example.com", KeyType::ForTLS13);

            assert_eq!(cache.put(k.clone(), v.clone()), true);
            tickets.push((k.clone(), v.clone()));
        }

        // Read out the first TLS13_TICKET_COUNT values.
        for _i in 0..C4CapableSessionCache::TLS13_TICKET_COUNT {
            let (k_in, v_in) = tickets.pop().expect("to retrieve a previously-enqueued ticket");
            let v_out =
                cache.get(&k_in.clone()).expect("to read a previously-enqueued ticket from cache");
            assert_eq!(v_in, v_out);
        }

        // Ensure that we didn't consume all initially inserted items, but there's also nothing
        // left in the cache.
        let (k, _) = tickets.pop().expect("one value still in ticket vec");
        assert_matches!(cache.get(&k.clone()), None);
    }
}
