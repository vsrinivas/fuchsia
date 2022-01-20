// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::configuration::ServerParameters;
use crate::protocol::{identifier::ClientIdentifier, DhcpOption, OptionCode};
use crate::server::{ClientRecords, DataStore, LeaseRecord};
use std::collections::{HashMap, HashSet};
use std::str::FromStr as _;
use std::string::ToString as _;

/// A wrapper around a `fuchsia.stash.StoreAccessor` proxy.
///
/// Stash provides a simple API by which the DHCP `Server` can store and load client configuration
/// data to persistent storage.
///
/// This wrapper stores client configuration as serialized JSON strings. The decision to use JSON
/// derives from its use in other Stash clients, cf. commit e9c57a0, and the relative immaturity of
/// more compact serde serialization formats, e.g. https://github.com/pyfisch/cbor/issues.
#[derive(Clone, Debug)]
pub struct Stash {
    prefix: String,
    proxy: fidl_fuchsia_stash::StoreAccessorProxy,
}

#[derive(Debug, thiserror::Error)]
pub enum StashError {
    #[error("stash initialized with empty prefix")]
    EmptyPrefix,
    #[error(
        "stash initialized with prefix={prefix} containing invalid character(s) {invalid_chars:?}"
    )]
    InvalidPrefix { prefix: String, invalid_chars: HashSet<char> },
    #[error("unexpected value variant stored in stash: {actual:?}, expected {expected:?}")]
    UnexpectedStashValue { actual: fidl_fuchsia_stash::Value, expected: fidl_fuchsia_stash::Value },
    #[error("failed to deserialize json string={0}")]
    JsonDeserialization(String, #[source] serde_json::Error),
    #[error("failed to serialize to json for key={0}")]
    JsonSerialization(String, #[source] serde_json::Error),
    #[error("stash does not contain value for key={0}")]
    MissingValue(String),
    #[error("FIDL call to stash failed: {0}")]
    Fidl(#[from] fidl::Error),
    #[error("connecting to stash failed")]
    StashConnect(#[source] anyhow::Error),
}

impl DataStore for Stash {
    type Error = StashError;

    fn insert(
        &mut self,
        client_id: &ClientIdentifier,
        client_record: &LeaseRecord,
    ) -> Result<(), Self::Error> {
        self.store(&self.client_key(client_id), client_record)
    }

    fn store_options(&mut self, opts: &[DhcpOption]) -> Result<(), Self::Error> {
        self.store(OPTIONS_KEY, opts)
    }

    fn store_parameters(&mut self, params: &ServerParameters) -> Result<(), Self::Error> {
        self.store(PARAMETERS_KEY, params)
    }

    fn delete(&mut self, client_id: &ClientIdentifier) -> Result<(), Self::Error> {
        self.rm_key(&self.client_key(client_id))
    }
}

const OPTIONS_KEY: &'static str = "options";
const PARAMETERS_KEY: &'static str = "parameters";
const CLIENT_KEY_PREFIX: &'static str = "client";

impl Stash {
    /// Instantiates a new `Stash` value.
    ///
    /// The newly instantiated value will use `id` to identify itself with the `fuchsia.stash`
    /// service.
    pub fn new(id: &str) -> Result<Self, StashError> {
        Self::new_with_prefix(id, CLIENT_KEY_PREFIX)
    }

    fn new_with_prefix(id: &str, prefix: &str) -> Result<Self, StashError> {
        if prefix.is_empty() {
            return Err(StashError::EmptyPrefix);
        }
        let invalid_chars: HashSet<char> =
            prefix.matches(&['-', ':'][..]).map(|s| char::from_str(s).unwrap()).collect();
        if !invalid_chars.is_empty() {
            return Err(StashError::InvalidPrefix { prefix: prefix.to_string(), invalid_chars });
        }
        let store_client = fuchsia_component::client::connect_to_protocol::<
            fidl_fuchsia_stash::SecureStoreMarker,
        >()
        .map_err(StashError::StashConnect)?;
        let () = store_client.identify(id)?;
        let (proxy, accessor_server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_stash::StoreAccessorMarker>()?;
        let () = store_client.create_accessor(false, accessor_server)?;
        let prefix = prefix.to_string();
        Ok(Stash { prefix, proxy })
    }

    fn store<T>(&self, key: &str, v: &T) -> Result<(), StashError>
    where
        T: serde::Serialize + std::fmt::Debug + ?Sized,
    {
        let mut v = fidl_fuchsia_stash::Value::Stringval(
            serde_json::to_string(v)
                .map_err(|e| StashError::JsonSerialization(key.to_string(), e))?,
        );
        let () = self.proxy.set_value(key, &mut v)?;
        let () = self.proxy.commit()?;
        Ok(())
    }

    /// Loads a `ClientRecords` map from data stored in `fuchsia.stash`.
    ///
    /// This function will retrieve all client configuration data from `fuchsia.stash`, deserialize
    /// the JSON string values, and load the resulting structured data into a `ClientRecords`
    /// hashmap. Any key-value pair which could not be parsed or deserialized will be removed and
    /// skipped.
    pub async fn load_client_records(&self) -> Result<ClientRecords, StashError> {
        use futures::TryStreamExt as _;

        let (iter, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_stash::GetIteratorMarker>()?;
        let () = self.proxy.get_prefix(&self.prefix, server)?;
        futures::stream::try_unfold(iter, |iter| async move {
            let kvs = iter.get_next().await?;
            let yielded = (!kvs.is_empty()).then(|| (kvs, iter));
            Result::<_, StashError>::Ok(yielded)
        })
        .map_ok(|kvs| futures::stream::iter(kvs.into_iter().map(Ok)))
        .try_flatten()
        .try_filter_map(|kv| async move {
            let key = match kv.key.split("-").last() {
                Some(v) => v,
                None => {
                    // Invalid key-value pair: remove the invalid pair and try the next one.
                    log::warn!("failed to parse key string: {}", kv.key);
                    let () = self.rm_key(&kv.key)?;
                    return Ok(None);
                }
            };
            let key = match ClientIdentifier::from_str(key) {
                Ok(v) => v,
                Err(e) => {
                    log::warn!("client id from string conversion failed: {}", e);
                    let () = self.rm_key(&kv.key)?;
                    return Ok(None);
                }
            };
            let val = match kv.val {
                fidl_fuchsia_stash::Value::Stringval(v) => v,
                v => {
                    log::warn!("invalid value variant stored in stash: {:?}", v);
                    let () = self.rm_key(&kv.key)?;
                    return Ok(None);
                }
            };
            let val: LeaseRecord = match serde_json::from_str(&val) {
                Ok(v) => v,
                Err(e) => {
                    log::warn!("failed to parse JSON from string: {}", e);
                    let () = self.rm_key(&kv.key)?;
                    return Ok(None);
                }
            };
            Ok(Some((key, val)))
        })
        .try_collect()
        .await
    }

    /// Loads a map of `OptionCode`s to `DhcpOption`s from data stored in `fuchsia.stash`.
    pub async fn load_options(&self) -> Result<HashMap<OptionCode, DhcpOption>, StashError> {
        let val = self.proxy.get_value(&OPTIONS_KEY.to_string()).await?;
        let val = match val {
            Some(v) => v,
            None => return Ok(HashMap::new()),
        };
        match *val {
            fidl_fuchsia_stash::Value::Stringval(v) => {
                Ok(serde_json::from_str::<Vec<DhcpOption>>(&v)
                    .map_err(|e| StashError::JsonDeserialization(v.clone(), e))?
                    .into_iter()
                    .map(|opt| (opt.code(), opt))
                    .collect())
            }
            v => Err(StashError::UnexpectedStashValue {
                actual: v,
                expected: fidl_fuchsia_stash::Value::Stringval(String::new()),
            }),
        }
    }

    /// Loads a new instance of `ServerParameters` from data stored in `fuchsia.stash`.
    pub async fn load_parameters(&self) -> Result<ServerParameters, StashError> {
        let val = self
            .proxy
            .get_value(&PARAMETERS_KEY.to_string())
            .await?
            .ok_or(StashError::MissingValue(PARAMETERS_KEY.to_string()))?;
        match *val {
            fidl_fuchsia_stash::Value::Stringval(v) => Ok(serde_json::from_str(&v)
                .map_err(|e| StashError::JsonDeserialization(v.clone(), e))?),
            v => Err(StashError::UnexpectedStashValue {
                actual: v,
                expected: fidl_fuchsia_stash::Value::Stringval(String::new()),
            }),
        }
    }

    fn rm_key(&self, key: &str) -> Result<(), StashError> {
        let () = self.proxy.delete_value(key)?;
        let () = self.proxy.commit()?;
        Ok(())
    }

    /// Clears all configuration data from `fuchsia.stash`.
    ///
    /// This function will delete all key-value pairs associated with the `Stash` value.
    pub fn clear(&self) -> Result<(), StashError> {
        let () = self.proxy.delete_prefix(&self.prefix)?;
        let () = self.proxy.commit()?;
        Ok(())
    }

    #[cfg(test)]
    pub fn clone_proxy(&self) -> fidl_fuchsia_stash::StoreAccessorProxy {
        self.proxy.clone()
    }

    pub(crate) fn client_key(&self, client_id: &ClientIdentifier) -> String {
        format!("{}-{}", self.prefix, client_id)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::configuration::{LeaseLength, ManagedAddresses};
    use net_declare::std::ip_v4;
    use std::convert::TryFrom as _;

    /// Creates a new stash instance with a randomized identifier to prevent test flakes.
    ///
    /// `prefix` must not contain either '-' or ':' as they are used as field delimiters in stash
    /// keys.
    fn new_stash(test_prefix: &str) -> Result<(Stash, String), StashError> {
        use rand::distributions::DistString as _;
        let rand_id = rand::distributions::Alphanumeric.sample_string(&mut rand::thread_rng(), 8);
        let stash = Stash::new_with_prefix(&rand_id, test_prefix)?;
        // Clear the Stash of any data leftover from the previous test.
        let () = stash.proxy.delete_prefix(&stash.prefix)?;
        let () = stash.proxy.commit()?;
        Ok((stash, rand_id))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn stash_new_with_prefix() {
        assert_matches::assert_matches!(
            Stash::new_with_prefix("stash_new", "valid"),
            Ok(Stash { .. })
        );
        assert_matches::assert_matches!(
            Stash::new_with_prefix("stash_new", "invalid-"),
            Err(StashError::InvalidPrefix { .. })
        );
        assert_matches::assert_matches!(
            Stash::new_with_prefix("stash_new", "invalid:"),
            Err(StashError::InvalidPrefix { .. })
        );
        assert_matches::assert_matches!(
            Stash::new_with_prefix("stash_new", ""),
            Err(StashError::EmptyPrefix)
        );
        let () = match Stash::new_with_prefix("stash_new", "a-b-c-d") {
            Err(StashError::InvalidPrefix { invalid_chars, prefix: _prefix }) => {
                assert_eq!(invalid_chars, ['-'].iter().cloned().collect())
            }
            v => {
                panic!("new_with_prefix returned {:?}, expected StashError::InvalidPrefix{{..}}", v)
            }
        };
        let () = match Stash::new_with_prefix("stash_new", "a-b-c:d-e") {
            Err(StashError::InvalidPrefix { invalid_chars, prefix: _prefix }) => {
                assert_eq!(invalid_chars, ['-', ':'].iter().cloned().collect())
            }
            v => {
                panic!("new_with_prefix returned {:?}, expected StashError::InvalidPrefix{{..}}", v)
            }
        };
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn store_client_succeeds() {
        let (mut stash, id) =
            new_stash("store_client_succeeds").expect("failed to create new stash");
        let accessor_client = stash.proxy.clone();

        // Store value in stash.
        let client_id = ClientIdentifier::from(crate::server::tests::random_mac_generator());
        let client_record = LeaseRecord::default();
        let () = stash
            .insert(&client_id, &client_record)
            .unwrap_or_else(|err| panic!("failed to store client in {}: {:?}", id, err));

        // Verify value actually stored in stash.
        let value = accessor_client
            .get_value(&stash.client_key(&client_id))
            .await
            .unwrap_or_else(|err| panic!("failed to get value from {}: {:?}", id, err));
        let value = match *value.unwrap() {
            fidl_fuchsia_stash::Value::Stringval(v) => v,
            v => panic!("stored value is not a string: {:?}", v),
        };
        let value: LeaseRecord =
            serde_json::from_str(&value).expect("failed to decode lease record");
        assert_eq!(value, client_record);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn store_options_succeeds() {
        let (mut stash, id) = new_stash("store_options_succeeds").expect("failed to create stash");
        let accessor_client = stash.proxy.clone();

        let opts = vec![
            DhcpOption::SubnetMask(ip_v4!("255.255.255.0")),
            DhcpOption::DomainNameServer(vec![ip_v4!("1.2.3.4"), ip_v4!("4.3.2.1")]),
        ];
        let () = stash.store_options(&opts).expect("failed to store options in stash");
        let value = accessor_client
            .get_value(&OPTIONS_KEY.to_string())
            .await
            .unwrap_or_else(|err| panic!("failed to get value from {}: {:?}", id, err));

        let value = match *value.unwrap() {
            fidl_fuchsia_stash::Value::Stringval(v) => v,
            v => panic!("stored value is not a string: {:?}", v),
        };
        let value: Vec<DhcpOption> = serde_json::from_str(&value)
            .unwrap_or_else(|err| panic!("failed to deserialize from {}: {:?}", value, err));
        assert_eq!(value, opts);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn store_parameters_succeeds() {
        let (mut stash, id) =
            new_stash("store_parameters_succeeds").expect("failed to create stash");
        let accessor_client = stash.proxy.clone();

        let params = ServerParameters {
            server_ips: vec![ip_v4!("192.168.0.1")],
            lease_length: LeaseLength { default_seconds: 42, max_seconds: 100 },
            managed_addrs: ManagedAddresses {
                mask: crate::configuration::SubnetMask::try_from(24).unwrap(),
                pool_range_start: ip_v4!("192.168.0.10"),
                pool_range_stop: ip_v4!("192.168.0.254"),
            },
            permitted_macs: crate::configuration::PermittedMacs(Vec::new()),
            static_assignments: crate::configuration::StaticAssignments(HashMap::new()),
            arp_probe: false,
            bound_device_names: vec![],
        };
        let () = stash.store_parameters(&params).expect("failed to store parameters");
        let value = accessor_client
            .get_value(&PARAMETERS_KEY.to_string())
            .await
            .unwrap_or_else(|err| panic!("failed to get value from {}: {:?}", id, err));

        let value = match *value.unwrap() {
            fidl_fuchsia_stash::Value::Stringval(v) => v,
            v => panic!("stored value is not a string: {:?}", v),
        };
        let value: ServerParameters = serde_json::from_str(&value)
            .unwrap_or_else(|err| panic!("failed to deserialize from {}: {:?}", value, err));
        assert_eq!(value, params);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_clients_with_populated_stash_returns_cached_clients() {
        let (stash, id) = new_stash("load_clients_with_populated_stash_returns_cached_clients")
            .expect("failed to create stash");
        let accessor = stash.proxy.clone();

        let client_id = ClientIdentifier::from(crate::server::tests::random_mac_generator());
        let client_record = LeaseRecord::default();
        let serialized_client =
            serde_json::to_string(&client_record).expect("serialization failed");
        let client_key = stash.client_key(&client_id);
        let mut client_val = fidl_fuchsia_stash::Value::Stringval(serialized_client);
        let () = accessor
            .set_value(&client_key, &mut client_val)
            .unwrap_or_else(|err| panic!("failed to set value in {}: {:?}", id, err));
        let () = accessor.commit().unwrap_or_else(|err| {
            panic!("failed to commit stash state change in {}: {:?}", id, err)
        });

        let loaded_cache = stash
            .load_client_records()
            .await
            .unwrap_or_else(|err| panic!("failed to load map from stash in {}: {:?}", id, err));

        let cached_clients = std::iter::once((client_id, client_record)).collect();
        assert_eq!(loaded_cache, cached_clients);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_options_with_stashed_options_returns_options() {
        let (stash, id) = new_stash("load_options_with_stashed_options_returns_options")
            .expect("failed to create stash");
        let accessor = stash.proxy.clone();

        let opts = vec![
            DhcpOption::SubnetMask(ip_v4!("255.255.255.0")),
            DhcpOption::DomainNameServer(vec![ip_v4!("1.2.3.4"), ip_v4!("4.3.2.1")]),
        ];
        let serialized_opts = serde_json::to_string(&opts).expect("serialization failed");
        let opts = opts.into_iter().map(|o| (o.code(), o)).collect();
        let () = accessor
            .set_value(
                &OPTIONS_KEY.to_string(),
                &mut fidl_fuchsia_stash::Value::Stringval(serialized_opts),
            )
            .unwrap_or_else(|err| {
                panic!("failed to set value in stash for key={}: {:?}", OPTIONS_KEY, err)
            });
        let () = accessor.commit().unwrap_or_else(|err| {
            panic!("failed to commit stash state change in {}: {:?}", id, err)
        });

        let loaded_opts = stash.load_options().await.expect("failed to load options");
        assert_eq!(loaded_opts, opts);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_options_with_no_stashed_options_returns_empty_map() {
        let (stash, _id) = new_stash("load_options_with_no_stashed_options_returns_empty_vec")
            .expect("failed to create stash");

        let opts = stash.load_options().await.expect("failed to load options");
        assert_eq!(opts, HashMap::new());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_parameters_with_stashed_parameters_returns_parameters() {
        let (stash, id) = new_stash("load_parameters_with_stashed_parameters_returns_parameters")
            .expect("faield to create stash");
        let accessor = stash.proxy.clone();

        let params = ServerParameters {
            server_ips: vec![ip_v4!("192.168.0.1")],
            lease_length: LeaseLength { default_seconds: 42, max_seconds: 100 },
            managed_addrs: ManagedAddresses {
                mask: crate::configuration::SubnetMask::try_from(24).unwrap(),
                pool_range_start: ip_v4!("192.168.0.10"),
                pool_range_stop: ip_v4!("192.168.0.254"),
            },
            permitted_macs: crate::configuration::PermittedMacs(Vec::new()),
            static_assignments: crate::configuration::StaticAssignments(HashMap::new()),
            arp_probe: false,
            bound_device_names: vec![],
        };
        let serialized_params = serde_json::to_string(&params).expect("serialization failed");
        let () = accessor
            .set_value(
                &PARAMETERS_KEY.to_string(),
                &mut fidl_fuchsia_stash::Value::Stringval(serialized_params),
            )
            .unwrap_or_else(|err| {
                panic!("failed to set value in stash for key={}: {:?}", OPTIONS_KEY, err)
            });
        let () = accessor.commit().unwrap_or_else(|err| {
            panic!("failed to commit stash state change in {}: {:?}", id, err)
        });

        let loaded_params = stash.load_parameters().await.expect("failed to load parameters");
        assert_eq!(loaded_params, params);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_parameters_with_no_stashed_parameters_returns_err() {
        let (stash, _id) = new_stash("load_parameters_with_no_stashed_parameters_returns_err")
            .expect("failed to create stash");
        assert_matches::assert_matches!(
            stash.load_parameters().await.expect_err("load_parameters should have returned err"),
            StashError::MissingValue(String { .. })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_clients_with_stash_containing_invalid_entries_returns_empty_cache() {
        let (stash, id) =
            new_stash("load_clients_with_stash_containing_invalid_entries_returns_empty_cache")
                .expect("failed to create stash");
        let accessor = stash.proxy.clone();

        let client_id = ClientIdentifier::from(crate::server::tests::random_mac_generator());
        let client_record = LeaseRecord::default();
        let serialized_client =
            serde_json::to_string(&client_record).expect("serialization failed");
        let invalid_key = "invalid_key";
        let mut client_stringval = fidl_fuchsia_stash::Value::Stringval(serialized_client);
        let () = accessor.set_value(invalid_key, &mut client_stringval).unwrap_or_else(|err| {
            panic!("failed to set value in stash for key={}: {:?}", OPTIONS_KEY, err)
        });
        let client_key = stash.client_key(&client_id);
        let mut client_intval = fidl_fuchsia_stash::Value::Intval(42);
        let () = accessor.set_value(&client_key, &mut client_intval).unwrap_or_else(|err| {
            panic!("failed to set value in stash for key={}: {:?}", OPTIONS_KEY, err)
        });
        let () = accessor.commit().unwrap_or_else(|err| {
            panic!("failed to commit stash state change in {}: {:?}", id, err)
        });

        let loaded_cache = stash
            .load_client_records()
            .await
            .unwrap_or_else(|err| panic!("failed to load map from stash in {}: {:?}", id, err));

        let empty_cache = HashMap::new();
        assert_eq!(loaded_cache, empty_cache);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn delete_client_succeeds() {
        let (mut stash, id) = new_stash("delete_client_succeeds").expect("failed to create stash");
        let accessor = stash.proxy.clone();

        // Store value in stash.
        let client_id = ClientIdentifier::from(crate::server::tests::random_mac_generator());
        let client_record = LeaseRecord::default();
        let () = stash
            .insert(&client_id, &client_record)
            .unwrap_or_else(|err| panic!("failed to store client in {}: {:?}", id, err));

        // Verify value actually stored in stash.
        let client_key = stash.client_key(&client_id);
        let value = accessor
            .get_value(&client_key)
            .await
            .unwrap_or_else(|err| panic!("failed to get value from {}: {:?}", id, err));
        assert!(value.is_some());

        // Delete value and verify its absence.
        let () = stash
            .delete(&client_id)
            .unwrap_or_else(|err| panic!("failed to delete client in {}: {:?}", id, err));
        let value = accessor
            .get_value(&client_key)
            .await
            .unwrap_or_else(|err| panic!("failed to get value from {}: {:?}", id, err));
        assert!(value.is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn clear_with_populated_stash_clears_stash() {
        let (stash, id) =
            new_stash("clear_with_populated_stash_clears_stash").expect("failed to create stash");
        let accessor = stash.proxy.clone();

        // Store a value in the stash.
        let client_mac = ClientIdentifier::from(crate::server::tests::random_mac_generator());
        let client_record = LeaseRecord::default();
        let serialized_client =
            serde_json::to_string(&client_record).expect("serialization failed");
        let client_key = stash.client_key(&client_mac);
        let mut client_val = fidl_fuchsia_stash::Value::Stringval(serialized_client);
        let () = accessor.set_value(&client_key, &mut client_val).unwrap_or_else(|err| {
            panic!("failed to set value in stash for key={}: {:?}", OPTIONS_KEY, err)
        });
        let () = accessor.commit().unwrap_or_else(|err| {
            panic!("failed to commit stash state change in {}: {:?}", id, err)
        });

        // Clear the stash.
        let () = stash
            .clear()
            .unwrap_or_else(|err| panic!("failed to clear stash in {}: {:?}", id, err));

        // Verify that the stash is actually empty.
        let (iter, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_stash::GetIteratorMarker>()
                .expect("failed to create iterator for stash");
        let () = accessor.get_prefix(&stash.prefix, server).expect("failed to get prefix iterator");
        let stash_contents = iter.get_next().await.expect("failed to get next item for iterator");
        assert_eq!(stash_contents.len(), 0);
    }
}
