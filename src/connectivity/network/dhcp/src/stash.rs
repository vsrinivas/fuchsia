// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::configuration::ServerParameters;
use crate::protocol::{identifier::ClientIdentifier, DhcpOption, OptionCode};
use crate::server::{CachedClients, CachedConfig};
use anyhow::{Context as _, Error};
use std::collections::HashMap;
use std::str::FromStr;
use std::string::ToString;

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

const OPTIONS_KEY: &'static str = "options";
const PARAMETERS_KEY: &'static str = "parameters";
const CLIENT_KEY_PREFIX: &'static str = "client";
#[cfg(test)]
pub(crate) const CLIENT_KEY_PREFIX_FOR_TEST: &'static str = CLIENT_KEY_PREFIX;

impl Stash {
    /// Instantiates a new `Stash` value.
    ///
    /// The newly instantiated value will use `id` to identify itself with the `fuchsia.stash`
    /// service.
    pub fn new(id: &str) -> Result<Self, Error> {
        Self::new_with_prefix(id, CLIENT_KEY_PREFIX)
    }

    fn new_with_prefix(id: &str, prefix: &str) -> Result<Self, Error> {
        if prefix.is_empty() {
            return Err(anyhow::anyhow!("empty prefix"));
        }
        if prefix.contains(&['-', ':'][..]) {
            return Err(anyhow::anyhow!("prefix contained invalid characters: {}", prefix));
        }
        let store_client = fuchsia_component::client::connect_to_service::<
            fidl_fuchsia_stash::SecureStoreMarker,
        >()
        .context("failed to connect to store")?;
        let () = store_client.identify(id).context("failed to identify client to store")?;
        let (proxy, accessor_server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_stash::StoreAccessorMarker>()
                .context("failed to create accessor proxy")?;
        let () = store_client
            .create_accessor(false, accessor_server)
            .context("failed to create accessor")?;
        let prefix = prefix.to_owned();
        Ok(Stash { prefix, proxy })
    }

    /// Stores the `client_config` value with the `client_id` key in `fuchsia.stash`.
    ///
    /// This function stores the `client_config` as a serialized JSON string.
    pub fn store_client_config<'a>(
        &'a self,
        client_id: &ClientIdentifier,
        client_config: &'a CachedConfig,
    ) -> Result<(), Error> {
        self.store(&self.client_key(client_id), client_config)
    }

    /// Stores `opts` in `fuchsia.stash`.
    ///
    /// This function stores the `opts` as a serialized JSON string.
    pub fn store_options(&self, opts: &[DhcpOption]) -> Result<(), Error> {
        self.store(OPTIONS_KEY, opts)
    }

    /// Stores `params` in `fuchsia.stash`.
    ///
    /// This function stores the `params` as a serialized JSON string.
    pub fn store_parameters(&self, params: &ServerParameters) -> Result<(), Error> {
        self.store(PARAMETERS_KEY, params)
    }

    fn store<T>(&self, key: &str, v: &T) -> Result<(), Error>
    where
        T: serde::Serialize + std::fmt::Debug + ?Sized,
    {
        let v = serde_json::to_string(v)
            .with_context(|| format!("failed to serialize {} to json, {} = {:?}", key, key, v))?;
        let () = self
            .proxy
            .set_value(key, &mut fidl_fuchsia_stash::Value::Stringval(v))
            .with_context(|| format!("failed to store {} in stash", key))?;
        let () = self.proxy.commit().context("failed to commit stash state change")?;
        Ok(())
    }

    /// Loads a `CachedClients` map from data stored in `fuchsia.stash`.
    ///
    /// This function will retrieve all client configuration data from `fuchsia.stash`, deserialize
    /// the JSON string values, and load the resulting structured data into a `CachedClients`
    /// hashmap. Any key-value pair which could not be parsed or deserialized will be removed and
    /// skipped.
    pub async fn load_client_configs(&self) -> Result<CachedClients, Error> {
        let (iter, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_stash::GetIteratorMarker>()
                .context("failed to create iterator")?;
        let () =
            self.proxy.get_prefix(&self.prefix, server).context("failed to get prefix iterator")?;
        let mut cache = std::collections::HashMap::new();
        for kv in iter.get_next().await.context("failed to get next iterator item")? {
            let key = match kv.key.split("-").last() {
                Some(v) => v,
                None => {
                    // Invalid key-value pair: remove the invalid pair and try the next one.
                    log::warn!("failed to parse key string: {}", kv.key);
                    let () = self.rm_key(&kv.key)?;
                    continue;
                }
            };
            let key = match ClientIdentifier::from_str(key) {
                Ok(v) => v,
                Err(e) => {
                    log::warn!("client id from string conversion failed: {}", e);
                    let () = self.rm_key(&kv.key)?;
                    continue;
                }
            };
            let val = match kv.val {
                fidl_fuchsia_stash::Value::Stringval(v) => v,
                v => {
                    log::warn!("invalid value variant stored in stash: {:?}", v);
                    let () = self.rm_key(&kv.key)?;
                    continue;
                }
            };
            let val: CachedConfig = match serde_json::from_str(&val) {
                Ok(v) => v,
                Err(e) => {
                    log::warn!("failed to parse JSON from string: {}", e);
                    let () = self.rm_key(&kv.key)?;
                    continue;
                }
            };
            cache.insert(key, val);
        }
        Ok(cache)
    }

    /// Loads a map of `OptionCode`s to `DhcpOption`s from data stored in `fuchsia.stash`.
    pub async fn load_options(&self) -> Result<HashMap<OptionCode, DhcpOption>, Error> {
        let val = self
            .proxy
            .get_value(&OPTIONS_KEY.to_string())
            .await
            .context("failed to get json for options from stash")?;
        let val = match val {
            Some(v) => v,
            None => return Ok(HashMap::new()),
        };
        match *val {
            fidl_fuchsia_stash::Value::Stringval(v) => {
                Ok(serde_json::from_str::<Vec<DhcpOption>>(&v)
                    .with_context(|| {
                        format!("failed to deserialize options from json, json = {}", v)
                    })?
                    .into_iter()
                    .map(|opt| (opt.code(), opt))
                    .collect())
            }
            v => Err(anyhow::anyhow!("invalid value variant stored in stash: {:?}", v)),
        }
    }

    /// Loads a new instance of `ServerParameters` from data stored in `fuchsia.stash`.
    pub async fn load_parameters(&self) -> Result<ServerParameters, Error> {
        let val = self
            .proxy
            .get_value(&PARAMETERS_KEY.to_string())
            .await?
            .ok_or(anyhow::anyhow!("failed to get parameters from stash"))?;
        match *val {
            fidl_fuchsia_stash::Value::Stringval(v) => {
                Ok(serde_json::from_str(&v).with_context(|| {
                    format!("failed to deserialize server parameters from json, json = {}", v)
                })?)
            }
            v => Err(anyhow::anyhow!("invalid value variant stored in stash: {:?}", v)),
        }
    }

    fn rm_key(&self, key: &str) -> Result<(), Error> {
        let () = self
            .proxy
            .delete_value(key)
            .with_context(|| format!("failed to delete invalid key: {}", key))?;
        let () = self
            .proxy
            .commit()
            .with_context(|| format!("failed to commit deletion of invalid key: {}", key))?;
        Ok(())
    }

    /// Deletes the stash entry associated with `client`, if any.
    ///
    /// This function immediately commits the deletion operation to the Stash, i.e. there is no
    /// batching of delete operations.
    pub fn delete(&self, client_id: &ClientIdentifier) -> Result<(), Error> {
        self.rm_key(&self.client_key(client_id))
    }

    /// Clears all configuration data from `fuchsia.stash`.
    ///
    /// This function will delete all key-value pairs associated with the `Stash` value.
    pub fn clear(&self) -> Result<(), Error> {
        let () = self
            .proxy
            .delete_prefix(&self.prefix)
            .with_context(|| format!("failed to delete prefix: {}", self.prefix))?;
        let () = self.proxy.commit().context("failed to commit stash state change")?;
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
    use std::collections::HashMap;
    use std::convert::TryFrom;

    /// Creates a new stash instance with a randomized identifier to prevent test flakes.
    ///
    /// `prefix` must not contain either '-' or ':' as they are used as field delimiters in stash
    /// keys.
    fn new_stash(test_prefix: &str) -> Result<(Stash, String), Error> {
        use rand::Rng;
        let rand_id: String =
            rand::thread_rng().sample_iter(&rand::distributions::Alphanumeric).take(8).collect();
        let stash =
            Stash::new_with_prefix(&rand_id, test_prefix).context("failed to create stash")?;
        // Clear the Stash of any data leftover from the previous test.
        let () = stash.proxy.delete_prefix(&stash.prefix).context("failed to delete prefix")?;
        let () = stash.proxy.commit().context("failed to commit transaction")?;
        Ok((stash, rand_id))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn stash_new_with_prefix() {
        matches::assert_matches!(Stash::new_with_prefix("stash_new", "valid"), Ok(Stash { .. }));
        matches::assert_matches!(Stash::new_with_prefix("stash_new", "invalid-"), Err(..));
        matches::assert_matches!(Stash::new_with_prefix("stash_new", "invalid:"), Err(..));
        matches::assert_matches!(Stash::new_with_prefix("stash_new", ""), Err(..));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn store_client_succeeds() -> Result<(), Error> {
        let (stash, id) = new_stash("store_client_succeeds")?;
        let accessor_client = stash.proxy.clone();

        // Store value in stash.
        let client_id = ClientIdentifier::from(crate::server::tests::random_mac_generator());
        let client_config = CachedConfig::default();
        let () = stash
            .store_client_config(&client_id, &client_config)
            .with_context(|| format!("failed to store client in {}", id))?;

        // Verify value actually stored in stash.
        let value = accessor_client
            .get_value(&stash.client_key(&client_id))
            .await
            .with_context(|| format!("failed to get value from {}", id))?;
        let value = match *value.unwrap() {
            fidl_fuchsia_stash::Value::Stringval(v) => v,
            v => panic!("stored value is not a string: {:?}", v),
        };
        let value: CachedConfig = serde_json::from_str(&value)?;
        assert_eq!(value, client_config);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn store_options_succeeds() -> Result<(), Error> {
        let (stash, _id) = new_stash("store_options_succeeds")?;
        let accessor_client = stash.proxy.clone();

        let opts = vec![
            DhcpOption::SubnetMask(ip_v4!(255.255.255.0)),
            DhcpOption::DomainNameServer(vec![ip_v4!(1.2.3.4), ip_v4!(4.3.2.1)]),
        ];
        let () = stash.store_options(&opts)?;
        let value = accessor_client.get_value(&OPTIONS_KEY.to_string()).await?;
        let value = match *value.unwrap() {
            fidl_fuchsia_stash::Value::Stringval(v) => v,
            v => panic!("stored value is not a string: {:?}", v),
        };
        let value: Vec<DhcpOption> = serde_json::from_str(&value)?;
        assert_eq!(value, opts);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn store_parameters_succeeds() -> Result<(), Error> {
        let (stash, _id) = new_stash("store_parameters_succeeds")?;
        let accessor_client = stash.proxy.clone();

        let params = ServerParameters {
            server_ips: vec![ip_v4!(192.168.0.1)],
            lease_length: LeaseLength { default_seconds: 42, max_seconds: 100 },
            managed_addrs: ManagedAddresses {
                network_id: ip_v4!(192.168.0.0),
                broadcast: ip_v4!(192.168.0.255),
                mask: crate::configuration::SubnetMask::try_from(24)?,
                pool_range_start: ip_v4!(192.168.0.10),
                pool_range_stop: ip_v4!(192.168.0.254),
            },
            permitted_macs: crate::configuration::PermittedMacs(Vec::new()),
            static_assignments: crate::configuration::StaticAssignments(HashMap::new()),
            arp_probe: false,
            bound_device_names: vec![],
        };
        let () = stash.store_parameters(&params)?;
        let value = accessor_client.get_value(&PARAMETERS_KEY.to_string()).await?;
        let value = match *value.unwrap() {
            fidl_fuchsia_stash::Value::Stringval(v) => v,
            v => panic!("stored value is not a string: {:?}", v),
        };
        let value: ServerParameters = serde_json::from_str(&value)?;
        assert_eq!(value, params);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_clients_with_populated_stash_returns_cached_clients() -> Result<(), Error> {
        let (stash, id) = new_stash("load_clients_with_populated_stash_returns_cached_clients")?;
        let accessor = stash.proxy.clone();

        let client_id = ClientIdentifier::from(crate::server::tests::random_mac_generator());
        let client_config = CachedConfig::default();
        let serialized_client =
            serde_json::to_string(&client_config).expect("serialization failed");
        let () = accessor
            .set_value(
                &stash.client_key(&client_id),
                &mut fidl_fuchsia_stash::Value::Stringval(serialized_client),
            )
            .with_context(|| format!("failed to set value in {}", id))?;
        let () = accessor
            .commit()
            .with_context(|| format!("failed to commit stash state change in {}", id))?;

        let loaded_cache = stash
            .load_client_configs()
            .await
            .with_context(|| format!("failed to load map from stash in {}", id))?;

        let mut cached_clients = HashMap::new();
        cached_clients.insert(client_id, client_config);
        assert_eq!(loaded_cache, cached_clients);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_options_with_stashed_options_returns_options() -> Result<(), Error> {
        let (stash, _id) = new_stash("load_options_with_stashed_options_returns_options")?;
        let accessor = stash.proxy.clone();

        let opts = vec![
            DhcpOption::SubnetMask(ip_v4!(255.255.255.0)),
            DhcpOption::DomainNameServer(vec![ip_v4!(1.2.3.4), ip_v4!(4.3.2.1)]),
        ];
        let serialized_opts = serde_json::to_string(&opts)?;
        let opts = opts.into_iter().map(|o| (o.code(), o)).collect();
        let () = accessor.set_value(
            &OPTIONS_KEY.to_string(),
            &mut fidl_fuchsia_stash::Value::Stringval(serialized_opts),
        )?;
        let () = accessor.commit()?;

        let loaded_opts = stash.load_options().await?;
        assert_eq!(loaded_opts, opts);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_options_with_no_stashed_options_returns_empty_map() -> Result<(), Error> {
        let (stash, _id) = new_stash("load_options_with_no_stashed_options_returns_empty_vec")?;

        let opts = stash.load_options().await?;
        assert_eq!(opts, HashMap::new());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_parameters_with_stashed_parameters_returns_parameters() -> Result<(), Error> {
        let (stash, _id) = new_stash("load_parameters_with_stashed_parameters_returns_parameters")?;
        let accessor = stash.proxy.clone();

        let params = ServerParameters {
            server_ips: vec![ip_v4!(192.168.0.1)],
            lease_length: LeaseLength { default_seconds: 42, max_seconds: 100 },
            managed_addrs: ManagedAddresses {
                network_id: ip_v4!(192.168.0.0),
                broadcast: ip_v4!(192.168.0.255),
                mask: crate::configuration::SubnetMask::try_from(24)?,
                pool_range_start: ip_v4!(192.168.0.10),
                pool_range_stop: ip_v4!(192.168.0.254),
            },
            permitted_macs: crate::configuration::PermittedMacs(Vec::new()),
            static_assignments: crate::configuration::StaticAssignments(HashMap::new()),
            arp_probe: false,
            bound_device_names: vec![],
        };
        let serialized_params = serde_json::to_string(&params)?;
        let () = accessor.set_value(
            &PARAMETERS_KEY.to_string(),
            &mut fidl_fuchsia_stash::Value::Stringval(serialized_params),
        )?;
        let () = accessor.commit()?;

        let loaded_params = stash.load_parameters().await?;
        assert_eq!(loaded_params, params);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_parameters_with_no_stashed_parameters_returns_err() -> Result<(), Error> {
        let (stash, _id) = new_stash("load_parameters_with_no_stashed_parameters_returns_err")?;

        let e = stash.load_parameters().await;
        assert!(e.is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_clients_with_stash_containing_invalid_entries_returns_empty_cache(
    ) -> Result<(), Error> {
        let (stash, id) =
            new_stash("load_clients_with_stash_containing_invalid_entries_returns_empty_cache")?;
        let accessor = stash.proxy.clone();

        let client_id = ClientIdentifier::from(crate::server::tests::random_mac_generator());
        let client_config = CachedConfig::default();
        let serialized_client =
            serde_json::to_string(&client_config).expect("serialization failed");
        let () = accessor
            .set_value("invalid key", &mut fidl_fuchsia_stash::Value::Stringval(serialized_client))
            .with_context(|| format!("failed to set value in {}", id))?;
        let () = accessor
            .set_value(&stash.client_key(&client_id), &mut fidl_fuchsia_stash::Value::Intval(42))
            .with_context(|| format!("failed to set value in {}", id))?;
        let () = accessor
            .commit()
            .with_context(|| format!("failed to commit stash state change in {}", id))?;

        let loaded_cache = stash
            .load_client_configs()
            .await
            .with_context(|| format!("failed to load map from stash in {}", id))?;

        let empty_cache = HashMap::new();
        assert_eq!(loaded_cache, empty_cache);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn delete_client_succeeds() -> Result<(), Error> {
        let (stash, id) = new_stash("delete_client_succeeds")?;
        let accessor = stash.proxy.clone();

        // Store value in stash.
        let client_id = ClientIdentifier::from(crate::server::tests::random_mac_generator());
        let client_config = CachedConfig::default();
        let () = stash
            .store_client_config(&client_id, &client_config)
            .with_context(|| format!("failed to store client in {}", id))?;

        // Verify value actually stored in stash.
        let value = accessor
            .get_value(&stash.client_key(&client_id))
            .await
            .with_context(|| format!("failed to get value from {}", id))?;
        assert!(value.is_some());

        // Delete value and verify its absence.
        let () = stash
            .delete(&client_id)
            .with_context(|| format!("failed to delete client in {}", id))?;
        let value = accessor
            .get_value(&stash.client_key(&client_id))
            .await
            .with_context(|| format!("failed to get value from {}", id))?;
        assert!(value.is_none());

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn clear_with_populated_stash_clears_stash() -> Result<(), Error> {
        let (stash, id) = new_stash("clear_with_populated_stash_clears_stash")?;
        let accessor = stash.proxy.clone();

        // Store a value in the stash.
        let client_mac = ClientIdentifier::from(crate::server::tests::random_mac_generator());
        let client_config = CachedConfig::default();
        let serialized_client =
            serde_json::to_string(&client_config).expect("serialization failed");
        let () = accessor
            .set_value(
                &stash.client_key(&client_mac),
                &mut fidl_fuchsia_stash::Value::Stringval(serialized_client),
            )
            .with_context(|| format!("failed to set value in {}", id))?;
        let () = accessor
            .commit()
            .with_context(|| format!("failed to commit stash state change in {}", id))?;

        // Clear the stash.
        let () = stash.clear().with_context(|| format!("failed to clear stash in {}", id))?;

        // Verify that the stash is actually empty.
        let (iter, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_stash::GetIteratorMarker>()
                .context("failed to create iterator")?;
        let () = accessor
            .get_prefix(&stash.prefix, server)
            .with_context(|| format!("failed to get prefix iterator from {}", id))?;
        let stash_contents = iter.get_next().await.context("failed to get next item")?;
        assert_eq!(stash_contents.len(), 0);

        Ok(())
    }
}
