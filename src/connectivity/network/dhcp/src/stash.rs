// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::{CachedClients, CachedConfig};
use failure::{Error, ResultExt};
use fidl_fuchsia_hardware_ethernet_ext::MacAddress;
use std::str::FromStr;

/// A wrapper around a `fuchsia.stash.StoreAccessor` proxy.
///
/// Stash provides a simple API by which the DHCP `Server` can store and load client configuration
/// data to persistent storage.
///
/// This wrapper stores client configuration as serialized JSON strings. The decision to use JSON
/// derives from its use in other Stash clients, cf. commit e9c57a0, and the relative immaturity of
/// more compact serde serialization formats, e.g. https://github.com/pyfisch/cbor/issues.
pub struct Stash {
    prefix: String,
    proxy: fidl_fuchsia_stash::StoreAccessorProxy,
}

impl Stash {
    /// Instantiates a new `Stash` value.
    ///
    /// The newly instantiated value will use `id` to identify itself with the `fuchsia.stash`
    /// service and `prefix` as the prefix for key strings in persistent storage.
    pub fn new(id: &str, prefix: &str) -> Result<Self, Error> {
        let store_client =
            fuchsia_component::client::connect_to_service::<fidl_fuchsia_stash::StoreMarker>()
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

    /// Stores the `client_config` value with the `client_mac` key in `fuchsia.stash`.
    ///
    /// This function stores the `client_config` as a serialized JSON string.
    pub fn store<'a>(
        &'a self,
        client_mac: &'a fidl_fuchsia_hardware_ethernet_ext::MacAddress,
        client_config: &'a CachedConfig,
    ) -> Result<(), Error> {
        let key = format!("{}-{}", self.prefix, client_mac);
        let val = serde_json::to_string(client_config)
            .context("failed to serialize client configuration")?;
        let () = self
            .proxy
            .set_value(&key, &mut fidl_fuchsia_stash::Value::Stringval(val))
            .context("failed to store client in stash")?;
        let () = self.proxy.commit().context("failed to commit stash state change")?;
        Ok(())
    }

    /// Loads a `CachedClients` map from data stored in `fuchsia.stash`.
    ///
    /// This function will retrieve all client configuration data from `fuchsia.stash`, deserialize
    /// the JSON string values, and load the resulting structured data into a `CachedClients`
    /// hashmap. Any key-value pair which could not be parsed or deserialized will be removed and
    /// skipped.
    pub async fn load(&self) -> Result<CachedClients, Error> {
        let (iter, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_stash::GetIteratorMarker>()
                .context("failed to create iterator")?;
        let () =
            self.proxy.get_prefix(&self.prefix, server).context("failed to get prefix iterator")?;
        let mut cache = std::collections::HashMap::new();
        for kv in iter.get_next().await.context("failed to get next iterator item")? {
            let key = match kv.key.split("-").last() {
                Some(v) => fidl_fuchsia_hardware_ethernet_ext::MacAddress::from_str(v)?,
                None => {
                    // Invalid key-value pair: remove the invalid pair and try the next one.
                    log::warn!("failed to parse key string: {}", kv.key);
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

    fn rm_key(&self, key: &str) -> Result<(), Error> {
        let () = self
            .proxy
            .delete_value(key)
            .with_context(|e| format!("failed to delete invalid key: {}: {}", key, e))?;
        let () = self.proxy.commit().with_context(|e| {
            format!("failed to commit deletion of invalid key: {}: {}", key, e)
        })?;
        Ok(())
    }

    /// Deletes the stash entry associated with `client`, if any.
    ///
    /// This function immediately commits the deletion operation to the Stash, i.e. there is no
    /// batching of delete operations.
    pub fn delete(&self, client: &MacAddress) -> Result<(), Error> {
        let key = format!("{}-{}", self.prefix, client);
        self.rm_key(&key)
    }

    /// Clears all configuration data from `fuchsia.stash`.
    ///
    /// This function will delete all key-value pairs associated with the `Stash` value.
    pub fn clear(&self) -> Result<(), Error> {
        let () = self
            .proxy
            .delete_prefix(&self.prefix)
            .with_context(|e| format!("failed to delete prefix: {}: {}", self.prefix, e))?;
        let () = self.proxy.commit().context("failed to commit stash state change")?;
        Ok(())
    }

    #[cfg(test)]
    pub fn clone_proxy(&self) -> fidl_fuchsia_stash::StoreAccessorProxy {
        self.proxy.clone()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use failure::ResultExt;
    use std::collections::HashMap;

    const TEST_ID: &str = "test_id";
    const TEST_PREFIX: &str = "test_prefix";

    fn new_stash() -> Result<Stash, Error> {
        let stash = Stash::new(TEST_ID, TEST_PREFIX)?;
        // Clear the Stash of data leftover from the previous test.
        let () = stash.proxy.delete_prefix(TEST_PREFIX).context("failed to delete prefix")?;
        let () = stash.proxy.commit().context("failed to commit transaction")?;
        Ok(stash)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn store_client_succeeds() -> Result<(), Error> {
        let stash = new_stash()?;
        let accessor_client = stash.proxy.clone();

        // Store value in stash.
        let client_mac = crate::server::tests::random_mac_generator();
        let client_config = CachedConfig::default();
        let () = stash.store(&client_mac, &client_config).context("failed to store client")?;

        // Verify value actually stored in stash.
        let value = accessor_client
            .get_value(&format!("{}-{}", TEST_PREFIX, client_mac))
            .await
            .context("failed to get value")?;
        let value = match *value.unwrap() {
            fidl_fuchsia_stash::Value::Stringval(v) => v,
            v => panic!("stored value is not a string: {:?}", v),
        };
        let value: CachedConfig = serde_json::from_str(&value)?;
        assert_eq!(value, client_config);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_clients_with_populated_stash_returns_cached_clients() -> Result<(), Error> {
        let stash = new_stash()?;
        let accessor = stash.proxy.clone();

        let client_mac = crate::server::tests::random_mac_generator();
        let client_config = CachedConfig::default();
        let serialized_client =
            serde_json::to_string(&client_config).expect("serialization failed");
        let () = accessor
            .set_value(
                &format!("{}-{}", TEST_PREFIX, client_mac),
                &mut fidl_fuchsia_stash::Value::Stringval(serialized_client),
            )
            .context("failed to set value")?;
        let () = accessor.commit().context("failed to commit stash state change")?;

        let loaded_cache = stash.load().await.context("failed to load map from stash")?;

        let mut cached_clients = HashMap::new();
        cached_clients.insert(client_mac, client_config);
        assert_eq!(loaded_cache, cached_clients);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_clients_with_stash_containing_invalid_entries_returns_empty_cache(
    ) -> Result<(), Error> {
        let stash = new_stash()?;
        let accessor = stash.proxy.clone();

        let client_mac = crate::server::tests::random_mac_generator();
        let client_config = CachedConfig::default();
        let serialized_client =
            serde_json::to_string(&client_config).expect("serialization failed");
        let () = accessor
            .set_value("invalid key", &mut fidl_fuchsia_stash::Value::Stringval(serialized_client))
            .context("failed to set value")?;
        let () = accessor
            .set_value(
                &format!("{}-{}", TEST_PREFIX, client_mac),
                &mut fidl_fuchsia_stash::Value::Intval(42),
            )
            .context("failed to set value")?;
        let () = accessor.commit().context("failed to commit stash state change")?;

        let loaded_cache = stash.load().await.context("failed to load map from stash")?;

        let empty_cache = HashMap::new();
        assert_eq!(loaded_cache, empty_cache);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn delete_client_succeeds() -> Result<(), Error> {
        let stash = new_stash()?;
        let accessor = stash.proxy.clone();

        // Store value in stash.
        let client_mac = crate::server::tests::random_mac_generator();
        let client_config = CachedConfig::default();
        let () = stash.store(&client_mac, &client_config).context("failed to store client")?;

        // Verify value actually stored in stash.
        let value = accessor
            .get_value(&format!("{}-{}", TEST_PREFIX, client_mac))
            .await
            .context("failed to get value")?;
        assert!(value.is_some());

        // Delete value and verify its absence.
        let () = stash.delete(&client_mac).context("failed to delete client")?;
        let value = accessor
            .get_value(&format!("{}-{}", TEST_PREFIX, client_mac))
            .await
            .context("failed to get value")?;
        assert!(value.is_none());

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn clear_with_populated_stash_clears_stash() -> Result<(), Error> {
        let stash = new_stash()?;
        let accessor = stash.proxy.clone();

        // Store a value in the stash.
        let client_mac = crate::server::tests::random_mac_generator();
        let client_config = CachedConfig::default();
        let serialized_client =
            serde_json::to_string(&client_config).expect("serialization failed");
        let () = accessor
            .set_value(
                &format!("{}-{}", TEST_PREFIX, client_mac),
                &mut fidl_fuchsia_stash::Value::Stringval(serialized_client),
            )
            .context("failed to set value")?;
        let () = accessor.commit().context("failed to commit stash state change")?;

        // Clear the stash.
        let () = stash.clear().context("failed to clear stash")?;

        // Verify that the stash is actually empty.
        let (iter, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_stash::GetIteratorMarker>()
                .context("failed to create iterator")?;
        let () =
            accessor.get_prefix(TEST_PREFIX, server).context("failed to get prefix iterator")?;
        let stash_contents = iter.get_next().await.context("failed to get next item")?;
        assert_eq!(stash_contents.len(), 0);

        Ok(())
    }
}
