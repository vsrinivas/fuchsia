// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::store::{
    keys::{
        BONDING_DATA_PREFIX,
        bonding_data_key,
    },
    serde::{
        BondingDataDeserializer,
        BondingDataSerializer,
    }
};

use {
    failure::Error,
    serde_json,
    std::collections::HashMap,

    // Fuchsia libraries
    fuchsia_bluetooth::error::Error as BtError,
    fuchsia_syslog::{fx_log_info, fx_log_err},

    // FIDL services
    fidl::endpoints::create_proxy,
    fidl_fuchsia_bluetooth_host::BondingData,
    fidl_fuchsia_stash::{
        GetIteratorMarker,
        StoreAccessorMarker,
        StoreAccessorProxy,
        StoreMarker,
        Value,
    }
};

/// Stash manages persistent data that is stored in bt-gap's component-specific storage.
#[derive(Debug)]
pub struct Stash {
    // The proxy to the Fuchsia stash service. This is assumed to have been initialized as a
    // read/write capable accessor with the identity of the current component.
    proxy: StoreAccessorProxy,

    // In-memory state of the bonding data stash. Each entry is hierarchically indexed by a
    // local Bluetooth adapter identity and a peer device identifier.
    bonding_data: HashMap<String, HashMap<String, BondingData>>,
}

impl Stash {
    /// Updates the bonding data for a given device. Creates a new entry if one matching this
    /// device does not exist.
    pub fn store_bond(&mut self, data: BondingData) -> Result<(), Error> {
        fx_log_info!("store_bond (id: {})", data.identifier);

        // Persist the serialized blob.
        let serialized = serde_json::to_string(&BondingDataSerializer(&data))?;
        self.proxy.set_value(
            &bonding_data_key(&data.identifier),
            &mut Value::Stringval(serialized),
        )?;
        self.proxy.commit()?;

        // Update the in memory cache.
        let local_map = self
            .bonding_data
            .entry(data.local_address.clone())
            .or_insert(HashMap::new());
        local_map.insert(data.identifier.clone(), data);
        Ok(())
    }

    /// Returns an iterator over the bonding data entries for the local adapter with the given
    /// `address`. Returns None if no such data exists.
    pub fn list_bonds(&self, local_address: &str) -> Option<impl Iterator<Item = &BondingData>> {
        Some(self.bonding_data.get(local_address)?.values().into_iter())
    }

    // Initializes the stash using the given `accessor`. This asynchronously loads existing
    // stash data. Returns an error in case of failure.
    async fn new(accessor: StoreAccessorProxy) -> Result<Stash, Error> {
        // Obtain a list iterator for all cached bonding data.
        let (iter, server_end) = create_proxy::<GetIteratorMarker>()?;
        accessor.get_prefix(BONDING_DATA_PREFIX, server_end)?;

        let mut bonding_map = HashMap::new();
        loop {
            let next = await!(iter.get_next())?;
            if next.is_empty() {
                break;
            }
            for key_value in next {
                if let Value::Stringval(json) = key_value.val {
                    let bonding_data: BondingDataDeserializer = serde_json::from_str(&json)?;
                    let bonding_data = bonding_data.contents();
                    let local_address_entries = bonding_map
                        .entry(bonding_data.local_address.clone())
                        .or_insert(HashMap::new());
                    local_address_entries.insert(bonding_data.identifier.clone(), bonding_data);
                } else {
                    fx_log_err!("stash malformed: bonding data should be a string");
                    return Err(BtError::new("failed to initialize stash").into());
                }
            }
        }
        Ok(Stash {
            proxy: accessor,
            bonding_data: bonding_map,
        })
    }
}

/// Connects to the stash service and initializes a Stash object. This function obtains
/// read/write capability to the component-specific storage identified by `component_id`.
pub async fn init_stash(component_id: &str) -> Result<Stash, Error> {
    let stash_svc = fuchsia_app::client::connect_to_service::<StoreMarker>()?;
    stash_svc.identify(component_id)?;

    let (proxy, server_end) = create_proxy::<StoreAccessorMarker>()?;
    stash_svc.create_accessor(false, server_end)?;

    await!(Stash::new(proxy))
}

// These tests access stash in a hermetic envionment and thus it's ok for state to leak between
// test runs, regardless of test failure. Each test clears out the state in stash before performing
// its test logic.
#[cfg(test)]
mod tests {
    use super::*;
    use {fuchsia_app::client::connect_to_service,
         fuchsia_async as fasync,
         pin_utils::pin_mut};

    fn create_stash_accessor() -> Result<StoreAccessorProxy, Error> {
        let stashserver = connect_to_service::<StoreMarker>()?;

        // Identify
        stashserver.identify(BONDING_DATA_PREFIX)?;

        // Create an accessor
        let (acc, server_end) = create_proxy()?;
        stashserver.create_accessor(false, server_end)?;
        Ok(acc)
    }

    fn clear_data_in_stash(acc: &StoreAccessorProxy) -> Result<(), Error> {
        acc.delete_prefix("")?;
        acc.commit()?;
        Ok(())
    }

    #[test]
    fn new_stash_succeeds_with_empty_values() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        // Create a Stash service interface.
        let accessor_proxy =
            create_stash_accessor().expect("failed to create StashAccessor");
        clear_data_in_stash(&accessor_proxy).expect("failed to clear data in stash");
        let stash_new_future = Stash::new(accessor_proxy);
        pin_mut!(stash_new_future);

        // The stash should be initialized with no data.
        assert!(exec.run_singlethreaded(stash_new_future)
                    .expect("expected Stash to initialize")
                    .bonding_data.is_empty());
    }

    #[test]
    fn new_stash_fails_with_malformed_key_value_entry() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        // Create a Stash service interface.
        let accessor_proxy =
            create_stash_accessor().expect("failed to create StashAccessor");
        clear_data_in_stash(&accessor_proxy).expect("failed to clear data in stash");

        // Set a key/value that contains a non-string value.
        accessor_proxy.set_value("bonding-data:test1234", &mut Value::Intval(5))
            .expect("failed to set a bonding data value");
        accessor_proxy.commit()
            .expect("failed to commit a bonding data value");

        // The stash should fail to initialize.
        let stash_new_future = Stash::new(accessor_proxy);
        assert!(exec.run_singlethreaded(stash_new_future).is_err());
    }

    #[test]
    fn new_stash_fails_with_malformed_json() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        // Create a mock Stash service interface.
        let accessor_proxy =
            create_stash_accessor().expect("failed to create StashAccessor");
        clear_data_in_stash(&accessor_proxy).expect("failed to clear data in stash");

        // Set a vector that contains a malformed JSON value
        accessor_proxy.set_value("bonding-data:test1234", &mut Value::Stringval("{0}".to_string()))
            .expect("failed to set a bonding data value");
        accessor_proxy.commit()
            .expect("failed to commit a bonding data value");

        // The stash should fail to initialize.
        let stash_new_future = Stash::new(accessor_proxy);
        assert!(exec.run_singlethreaded(stash_new_future).is_err());
    }

    #[test]
    fn new_stash_succeeds_with_values() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        // Create a Stash service interface.
        let accessor_proxy =
            create_stash_accessor().expect("failed to create StashAccessor");
        clear_data_in_stash(&accessor_proxy).expect("failed to clear data in stash");


        // Insert values into stash that contain bonding data for several devices.
        accessor_proxy.set_value("bonding-data:id-1", &mut Value::Stringval(
                    r#"
                    {
                       "identifier": "id-1",
                       "localAddress": "00:00:00:00:00:01",
                       "name": "Test Device 1",
                       "le": null
                    }"#
                    .to_string(),
                )).expect("failed to set value");
        accessor_proxy.set_value("bonding-data:id-2", &mut Value::Stringval(
                    r#"
                    {
                       "identifier": "id-2",
                       "localAddress": "00:00:00:00:00:01",
                       "name": "Test Device 2",
                       "le": null
                    }"#
                    .to_string(),
                )).expect("failed to set value");
        accessor_proxy.set_value("bonding-data:id-3", &mut Value::Stringval(
                    r#"
                    {
                       "identifier": "id-3",
                       "localAddress": "00:00:00:00:00:02",
                       "name": null,
                       "le": null
                    }"#
                    .to_string(),
                )).expect("failed to set value");
        accessor_proxy.commit()
            .expect("failed to commit bonding data values");

        // The stash should initialize with bonding data stored in stash
        let stash_new_future = Stash::new(accessor_proxy);
        let stash = exec.run_singlethreaded(stash_new_future).expect("stash failed to initialize");

        // There should be devices registered for two local addresses.
        assert_eq!(2, stash.bonding_data.len());

        // The first local address should have two devices associated with it.
        let local = stash
            .bonding_data
            .get("00:00:00:00:00:01")
            .expect("could not find local address entries");
        assert_eq!(2, local.len());
        assert_eq!(
            &BondingData {
                identifier: "id-1".to_string(),
                local_address: "00:00:00:00:00:01".to_string(),
                name: Some("Test Device 1".to_string()),
                le: None,
            },
            local.get("id-1").expect("could not find device")
        );
        assert_eq!(
            &BondingData {
                identifier: "id-2".to_string(),
                local_address: "00:00:00:00:00:01".to_string(),
                name: Some("Test Device 2".to_string()),
                le: None,
            },
            local.get("id-2").expect("could not find device")
        );

        // The second local address should have one device associated with it.
        let local = stash
            .bonding_data
            .get("00:00:00:00:00:02")
            .expect("could not find local address entries");
        assert_eq!(1, local.len());
        assert_eq!(
            &BondingData {
                identifier: "id-3".to_string(),
                local_address: "00:00:00:00:00:02".to_string(),
                name: None,
                le: None,
            },
            local.get("id-3").expect("could not find device")
        );
    }

    // TODO(NET-1928): deflake and removed #[ignore]. reproduction:
    //
    // run_test_component bt-gap-unittests store_bond_commits_entry --ignored
    #[test]
    #[ignore]
    fn store_bond_commits_entry() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let accessor_proxy =
            create_stash_accessor().expect("failed to create StashAccessor");
        clear_data_in_stash(&accessor_proxy).expect("failed to clear data in stash");
        let mut stash = exec.run_singlethreaded(Stash::new(accessor_proxy))
            .expect("stash failed to initialize");

        let accessor_proxy_2 =
            create_stash_accessor().expect("failed to create StashAccessor");

        let bonding_data = BondingData {
            identifier: "id-1".to_string(),
            local_address: "00:00:00:00:00:01".to_string(),
            name: None,
            le: None,
        };
        assert!(stash.store_bond(bonding_data).is_ok());

        // Make sure that the in-memory cache has been updated.
        assert_eq!(1, stash.bonding_data.len());
        assert_eq!(
            &BondingData {
                identifier: "id-1".to_string(),
                local_address: "00:00:00:00:00:01".to_string(),
                name: None,
                le: None,
            },
            stash
                .bonding_data
                .get("00:00:00:00:00:01")
                .unwrap()
                .get("id-1")
                .unwrap()
        );

        // The new data should be accessible over FIDL.
        assert_eq!(exec.run_singlethreaded(accessor_proxy_2.get_value("bonding-data:id-1"))
                       .expect("failed to get value")
                       .map(|x| *x),
            Some(Value::Stringval(
                "{\"identifier\":\"id-1\",\"localAddress\":\"00:00:00:00:00:01\",\"name\":null,\
                 \"le\":null}"
                    .to_string()
            )));
    }

    #[test]
    fn list_bonds() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let accessor_proxy =
            create_stash_accessor().expect("failed to create StashAccessor");
        clear_data_in_stash(&accessor_proxy).expect("failed to clear data in stash");

        // Insert values into stash that contain bonding data for several devices.
        accessor_proxy.set_value("bonding-data:id-1", &mut Value::Stringval(
                    r#"
                    {
                       "identifier": "id-1",
                       "localAddress": "00:00:00:00:00:01",
                       "name": null,
                       "le": null
                    }"#
                    .to_string(),
                )).expect("failed to set value");
        accessor_proxy.set_value("bonding-data:id-2", &mut Value::Stringval(
                    r#"
                    {
                       "identifier": "id-2",
                       "localAddress": "00:00:00:00:00:01",
                       "name": null,
                       "le": null
                    }"#
                    .to_string(),
                )).expect("failed to set value");
        accessor_proxy.commit()
            .expect("failed to commit bonding data values");

        let stash = exec.run_singlethreaded(Stash::new(accessor_proxy))
            .expect("stash failed to initialize");

        // Should return None for unknown address.
        assert!(stash.list_bonds("00:00:00:00:00:00").is_none());

        let mut iter = stash
            .list_bonds("00:00:00:00:00:01")
            .expect("expected to find address");
        let next_id = &iter.next().unwrap().identifier;
        assert!("id-1" == next_id.as_str() || "id-2" == next_id.as_str());
        let next_id = &iter.next().unwrap().identifier;
        assert!("id-1" == next_id.as_str() || "id-2" == next_id.as_str());
        assert_eq!(None, iter.next());
    }
}
