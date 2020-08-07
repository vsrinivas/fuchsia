// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{StashNode, NODE_SEPARATOR},
    anyhow::{bail, format_err, Context, Error},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_stash as fidl_stash,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::fx_log_err,
    serde::{Deserialize, Serialize},
    std::collections::HashMap,
};

pub const STASH_PREFIX: &str = "config";
/// The name we store the persistent data of a network config under. The StashNode abstraction
/// requires that writing to a StashNode is done as a named field, so we will store the network
/// config's data under this name.
pub const DATA: &str = "data";

/// The data that will be stored between reboots of a device. Used to convert the data between JSON
/// and network config.
#[derive(Debug, Clone, Deserialize, PartialEq, Serialize)]
pub struct PersistentData {
    pub credential: Credential,
    pub has_ever_connected: bool,
}

/// The network identifier is the SSID and security policy of the network, and it is used to
/// distinguish networks. It mirrors the NetworkIdentifier in fidl_fuchsia_wlan_policy.
#[derive(Clone, Debug, Deserialize, Eq, Hash, PartialEq, Serialize)]
pub struct NetworkIdentifier {
    pub ssid: Vec<u8>,
    pub security_type: SecurityType,
}

/// The security type of a network connection. It mirrors the fidl_fuchsia_wlan_policy SecurityType
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, PartialEq, Serialize)]
pub enum SecurityType {
    None,
    Wep,
    Wpa,
    Wpa2,
    Wpa3,
}
/// The credential of a network connection. It mirrors the fidl_fuchsia_wlan_policy Credential
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum Credential {
    None,
    Password(Vec<u8>),
    Psk(Vec<u8>),
}

/// Manages access to the persistent storage or saved network configs through Stash
pub struct PolicyStash {
    root: StashNode,
}

impl PolicyStash {
    /// Initialize new Stash with the ID provided by the Saved Networks Manager. The ID will
    /// identify stored values as being part of the same persistent storage.
    pub fn new_with_id(id: &str) -> Result<Self, Error> {
        let store_client = connect_to_service::<fidl_stash::SecureStoreMarker>()
            .context("failed to connect to store")?;
        store_client.identify(id).context("failed to identify client to store")?;
        let (store, accessor_server) = create_proxy().context("failed to create accessor proxy")?;
        store_client
            .create_accessor(false, accessor_server)
            .context("failed to create accessor")?;
        let root = StashNode::root(store).child(STASH_PREFIX);
        Ok(Self { root })
    }

    /// Initialize new Stash with a provided proxy in order to mock stash in unit tests.
    pub fn new_with_stash(proxy: fidl_stash::StoreAccessorProxy) -> Self {
        Self { root: StashNode::root(proxy).child(STASH_PREFIX) }
    }

    /// Update the network configs of a given network identifier to persistent storage, deleting
    /// the key entirely if the new list of configs is empty.
    pub async fn write(
        &self,
        id: &NetworkIdentifier,
        network_configs: &[PersistentData],
    ) -> Result<(), Error> {
        // write each config to a StashNode under the network identifier. The key of the StashNode
        // will be STASH_PREFIX#<net_id>#<index>
        let id_key = Self::serialize_key(id)
            .map_err(|_| format_err!("failed to serialize network identifier"))?;
        let mut id_node = self.root.child(&id_key);

        // If configs to update is empty, we essentially delete the network ID's configs.
        if network_configs.is_empty() {
            id_node.delete()?;
        } else {
            // use a different number to separate each child network config
            let mut config_index = 0;
            for network_config in network_configs {
                let mut config_node = id_node.child(&config_index.to_string());
                write_config(&mut config_node, network_config)?;
                config_index += 1;
            }
        }
        id_node.flush().await.map_err(|e| format_err!("failed to flush changes: {}", e))
    }

    /// Make string value of NetworkIdentifier that will be the key for a config in the stash.
    fn serialize_key(id: &NetworkIdentifier) -> Result<String, serde_json::error::Error> {
        serde_json::to_string(id)
    }

    /// Create the NetworkIdentifier described by the StashNode's key. The key must be in the
    /// format of the root's key followed by a JSON representation of a NetworkIdentifier and then
    /// a node separator. Everything after in the key will be ignored.
    fn id_from_key(&self, stash_node: &StashNode) -> Result<NetworkIdentifier, Error> {
        let key = stash_node.key();
        // Verify that the key begins with the root node's key and remove it.
        if !key.starts_with(&self.root.key()) {
            bail!("key is missing the beginning node separator");
        }
        let prefix_len = self.root.key().len();
        let mut key_after_root = key[prefix_len..].to_string();
        if let Some(index) = key_after_root.find(NODE_SEPARATOR) {
            key_after_root.truncate(index);
        } else {
            bail!("key is missing node separator after network identifier");
        }
        // key_after_root should now just be the serialization of the NetworkIdentifier
        serde_json::from_str(&key_after_root).map_err(|e| format_err!("{}", e))
    }

    /// Read persisting data of a given StashNode and use it to build a network config.
    async fn read_config(stash_node: &StashNode) -> Result<PersistentData, Error> {
        let fields = stash_node.fields().await?;
        let data = fields.get_str(DATA).ok_or_else(|| format_err!("failed to config's data"))?;
        let data: PersistentData = serde_json::from_str(data).map_err(|e| format_err!("{}", e))?;
        Ok(data)
    }

    /// Load all saved network configs from stash. Will create HashMap of network configs by SSID
    /// as saved in the stash. If something in stash can't be interpreted, we ignore it.
    pub async fn load(&self) -> Result<HashMap<NetworkIdentifier, Vec<PersistentData>>, Error> {
        // get all the children nodes of root, which represent the unique identifiers,
        let id_nodes = self.root.children().await?;

        let mut network_configs = HashMap::new();
        // for each child representing a network config, read in values
        for id_node in id_nodes {
            let mut config_list = vec![];
            match self.id_from_key(&id_node) {
                Ok(net_id) => {
                    for config_node in id_node.children().await? {
                        match Self::read_config(&config_node).await {
                            // If there is an error reading a saved network from stash, make a note
                            // but don't prevent wlancfg starting up.
                            Ok(network_config) => {
                                config_list.push(network_config);
                            }
                            Err(e) => {
                                fx_log_err!("Error loading from stash: {:?}", e);
                            }
                        }
                    }
                    // If we encountered an error reading the configs, don't add.
                    if config_list.is_empty() {
                        continue;
                    }
                    network_configs.insert(net_id, config_list);
                }
                Err(e) => {
                    fx_log_err!("Error reading network identifier from stash: {:?}", e);
                    continue;
                }
            }
        }

        Ok(network_configs)
    }

    /// Remove all saved values from the stash. It will delete everything under the root node,
    /// and anything else in the same stash but not under the root node would be ignored.
    pub async fn clear(&mut self) -> Result<(), Error> {
        self.root.delete()?;
        self.root.flush().await?;
        Ok(())
    }
}

/// Write the persisting values (not including network ID) of a network config to the provided
/// stash node.
fn write_config(stash_node: &mut StashNode, persistent_data: &PersistentData) -> Result<(), Error> {
    let data_str = serde_json::to_string(&persistent_data).map_err(|e| format_err!("{}", e))?;
    stash_node.write_str(DATA, data_str)
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    /// The PSK provided must be the bytes form of the 64 hexadecimal character hash. This is a
    /// duplicate of a definition in wlan/wlancfg/src, since I don't think there's a good way to
    /// import just that constant.
    pub const PSK_BYTE_LEN: usize = 32;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_and_read() {
        let stash = new_stash("write_and_read").await;
        let cfg_id = NetworkIdentifier { ssid: b"foo".to_vec(), security_type: SecurityType::Wpa2 };
        let cfg = PersistentData {
            credential: Credential::Password(b"password".to_vec()),
            has_ever_connected: true,
        };

        // Save a network config to the stash
        stash.write(&cfg_id, &vec![cfg.clone()]).await.expect("Failed writing to stash");

        // Expect to read the same value back with the same key
        let cfgs_from_stash = stash.load().await.expect("Failed reading from stash");
        assert_eq!(1, cfgs_from_stash.len());
        assert_eq!(Some(&vec![cfg.clone()]), cfgs_from_stash.get(&cfg_id));

        // Overwrite the list of configs saved in stash
        let cfg_2 = PersistentData {
            credential: Credential::Password(b"other-password".to_vec()),
            has_ever_connected: false,
        };
        stash
            .write(&cfg_id, &vec![cfg.clone(), cfg_2.clone()])
            .await
            .expect("Failed writing to stash");

        // Expect to read the saved value back with the same key
        let cfgs_from_stash = stash.load().await.expect("Failed reading from stash");
        assert_eq!(1, cfgs_from_stash.len());
        let actual_configs = cfgs_from_stash.get(&cfg_id).unwrap();
        assert_eq!(2, actual_configs.len());
        assert!(actual_configs.contains(&cfg));
        assert!(actual_configs.contains(&cfg_2));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_read_security_types() {
        let stash = new_stash("write_read_security_types").await;
        let password = Credential::Password(b"config-password".to_vec());

        // create and write configs with each security type
        let net_id_open = network_id("foo", SecurityType::None);
        let net_id_wep = network_id("foo", SecurityType::Wep);
        let net_id_wpa = network_id("foo", SecurityType::Wpa);
        let net_id_wpa2 = network_id("foo", SecurityType::Wpa2);
        let net_id_wpa3 = network_id("foo", SecurityType::Wpa3);
        let cfg_open = PersistentData { credential: Credential::None, has_ever_connected: false };
        let cfg_wep = PersistentData { credential: password.clone(), has_ever_connected: false };
        let cfg_wpa = PersistentData { credential: password.clone(), has_ever_connected: false };
        let cfg_wpa2 = PersistentData { credential: password.clone(), has_ever_connected: false };
        let cfg_wpa3 = PersistentData { credential: password.clone(), has_ever_connected: false };

        stash.write(&net_id_open, &vec![cfg_open.clone()]).await.expect("failed to write config");
        stash.write(&net_id_wep, &vec![cfg_wep.clone()]).await.expect("failed to write config");
        stash.write(&net_id_wpa, &vec![cfg_wpa.clone()]).await.expect("failed to write config");
        stash.write(&net_id_wpa2, &vec![cfg_wpa2.clone()]).await.expect("failed to write config");
        stash.write(&net_id_wpa3, &vec![cfg_wpa3.clone()]).await.expect("failed to write config");

        // load stash and expect each config that we wrote
        let configs = stash.load().await.expect("failed loading from stash");
        assert_eq!(Some(&vec![cfg_open]), configs.get(&net_id_open));
        assert_eq!(Some(&vec![cfg_wep]), configs.get(&net_id_wep));
        assert_eq!(Some(&vec![cfg_wpa]), configs.get(&net_id_wpa));
        assert_eq!(Some(&vec![cfg_wpa2]), configs.get(&net_id_wpa2));
        assert_eq!(Some(&vec![cfg_wpa3]), configs.get(&net_id_wpa3));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_read_credentials() {
        let stash = new_stash("write_read_credentials").await;

        let net_id_none = network_id("bar-none", SecurityType::None);
        let net_id_password = network_id("bar-password", SecurityType::Wpa2);
        let net_id_psk = network_id("bar-psk", SecurityType::Wpa2);

        // create and write configs with each type credential
        let password = Credential::Password(b"config-password".to_vec());
        let psk = Credential::Psk([65; PSK_BYTE_LEN].to_vec());

        let cfg_none = PersistentData { credential: Credential::None, has_ever_connected: false };
        let cfg_password = PersistentData { credential: password, has_ever_connected: false };
        let cfg_psk = PersistentData { credential: psk, has_ever_connected: false };

        // write each config to stash, then check that we see them when we load
        stash.write(&net_id_none, &vec![cfg_none.clone()]).await.expect("failed to write");
        stash.write(&net_id_password, &vec![cfg_password.clone()]).await.expect("failed to write");
        stash.write(&net_id_psk, &vec![cfg_psk.clone()]).await.expect("failed to write");

        let configs = stash.load().await.expect("failed loading from stash");
        assert_eq!(Some(&vec![cfg_none]), configs.get(&net_id_none));
        assert_eq!(Some(&vec![cfg_password]), configs.get(&net_id_password));
        assert_eq!(Some(&vec![cfg_psk]), configs.get(&net_id_psk));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_persists() {
        let stash_id = "write_persists";
        let stash = new_stash(stash_id).await;
        let cfg_id = NetworkIdentifier { ssid: b"foo".to_vec(), security_type: SecurityType::Wpa2 };
        let cfg = PersistentData {
            credential: Credential::Password(b"password".to_vec()),
            has_ever_connected: true,
        };

        // Save a network config to the stash
        stash.write(&cfg_id, &vec![cfg.clone()]).await.expect("Failed writing to stash");

        //create the stash again with same id
        let stash = PolicyStash::new_with_id(stash_id).expect("Failed to create new stash");

        // Expect to read the same value back with the same key, should exist in new stash
        let cfgs_from_stash = stash.load().await.expect("Failed reading from stash");
        assert_eq!(1, cfgs_from_stash.len());
        assert_eq!(Some(&vec![cfg.clone()]), cfgs_from_stash.get(&cfg_id));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn load_stash() {
        let store = new_stash("load_stash").await;
        let foo_net_id =
            NetworkIdentifier { ssid: b"foo".to_vec(), security_type: SecurityType::Wpa2 };
        let cfg_foo = PersistentData {
            credential: Credential::Password(b"12345678".to_vec()),
            has_ever_connected: true,
        };
        let bar_net_id =
            NetworkIdentifier { ssid: b"bar".to_vec(), security_type: SecurityType::Wpa2 };
        let cfg_bar = PersistentData {
            credential: Credential::Password(b"qwertyuiop".to_vec()),
            has_ever_connected: true,
        };

        // Store two networks in our stash.
        store
            .write(&foo_net_id, &vec![cfg_foo.clone()])
            .await
            .expect("Failed to save config to stash");
        store
            .write(&bar_net_id, &vec![cfg_bar.clone()])
            .await
            .expect("Failed to save config to stash");

        // load should give us a hashmap with the two networks we saved
        let mut expected_cfgs = HashMap::new();
        expected_cfgs.insert(foo_net_id.clone(), vec![cfg_foo]);
        expected_cfgs.insert(bar_net_id.clone(), vec![cfg_bar]);
        assert_eq!(expected_cfgs, store.load().await.expect("Failed to load configs from stash"));
    }

    #[fasync::run_singlethreaded(test)]
    async fn load_stash_ignore_bad_values() {
        let stash = new_stash("load_stash_ignore_bad_values").await;

        // write bad value directly to StashNode
        let some_net_id = network_id("foo", SecurityType::Wpa2);
        let net_id_str = PolicyStash::serialize_key(&some_net_id)
            .expect("failed to serialize network identifier");
        let mut config_node = stash.root.child(&net_id_str).child(&format!("{}", 0));
        let bad_value = "some bad value".to_string();
        config_node.write_str(DATA, bad_value).expect("failed to write to stashnode");

        // check that load doesn't fail because of bad string
        let loaded_configs = stash.load().await.expect("failed to load stash");
        assert!(loaded_configs.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn clear_stash() {
        let stash_id = "clear_stash";
        let mut stash = new_stash(stash_id).await;

        // add some configs to the stash
        let net_id_foo =
            NetworkIdentifier { ssid: b"foo".to_vec(), security_type: SecurityType::Wpa2 };
        let cfg_foo = PersistentData {
            credential: Credential::Password(b"qwertyuio".to_vec()),
            has_ever_connected: true,
        };
        let net_id_bar =
            NetworkIdentifier { ssid: b"bar".to_vec(), security_type: SecurityType::Wpa2 };
        let cfg_bar = PersistentData {
            credential: Credential::Password(b"12345678".to_vec()),
            has_ever_connected: false,
        };
        stash.write(&net_id_foo, &vec![cfg_foo.clone()]).await.expect("Failed to write to stash");
        stash.write(&net_id_bar, &vec![cfg_bar.clone()]).await.expect("Failed to write to stash");

        // verify that the configs are found in stash
        let configs_from_stash = stash.load().await.expect("Failed to read");
        assert_eq!(2, configs_from_stash.len());
        assert_eq!(Some(&vec![cfg_foo.clone()]), configs_from_stash.get(&net_id_foo));
        assert_eq!(Some(&vec![cfg_bar.clone()]), configs_from_stash.get(&net_id_bar));

        // clear the stash
        stash.clear().await.expect("Failed to clear stash");
        // verify that the configs are no longer in the stash
        let configs_from_stash = stash.load().await.expect("Failed to read");
        assert_eq!(0, configs_from_stash.len());

        // recreate stash and verify that clearing the stash persists
        let stash = PolicyStash::new_with_id(stash_id).expect("Failed to create new stash");
        let configs_from_stash = stash.load().await.expect("Failed to read");
        assert_eq!(0, configs_from_stash.len());
    }

    // creates a new stash with the given ID and clears the values saved in the stash
    async fn new_stash(stash_id: &str) -> PolicyStash {
        let mut stash = PolicyStash::new_with_id(stash_id).expect("Failed to create new stash");
        stash.root.delete().expect("failed to clear stash");
        stash.root.flush().await.expect("Failed to commit clearing stash");
        stash
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_to_correct_stash_node() {
        let stash = new_stash("write_to_correct_stash_node").await;

        let net_id = network_id("foo", SecurityType::Wpa2);
        let credential = Credential::Password(b"password".to_vec());
        let network_config =
            PersistentData { credential: credential.clone(), has_ever_connected: false };

        // write to stash and check that the right thing is written under the right StashNode
        stash.write(&net_id, &vec![network_config]).await.expect("failed to write to stash");
        let net_id_str =
            PolicyStash::serialize_key(&net_id).expect("failed to serialize network identifier");
        let expected_node = stash.root.child(&net_id_str).child(&format!("{}", 0));
        let fields = expected_node.fields().await.expect("failed to get fields");
        let data_actual = fields.get_str(&format!("{}", DATA));
        let data_expected =
            serde_json::to_string(&PersistentData { credential, has_ever_connected: false })
                .expect("failed to serialize data");
        assert_eq!(data_actual, Some(&data_expected));
    }

    fn network_id(ssid: impl Into<Vec<u8>>, security_type: SecurityType) -> NetworkIdentifier {
        NetworkIdentifier { ssid: ssid.into(), security_type }
    }
}
