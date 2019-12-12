// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        known_ess_store::EssJsonRead,
        network_config::{NetworkConfig, NetworkIdentifier},
    },
    failure::{self, bail, format_err},
    fidl_fuchsia_wlan_policy as fidl_policy,
    log::{error, info},
    parking_lot::Mutex,
    serde_json,
    std::{
        clone::Clone,
        collections::{hash_map::Entry, HashMap},
        fs, io,
        path::PathBuf,
    },
};

/// The Saved Network Manager holds in-memory saved networks and provides thread-safe access to
/// saved networks.
pub struct SavedNetworksManager {
    saved_networks: Mutex<NetworkConfigMap>,
}

/// Save multiple network configs per SSID in able to store multiple connections with different
/// credentials, for different authentication credentials on the same network or for different
/// networks with the same name.
type NetworkConfigMap = HashMap<NetworkIdentifier, Vec<NetworkConfig>>;

const KNOWN_NETWORKS_PATH: &str = "/data/known_networks.json";
const MAX_CONFIGS_PER_SSID: usize = 1;

impl SavedNetworksManager {
    /// initializes a new Saved Network Manager by reading saved networks from a set file location
    pub fn new() -> Result<Self, failure::Error> {
        Self::new_with_paths(PathBuf::from(KNOWN_NETWORKS_PATH))
    }

    /// Before we create new persistent storage, we will load from the place ess_store writes.
    /// Saved network manager reads from these paths but does not write to them.
    /// During migration we will continue to read from both new and old storage locations, then
    /// remove load_ess_store once migration is done.
    pub fn new_with_paths(legacy_storage_path: PathBuf) -> Result<Self, failure::Error> {
        let legacy_saved_networks = Self::load_ess_store(legacy_storage_path)?;
        Ok(Self { saved_networks: Mutex::new(legacy_saved_networks) })
    }

    /// Handles reading persisted networks saved by KnownEssStor into our new store.
    /// Remove this when migration to new persistent storage location is over.
    fn load_ess_store(storage_path: PathBuf) -> Result<NetworkConfigMap, failure::Error> {
        // Temporarily read from memory the same way EssStore does.
        let config_list: Vec<EssJsonRead> = match fs::File::open(&storage_path) {
            Ok(file) => match serde_json::from_reader(io::BufReader::new(file)) {
                Ok(list) => list,
                Err(e) => {
                    error!(
                        "Failed to parse the list of known wireless networks from JSONin {}: {}. \
                         Starting with an empty list.",
                        storage_path.display(),
                        e
                    );
                    fs::remove_file(&storage_path).map_err(|e| {
                        format_err!("Failed to delete {}: {}", storage_path.display(), e)
                    })?;
                    Vec::new()
                }
            },
            Err(e) => match e.kind() {
                io::ErrorKind::NotFound => Vec::new(),
                _ => bail!("Failed to open {}: {}", storage_path.display(), e),
            },
        };

        let mut saved_networks = HashMap::<NetworkIdentifier, Vec<NetworkConfig>>::new();
        for config in config_list {
            // Choose appropriate unknown values based on password and how known
            // ESS have been used for connections.
            let credential = credential_from_bytes(config.password);
            if let Ok(network_config) = NetworkConfig::new(
                (config.ssid.clone(), derive_security_type(&credential)),
                // TODO(40966) make a better choice for security type and credential
                credential,
                false,
                false,
            ) {
                saved_networks
                    .entry((config.ssid.clone(), network_config.security_type))
                    .or_default()
                    .push(network_config);
            } else {
                eprintln!(
                    "Error creating network config from loaded data for SSID {}",
                    String::from_utf8_lossy(&config.ssid)
                );
            }
        }
        Ok(saved_networks)
    }

    /// For now, simply clears in memory storage of networks. Later must clear persistent storage
    pub fn clear(&self) -> Result<(), failure::Error> {
        self.saved_networks.lock().clear();
        Ok(())
    }

    /// Get the count of networks in store, including multiple values with same SSID
    pub fn known_network_count(&self) -> usize {
        self.saved_networks.lock().values().into_iter().flatten().count()
    }

    // Return a list of network configs that match the given SSID.
    pub fn lookup(&self, id: NetworkIdentifier) -> Vec<NetworkConfig> {
        self.saved_networks.lock().entry(id).or_default().iter().map(Clone::clone).collect()
    }

    /// Save a network by SSID and password. If the SSID and password have been saved
    /// together before, do not modify the saved config.
    pub fn store(
        &self,
        ssid: Vec<u8>,
        credential: fidl_policy::Credential,
    ) -> Result<(), failure::Error> {
        let mut guard = self.saved_networks.lock();
        let network_entry = guard.entry((ssid.clone(), derive_security_type(&credential)));
        if let Entry::Occupied(network_configs) = &network_entry {
            // TODO(40966): when we know security type of the networks we store, check for
            // same security type
            if network_configs.get().iter().any(|cfg| cfg.credential == credential) {
                info!(
                    "wlancfg: Saving a previously saved network with same password: {}",
                    String::from_utf8_lossy(&ssid)
                );
                return Ok(());
            }
        }
        // TODO(40966) - add meaningful use of security type
        let network_config =
            NetworkConfig::new((ssid, derive_security_type(&credential)), credential, false, false)
                .map_err(|_| format_err!("Error creating the network config to store"))?;

        let network_configs = network_entry.or_default();
        evict_if_needed(network_configs);
        network_configs.push(network_config);
        Ok(())
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

/// Returns:
/// - an Open-Credential instance iff `bytes` is empty,
/// - a PSK-Credential instance iff `bytes` holds exactly 64 bytes,
/// - a Password-Credential in all other cases.
/// In the PSK case, the provided bytes must represent the PSK in hex format.
/// Note: This function is of temporary nature until connection results communicate
/// type of credential
pub fn credential_from_bytes(bytes: Vec<u8>) -> fidl_policy::Credential {
    const PSK_HEX_STRING_LENGTH: usize = 64;
    match bytes.len() {
        0 => fidl_policy::Credential::None(fidl_policy::Empty),
        PSK_HEX_STRING_LENGTH => fidl_policy::Credential::Psk(bytes),
        _ => fidl_policy::Credential::Password(bytes),
    }
}

/// Choose a security type that fits the credential while we don't actually know the security type
/// of the saved networks.
pub fn derive_security_type(credential: &fidl_policy::Credential) -> fidl_policy::SecurityType {
    match credential {
        fidl_policy::Credential::None(_) => fidl_policy::SecurityType::None,
        _ => fidl_policy::SecurityType::Wpa2,
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::network_config::{NetworkConfig, PerformanceStats},
        std::{io::Write, mem, path::Path},
        tempfile,
    };

    const STORE_JSON_PATH: &str = "store.json";

    #[test]
    fn store_and_lookup() {
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let saved_networks = create_saved_networks(temp_dir.path());

        assert!(saved_networks
            .lookup((b"foo".to_vec(), fidl_policy::SecurityType::Wpa2))
            .is_empty());
        assert_eq!(0, saved_networks.known_network_count());
        saved_networks
            .store(b"foo".to_vec(), credential_from_bytes(b"qwertyuio".to_vec()))
            .expect("storing 'foo' failed");
        assert_eq!(
            vec![network_config(b"foo", b"qwertyuio")],
            saved_networks.lookup((b"foo".to_vec(), fidl_policy::SecurityType::Wpa2))
        );
        assert_eq!(1, saved_networks.known_network_count());

        saved_networks
            .store(b"foo".to_vec(), credential_from_bytes(b"12345678".to_vec()))
            .expect("storing 'foo' a second time failed");
        // There should only be one saved "foo" network because MAX_CONFIGS_PER_SSID is 1.
        // When this constant becomes greater than 1, both network configs should be found
        assert_eq!(
            vec![network_config(b"foo", b"12345678")],
            saved_networks.lookup((b"foo".to_vec(), fidl_policy::SecurityType::Wpa2))
        );
        assert_eq!(1, saved_networks.known_network_count());

        saved_networks
            .store(b"baz".to_vec(), credential_from_bytes(vec![1; 64]))
            .expect("storing 'baz' with PSK failed");
        assert_eq!(
            vec![network_config(b"baz", &[1; 64])],
            saved_networks.lookup((b"baz".to_vec(), fidl_policy::SecurityType::Wpa2))
        );
        assert_eq!(2, saved_networks.known_network_count());

        // Currently store is not yet persistent. Soon it should become persistent.
        let saved_networks = create_saved_networks(temp_dir.path());
        assert!(saved_networks
            .lookup((b"foo".to_vec(), fidl_policy::SecurityType::Wpa2))
            .is_empty());
        assert!(saved_networks
            .lookup((b"baz".to_vec(), fidl_policy::SecurityType::Wpa2))
            .is_empty());
        assert_eq!(0, saved_networks.known_network_count());
    }

    #[test]
    fn store_twice() {
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let saved_networks = create_saved_networks(temp_dir.path());

        saved_networks
            .store(b"foo".to_vec(), credential_from_bytes(b"qwertyuio".to_vec()))
            .expect("storing 'foo' failed");
        saved_networks
            .store(b"foo".to_vec(), credential_from_bytes(b"qwertyuio".to_vec()))
            .expect("storing 'foo' a second time failed");
        let expected_cfgs = vec![network_config(b"foo", b"qwertyuio")];
        assert_eq!(
            expected_cfgs,
            saved_networks.lookup((b"foo".to_vec(), fidl_policy::SecurityType::Wpa2))
        );
        assert_eq!(1, saved_networks.known_network_count());
    }

    #[test]
    fn store_many_same_ssid() {
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let saved_networks = create_saved_networks(temp_dir.path());

        // save max + 1 networks with same SSID and different credentials
        for i in 0..MAX_CONFIGS_PER_SSID + 1 {
            let mut password = b"password".to_vec();
            password.push(i as u8);
            saved_networks
                .store(b"foo".to_vec(), credential_from_bytes(password))
                .expect("Failed to saved network");
        }

        // since none have been connected to yet, we don't care which config was removed
        assert_eq!(
            MAX_CONFIGS_PER_SSID,
            saved_networks.lookup((b"foo".to_vec(), fidl_policy::SecurityType::Wpa2)).len()
        );
    }

    #[test]
    fn evict_if_needed_removes_unconnected() {
        // this test is less meaningful when MAX_CONFIGS_PER_SSID is greater than 1, otherwise
        // the only saved configs should be removed when the max capacity is met, regardless of
        // whether it has been connected to.
        let unconnected_config = network_config(b"foo", b"password");
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
            let mut configs = vec![network_config(b"foo", b"password")];
            evict_if_needed(&mut configs);
            // if MAX_CONFIGS_PER_SSID is 1, this wouldn't be true
            assert_eq!(vec![network_config(b"foo", b"password")], configs);
        }
    }

    #[test]
    fn unwrap_or_else_from_bad_file() {
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let path = temp_dir.path().join(STORE_JSON_PATH);
        let mut file = fs::File::create(&path).expect("failed to open file for writing");
        // Write invalid JSON and close the file
        file.write(b"{").expect("failed to write broken json into file");
        mem::drop(file);
        assert!(path.exists());

        // Constructing a saved network config store should still succeed,
        // but the invalid file should be gone now
        let saved_networks = create_saved_networks(temp_dir.path());
        assert!(!path.exists());

        // Writing an entry should not create the file yet because networks configs don't persist.
        saved_networks
            .store(b"foo".to_vec(), credential_from_bytes(b"qwertyuio".to_vec()))
            .expect("storing 'foo' failed");
        assert!(!path.exists());
    }

    #[test]
    fn bail_if_path_is_bad() {
        let saved_networks = SavedNetworksManager::new_with_paths(PathBuf::from("/dev/null/foo"))
            .expect("Failed to create a SavedNetworksManager");

        // expect error once network configs are saved persistently, for now expect none
        saved_networks
            .store(b"foo".to_vec(), credential_from_bytes(b"qwertyuio".to_vec()))
            .expect("expected store to fail");
    }

    #[test]
    fn clear() {
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");

        // Expect the store to be constructed successfully even if the file doesn't
        // exist yet
        let saved_networks = create_saved_networks(temp_dir.path());

        saved_networks
            .store(b"foo".to_vec(), credential_from_bytes(b"qwertyuio".to_vec()))
            .expect("storing 'foo' failed");
        assert_eq!(
            vec![network_config(b"foo", b"qwertyuio")],
            saved_networks.lookup((b"foo".to_vec(), fidl_policy::SecurityType::Wpa2))
        );
        assert_eq!(1, saved_networks.known_network_count());
        saved_networks.clear().expect("clearing store failed");
        assert_eq!(0, saved_networks.known_network_count());

        // Load store from the file to verify it is also gone from persistent storage
        let saved_networks = create_saved_networks(temp_dir.path());
        assert_eq!(0, saved_networks.known_network_count());
    }

    fn create_saved_networks(path: &Path) -> SavedNetworksManager {
        SavedNetworksManager::new_with_paths(path.join(STORE_JSON_PATH))
            .expect("Failed to create a SavedNetworksManager")
    }

    #[test]
    fn test_credential_from_bytes() {
        assert_eq!(credential_from_bytes(vec![1]), fidl_policy::Credential::Password(vec![1]));
        assert_eq!(
            credential_from_bytes(vec![2; 63]),
            fidl_policy::Credential::Password(vec![2; 63])
        );
        assert_eq!(credential_from_bytes(vec![2; 64]), fidl_policy::Credential::Psk(vec![2; 64]));
        assert_eq!(
            credential_from_bytes(vec![]),
            fidl_policy::Credential::None(fidl_policy::Empty)
        );
    }

    #[test]
    fn test_derive_security_type_from_credential() {
        let password = fidl_policy::Credential::Password(b"password".to_vec());
        let psk = fidl_policy::Credential::Psk(b"psk-type".to_vec());
        let none = fidl_policy::Credential::None(fidl_policy::Empty);

        assert_eq!(fidl_policy::SecurityType::Wpa2, derive_security_type(&password));
        assert_eq!(fidl_policy::SecurityType::Wpa2, derive_security_type(&psk));
        assert_eq!(fidl_policy::SecurityType::None, derive_security_type(&none));
    }

    // hard code unused fields for tests same as known_ess_store until they are used
    fn network_config(ssid: &[u8], password: &[u8]) -> NetworkConfig {
        let credential = credential_from_bytes(password.to_vec());
        NetworkConfig {
            ssid: ssid.to_vec(),
            security_type: derive_security_type(&credential),
            credential,
            has_ever_connected: false,
            seen_in_passive_scan_results: false,
            perf_stats: PerformanceStats::new(),
        }
    }
}
