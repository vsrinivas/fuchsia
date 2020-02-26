// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        known_ess_store::{self, EssJsonRead, KnownEss, KnownEssStore},
        network_config::{
            Credential, NetworkConfig, NetworkConfigError, NetworkIdentifier, SecurityType,
        },
        stash::Stash,
    },
    anyhow::format_err,
    log::{error, info},
    parking_lot::Mutex,
    std::{
        clone::Clone,
        collections::{hash_map::Entry, HashMap},
        fs, io,
        path::Path,
    },
};

/// The Saved Network Manager keeps track of saved networks and provides thread-safe access to
/// saved networks. Networks are saved by NetworkConfig and accessed by their NetworkIdentifier
/// (SSID and security protocol). Network configs are saved in-memory, and part of each network
/// data is saved persistently.
pub struct SavedNetworksManager {
    saved_networks: Mutex<NetworkConfigMap>,
    stash: Mutex<Stash>,
    legacy_store: KnownEssStore,
}

/// Save multiple network configs per SSID in able to store multiple connections with different
/// credentials, for different authentication credentials on the same network or for different
/// networks with the same name.
type NetworkConfigMap = HashMap<NetworkIdentifier, Vec<NetworkConfig>>;

const STASH_ID: &str = "saved_networks";
const MAX_CONFIGS_PER_SSID: usize = 1;

impl SavedNetworksManager {
    /// Initializes a new Saved Network Manager by reading saved networks from a secure storage
    /// (stash) or from a legacy storage file (from KnownEssStore) if stash is empty. In either
    /// case it initializes in-memory storage and persistent storage with stash to remember
    /// networks.
    pub async fn new() -> Result<Self, anyhow::Error> {
        let path = known_ess_store::KNOWN_NETWORKS_PATH;
        let tmp_path = known_ess_store::TMP_KNOWN_NETWORKS_PATH;
        Self::new_with_stash_or_paths(STASH_ID, Path::new(path), Path::new(tmp_path)).await
    }

    /// Load from persistent data from 1 of 2 places: stash or the file created by KnownEssStore.
    /// For now we need to support reading from the the legacy version (KnownEssStore) as well
    /// from stash. And we need to keep the legacy version temporarily so we will decide where to
    /// read based on whether stash has been used.
    /// TODO(44184) Eventually delete logic for handling legacy storage from KnownEssStore and
    /// update comments once all users have migrated.
    pub async fn new_with_stash_or_paths(
        stash_id: impl AsRef<str>,
        legacy_path: impl AsRef<Path>,
        legacy_tmp_path: impl AsRef<Path>,
    ) -> Result<Self, anyhow::Error> {
        let mut stash = Stash::new_with_id(stash_id.as_ref())?;
        let mut saved_networks = stash.load().await?;
        // Don't read legacy if stash is not empty; we have already migrated.
        if stash.load().await?.is_empty() {
            Self::migrate_legacy(legacy_path.as_ref(), &mut stash, &mut saved_networks).await?;
        }
        // KnownEssStore will internally load from the correct path.
        let legacy_store = KnownEssStore::new_with_paths(
            legacy_path.as_ref().to_path_buf(),
            legacy_tmp_path.as_ref().to_path_buf(),
        )?;

        Ok(Self {
            saved_networks: Mutex::new(saved_networks),
            stash: Mutex::new(stash),
            legacy_store,
        })
    }

    /// Read from the old persistent storage of network configs, then write them into the new
    /// storage, both in the hashmap and the stash that stores them persistently.
    async fn migrate_legacy(
        legacy_storage_path: impl AsRef<Path>,
        stash: &mut Stash,
        saved_networks: &mut NetworkConfigMap,
    ) -> Result<(), anyhow::Error> {
        info!("Attempting to migrate saved networks from legacy implementation to stash");
        match Self::load_from_path(&legacy_storage_path) {
            Ok(legacy_saved_networks) => {
                for (net_id, configs) in legacy_saved_networks {
                    stash.write(&net_id, &configs)?;
                    saved_networks.insert(net_id, configs);
                }
            }
            Err(e) => {
                format_err!("Failed to load legacy networks: {}", e);
            }
        }
        Ok(())
    }

    /// Handles reading networks persisted by the previous version of network storage
    /// (KnownEssStore) for the new store.
    fn load_from_path(storage_path: impl AsRef<Path>) -> Result<NetworkConfigMap, anyhow::Error> {
        // Temporarily read from memory the same way EssStore does.
        let config_list: Vec<EssJsonRead> = match fs::File::open(&storage_path) {
            Ok(file) => match serde_json::from_reader(io::BufReader::new(file)) {
                Ok(list) => list,
                Err(e) => {
                    error!(
                        "Failed to parse the list of known wireless networks from JSONin {}: {}. \
                         Starting with an empty list.",
                        storage_path.as_ref().display(),
                        e
                    );
                    fs::remove_file(&storage_path).map_err(|e| {
                        format_err!("Failed to delete {}: {}", storage_path.as_ref().display(), e)
                    })?;
                    Vec::new()
                }
            },
            Err(e) => match e.kind() {
                io::ErrorKind::NotFound => Vec::new(),
                _ => {
                    return Err(format_err!(
                        "Failed to open {}: {}",
                        storage_path.as_ref().display(),
                        e
                    ))
                }
            },
        };
        let mut saved_networks = HashMap::<NetworkIdentifier, Vec<NetworkConfig>>::new();
        for config in config_list {
            // Choose appropriate unknown values based on password and how known
            // ESS have been used for connections.
            let credential = Credential::from_bytes(config.password);
            let network_id =
                NetworkIdentifier::new(config.ssid, credential.derived_security_type());
            if let Ok(network_config) =
                NetworkConfig::new(network_id.clone(), credential, false, false)
            {
                saved_networks.entry(network_id).or_default().push(network_config);
            } else {
                eprintln!(
                    "Error creating network config from loaded data for SSID {}",
                    String::from_utf8_lossy(&network_id.ssid.clone())
                );
            }
        }
        Ok(saved_networks)
    }

    /// Clear the in memory storage and the persistent storage. Also clear the legacy storage.
    pub fn clear(&self) -> Result<(), anyhow::Error> {
        self.saved_networks.lock().clear();
        self.stash.lock().clear()?;
        self.legacy_store.clear()
    }

    /// Get the count of networks in store, including multiple values with same SSID
    pub fn known_network_count(&self) -> usize {
        self.saved_networks.lock().values().into_iter().flatten().count()
    }

    /// Return a list of network configs that match the given SSID.
    pub fn lookup(&self, id: NetworkIdentifier) -> Vec<NetworkConfig> {
        self.saved_networks.lock().entry(id).or_default().iter().map(Clone::clone).collect()
    }

    /// Save a network by SSID and password. If the SSID and password have been saved together
    /// before, do not modify the saved config. Update the legacy storage to keep it consistent
    /// with what it did before the new version.
    pub fn store(
        &self,
        network_id: NetworkIdentifier,
        credential: Credential,
    ) -> Result<(), NetworkConfigError> {
        let mut guard = self.saved_networks.lock();
        let network_entry = guard.entry(network_id.clone());
        if let Entry::Occupied(network_configs) = &network_entry {
            if network_configs.get().iter().any(|cfg| cfg.credential == credential) {
                info!(
                    "wlancfg: Saving a previously saved network with same password: {}",
                    String::from_utf8_lossy(&network_id.ssid)
                );
                return Ok(());
            }
        }
        let network_config =
            NetworkConfig::new(network_id.clone(), credential.clone(), false, false)?;
        let network_configs = network_entry.or_default();
        evict_if_needed(network_configs);
        network_configs.push(network_config);
        self.stash
            .lock()
            .write(&network_id, &network_configs)
            .map_err(|_| NetworkConfigError::StashWriteError)?;
        // Write saved networks to the legacy store only if they are WPA2 or Open, as legacy store
        // does not support more types.
        if network_id.security_type == SecurityType::Wpa2
            || network_id.security_type == SecurityType::None
        {
            let ess = KnownEss { password: credential.into_bytes() };
            self.legacy_store
                .store(network_id.ssid, ess)
                .map_err(|_| NetworkConfigError::LegacyWriteError)?;
        }
        Ok(())
    }

    /// Update a saved network that after connecting to it. If a network with these identifiers
    /// has not been connected to before, this will not save it and return an error.
    pub fn record_connect_success(&self, id: NetworkIdentifier, credential: &Credential) {
        if let Some(networks) = self.saved_networks.lock().get_mut(&id) {
            for network in networks.iter_mut() {
                if &network.credential == credential {
                    if !network.has_ever_connected {
                        network.has_ever_connected = true;
                        // Update persistent storage since a config has changed.
                        self.stash.lock().write(&id, &networks).unwrap_or_else(|_| {
                            info!("Failed recording successful connect in persistent storage");
                        });
                    }
                    return;
                }
            }
        }

        // Will not reach here if we find the saved network with matching SSID and credential.
        info!(
            "Failed finding network ({},{:?}) to record success.",
            String::from_utf8_lossy(&id.ssid),
            id.security_type
        );
    }
}

/// If the list of configs is at capacity for the number of saved configs per SSID,
/// remove a saved network that has never been successfully connected to. If all have
/// been successfully connected to, remove any. If a network config is evicted, that connection
/// is forgotten for future connections.
/// TODO(41232) - when network configs record information about successful connections,
/// use this to make a better decision what to forget if all networks have connected before.
/// TODO(41626) - make sure that we disconnect from the network if we evict a network config
/// for a network we are currently connected to.
fn evict_if_needed(configs: &mut Vec<NetworkConfig>) {
    if configs.len() < MAX_CONFIGS_PER_SSID {
        return;
    }

    for i in 0..configs.len() {
        if let Some(config) = configs.get(i) {
            if !config.has_ever_connected {
                configs.remove(i);
                return;
            }
        }
    }
    // If all saved networks have connected, remove the first network
    configs.remove(0);
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::network_config::{PerformanceStats, SecurityType},
        fuchsia_async as fasync,
        std::{io::Write, mem},
        tempfile::TempDir,
    };

    #[fasync::run_singlethreaded(test)]
    async fn store_and_lookup() {
        let stash_id = "store_and_lookup";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let network_id_foo = NetworkIdentifier::new("foo", SecurityType::Wpa2);

        assert!(saved_networks.lookup(network_id_foo.clone()).is_empty());
        assert_eq!(0, saved_networks.known_network_count());

        // Store a network and verify it was stored.
        saved_networks
            .store(network_id_foo.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .expect("storing 'foo' failed");
        assert_eq!(
            vec![network_config("foo", "qwertyuio")],
            saved_networks.lookup(network_id_foo.clone())
        );
        assert_eq!(1, saved_networks.known_network_count());

        // Store another network with the same SSID.
        saved_networks
            .store(network_id_foo.clone(), Credential::Password(b"12345678".to_vec()))
            .expect("storing 'foo' a second time failed");

        // There should only be one saved "foo" network because MAX_CONFIGS_PER_SSID is 1.
        // When this constant becomes greater than 1, both network configs should be found
        assert_eq!(
            vec![network_config("foo", "12345678")],
            saved_networks.lookup(network_id_foo.clone())
        );
        assert_eq!(1, saved_networks.known_network_count());

        // Store another network and verify.
        let network_id_baz = NetworkIdentifier::new("baz", SecurityType::Wpa2);
        saved_networks
            .store(network_id_baz.clone(), Credential::Psk(vec![1; 64]))
            .expect("storing 'baz' with PSK failed");
        assert_eq!(
            vec![network_config("baz", [1; 64].to_vec())],
            saved_networks.lookup(network_id_baz.clone())
        );
        assert_eq!(2, saved_networks.known_network_count());

        // Saved networks should persist when we create a saved networks manager with the same ID.
        let saved_networks =
            SavedNetworksManager::new_with_stash_or_paths(stash_id, &path, tmp_path)
                .await
                .expect("failed to create saved networks store");
        assert_eq!(
            vec![network_config("foo", "12345678")],
            saved_networks.lookup(network_id_foo.clone())
        );
        assert_eq!(
            vec![network_config("baz", [1; 64].to_vec())],
            saved_networks.lookup(network_id_baz)
        );
        assert_eq!(2, saved_networks.known_network_count());
    }

    #[fasync::run_singlethreaded(test)]
    async fn store_twice() {
        let stash_id = "store_twice";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;
        let network_id = NetworkIdentifier::new(b"foo".to_vec(), SecurityType::Wpa2);

        saved_networks
            .store(network_id.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .expect("storing 'foo' failed");
        saved_networks
            .store(network_id.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .expect("storing 'foo' a second time failed");
        let expected_cfgs = vec![network_config("foo", "qwertyuio")];
        assert_eq!(expected_cfgs, saved_networks.lookup(network_id));
        assert_eq!(1, saved_networks.known_network_count());
    }

    #[fasync::run_singlethreaded(test)]
    async fn store_many_same_ssid() {
        let stash_id = "store_many_same_ssid";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let network_id = NetworkIdentifier::new("foo", SecurityType::Wpa2);
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        // save max + 1 networks with same SSID and different credentials
        for i in 0..MAX_CONFIGS_PER_SSID + 1 {
            let mut password = b"password".to_vec();
            password.push(i as u8);
            saved_networks
                .store(network_id.clone(), Credential::Password(password))
                .expect("Failed to saved network");
        }

        // since none have been connected to yet, we don't care which config was removed
        assert_eq!(MAX_CONFIGS_PER_SSID, saved_networks.lookup(network_id).len());
    }

    #[fasync::run_singlethreaded(test)]
    async fn connect_network() {
        let stash_id = "connect_network";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        let network_id = NetworkIdentifier::new("bar", SecurityType::Wpa2);
        let credential = Credential::Password(b"password".to_vec());

        // If connect and network hasn't been saved, we should give an error and not save network.
        saved_networks.record_connect_success(network_id.clone(), &credential);
        assert!(saved_networks.lookup(network_id.clone()).is_empty());
        assert_eq!(0, saved_networks.known_network_count());

        // Save the network and record a successful connection.
        saved_networks.store(network_id.clone(), credential.clone()).expect("Failed save network");

        let config = network_config("bar", "password");
        assert_eq!(vec![config], saved_networks.lookup(network_id.clone()));

        saved_networks.record_connect_success(network_id.clone(), &credential);

        // The network should be saved with the connection recorded
        let mut config = network_config("bar", "password");
        config.has_ever_connected = true;
        assert_eq!(vec![config], saved_networks.lookup(network_id));
        assert_eq!(1, saved_networks.known_network_count());
    }

    #[test]
    fn evict_if_needed_removes_unconnected() {
        // this test is less meaningful when MAX_CONFIGS_PER_SSID is greater than 1, otherwise
        // the only saved configs should be removed when the max capacity is met, regardless of
        // whether it has been connected to.
        let unconnected_config = network_config("foo", "password");
        let mut connected_config = unconnected_config.clone();
        connected_config.has_ever_connected = false;
        let mut network_configs = vec![connected_config; MAX_CONFIGS_PER_SSID - 1];
        network_configs.insert(MAX_CONFIGS_PER_SSID / 2, unconnected_config);

        evict_if_needed(&mut network_configs);
        assert_eq!(MAX_CONFIGS_PER_SSID - 1, network_configs.len());
        // check that everything left has been connected to before, only one removed is
        // the one that has never been connected to
        for config in network_configs.iter() {
            assert_eq!(true, config.has_ever_connected);
        }
    }

    #[test]
    fn evict_if_needed_already_has_space() {
        let mut configs = vec![];
        evict_if_needed(&mut configs);
        let expected_cfgs: Vec<NetworkConfig> = vec![];
        assert_eq!(expected_cfgs, configs);

        if MAX_CONFIGS_PER_SSID > 1 {
            let mut configs = vec![network_config("foo", "password")];
            evict_if_needed(&mut configs);
            // if MAX_CONFIGS_PER_SSID is 1, this wouldn't be true
            assert_eq!(vec![network_config("foo", "password")], configs);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn clear() {
        let stash_id = "clear";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let network_id = NetworkIdentifier::new(b"foo".to_vec(), SecurityType::Wpa2);
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        saved_networks
            .store(network_id.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .expect("storing 'foo' failed");
        assert!(path.exists());
        assert_eq!(vec![network_config("foo", "qwertyuio")], saved_networks.lookup(network_id));
        assert_eq!(1, saved_networks.known_network_count());

        saved_networks.clear().expect("clearing store failed");
        assert_eq!(0, saved_networks.known_network_count());

        // Load store from stash to verify it is also gone from persistent storage
        let saved_networks =
            SavedNetworksManager::new_with_stash_or_paths(stash_id, &path, &tmp_path)
                .await
                .expect("failed to create saved networks manager");

        assert_eq!(0, saved_networks.known_network_count());
    }

    #[fasync::run_singlethreaded(test)]
    async fn ignore_legacy_file_bad_format() {
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let mut file = fs::File::create(&path).expect("failed to open file for writing");
        // Write invalid JSON and close the file
        file.write(b"{").expect("failed to write broken json into file");
        mem::drop(file);
        assert!(path.exists());
        // Constructing a saved network config store should still succeed,
        // but the invalid file should be gone now
        let stash_id = "ignore_legacy_file_bad_format";
        let saved_networks =
            SavedNetworksManager::new_with_stash_or_paths(stash_id, &path, &tmp_path)
                .await
                .expect("failed to create saved networks store");
        // KnownEssStore deletes the file if it can't read it, as in this case.
        assert!(!path.exists());
        // Writing an entry should not create the file yet because networks configs don't persist.
        assert_eq!(0, saved_networks.known_network_count());
        let network_id = NetworkIdentifier::new(b"foo".to_vec(), SecurityType::Wpa2);
        saved_networks
            .store(network_id.clone(), Credential::Password(b"qwertyuio".to_vec()))
            .expect("storing 'foo' failed");

        // There should be a file here again since we stored a network, so one will be created.
        assert!(path.exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_network_from_legacy_storage() {
        // Possible contents of a file generated from KnownEssStore, with networks foo and bar with
        // passwords foobar and password respecitively. Network foo should not be read into new
        // saved network manager because the password is too short for a valid network password.
        let contents = b"[{\"ssid\":[102,111,111],\"password\":[102,111,111,98,97,114]},
            {\"ssid\":[98,97,114],\"password\":[112, 97, 115, 115, 119, 111, 114, 100]}]";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let mut file = fs::File::create(&path).expect("failed to open file for writing");

        file.write(contents).expect("Failed to write to file");
        file.flush().expect("failed to flush contents of file");

        let stash_id = "read_network_from_legacy_storage";
        let saved_networks =
            SavedNetworksManager::new_with_stash_or_paths(stash_id, &path, &tmp_path)
                .await
                .expect("failed to create saved networks store");

        // We should not delete the file while creating SavedNetworksManager.
        assert!(path.exists());

        // Network bar should have been read into the saved networks manager because it is valid
        assert_eq!(1, saved_networks.known_network_count());
        let bar_id = NetworkIdentifier::new(b"bar".to_vec(), SecurityType::Wpa2);
        let bar_config = NetworkConfig::new(
            bar_id.clone(),
            Credential::Password(b"password".to_vec()),
            false,
            false,
        )
        .expect("failed to create network config");
        assert_eq!(vec![bar_config], saved_networks.lookup(bar_id));

        // Network foo should not have been read into saved networks manager because it is invalid.
        let foo_id = NetworkIdentifier::new(b"foo".to_vec(), SecurityType::Wpa2);
        assert!(saved_networks.lookup(foo_id).is_empty());

        assert!(path.exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn do_not_migrate_networks_twice() {
        let contents = b"[{\"ssid\":[102,111,111],\"password\":[102,111,111,98,97,114]},
            {\"ssid\":[98,97,114],\"password\":[112, 97, 115, 115, 119, 111, 114, 100]}]";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let mut file = fs::File::create(&path).expect("failed to open file for writing");

        file.write(contents).expect("Failed to write to file");
        file.flush().expect("failed to flush contents of file");

        let stash_id = "do_not_migrate_networks_twice";
        let saved_networks =
            SavedNetworksManager::new_with_stash_or_paths(stash_id, &path, &tmp_path)
                .await
                .expect("failed to create saved networks store");

        // We should not have deleted the file while creating SavedNetworksManager.
        assert!(path.exists());

        // Verify the network config loaded from legacy storage
        let net_id = NetworkIdentifier::new(b"bar".to_vec(), SecurityType::Wpa2);
        let net_config = NetworkConfig::new(
            net_id.clone(),
            Credential::Password(b"password".to_vec()),
            false,
            false,
        )
        .expect("failed to create network config");
        assert_eq!(vec![net_config], saved_networks.lookup(net_id.clone()));

        // Replace the network 'bar' that was read from legacy version storage
        saved_networks
            .store(net_id.clone(), Credential::Password(b"foobarbaz".to_vec()))
            .expect("failed to store network");
        let new_net_config = NetworkConfig::new(
            net_id.clone(),
            Credential::Password(b"foobarbaz".to_vec()),
            false,
            false,
        )
        .expect("failed to create network config");
        assert_eq!(vec![new_net_config.clone()], saved_networks.lookup(net_id.clone()));

        // Recreate the SavedNetworksManager again, as would happen when the device restasts
        let saved_networks =
            SavedNetworksManager::new_with_stash_or_paths(stash_id, &path, &tmp_path)
                .await
                .expect("failed to create saved networks store");

        // We should not have deleted the file while creating SavedNetworksManager the first time.
        assert!(path.exists());
        // Expect to see the replaced network 'bar'
        assert_eq!(1, saved_networks.known_network_count());
        assert_eq!(vec![new_net_config], saved_networks.lookup(net_id));
    }

    #[fasync::run_singlethreaded(test)]
    async fn ignore_legacy_if_stash_exists() {
        let contents = b"[{\"ssid\":[102,111,111],\"password\":[102,111,111,98,97,114]},
            {\"ssid\":[98,97,114],\"password\":[112, 97, 115, 115, 119, 111, 114, 100]}]";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let mut file = fs::File::create(&path).expect("failed to open file for writing");

        file.write(contents).expect("Failed to write to file");
        file.flush().expect("failed to flush contents of file");

        let stash_id = "ignore_legacy_if_stash_exists";
        let saved_networks =
            SavedNetworksManager::new_with_stash_or_paths(stash_id, &path, &tmp_path)
                .await
                .expect("failed to create saved networks store");

        // We should not delete the file while creating SavedNetworksManager.
        assert!(path.exists());

        // Verify the network config loaded from legacy storage
        let net_id = NetworkIdentifier::new(b"bar".to_vec(), SecurityType::Wpa2);
        let net_config = NetworkConfig::new(
            net_id.clone(),
            Credential::Password(b"password".to_vec()),
            false,
            false,
        )
        .expect("failed to create network config");
        assert_eq!(vec![net_config], saved_networks.lookup(net_id.clone()));

        // Replace the network 'bar' that was read from legacy version storage
        saved_networks
            .store(net_id.clone(), Credential::Password(b"foobarbaz".to_vec()))
            .expect("failed to store network");
        let new_net_config = NetworkConfig::new(
            net_id.clone(),
            Credential::Password(b"foobarbaz".to_vec()),
            false,
            false,
        )
        .expect("failed to create network config");
        assert_eq!(vec![new_net_config.clone()], saved_networks.lookup(net_id.clone()));

        // Add legacy store file again as if we had failed to delete it
        let mut file = fs::File::create(&path).expect("failed to open file for writing");
        file.write(contents).expect("Failed to write to file");
        file.flush().expect("failed to flush contents of file");

        // Recreate the SavedNetworksManager again, as would happen when the device restasts
        let saved_networks =
            SavedNetworksManager::new_with_stash_or_paths(stash_id, &path, &tmp_path)
                .await
                .expect("failed to create saved networks store");

        // We should ignore the legacy file since there is something in the stash.
        assert_eq!(1, saved_networks.known_network_count());
        assert_eq!(vec![new_net_config], saved_networks.lookup(net_id));
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_and_load_legacy() {
        let stash_id = "write_and_load_legacy";
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = create_saved_networks(stash_id, &path, &tmp_path).await;

        // Save a network, which should write to the legacy store
        let net_id = NetworkIdentifier::new(b"bar".to_vec(), SecurityType::Wpa2);
        saved_networks
            .store(net_id.clone(), Credential::Password(b"foobarbaz".to_vec()))
            .expect("failed to store network");

        // Explicitly clear just the stash
        saved_networks.stash.lock().clear().expect("failed to clear the stash");

        // Create the saved networks manager again to trigger reading from persistent storage
        let saved_networks =
            SavedNetworksManager::new_with_stash_or_paths(stash_id, &path, tmp_path)
                .await
                .expect("failed to create saved networks store");

        // Verify that the network was read in from legacy store.
        let net_config = NetworkConfig::new(
            net_id.clone(),
            Credential::Password(b"foobarbaz".to_vec()),
            false,
            false,
        )
        .expect("failed to create network config");
        assert_eq!(vec![net_config.clone()], saved_networks.lookup(net_id.clone()));
    }

    /// Create a saved networks manager and clear the contents. Stash ID should be different for
    /// each test so that they don't interfere.
    async fn create_saved_networks(
        stash_id: impl AsRef<str>,
        path: impl AsRef<Path>,
        tmp_path: impl AsRef<Path>,
    ) -> SavedNetworksManager {
        let saved_networks =
            SavedNetworksManager::new_with_stash_or_paths(stash_id, &path, &tmp_path)
                .await
                .expect("Failed to create SavedNetworksManager");
        saved_networks.clear().expect("Failed to clear new SavedNetworksManager");
        saved_networks
    }

    // hard code unused fields for tests same as known_ess_store until they are used
    fn network_config(ssid: impl Into<Vec<u8>>, password: impl Into<Vec<u8>>) -> NetworkConfig {
        let credential = Credential::from_bytes(password.into());
        NetworkConfig {
            ssid: ssid.into(),
            security_type: credential.derived_security_type(),
            credential,
            has_ever_connected: false,
            seen_in_passive_scan_results: false,
            perf_stats: PerformanceStats::new(),
        }
    }
}
