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
    fuchsia_syslog::{fx_log, fx_log_info, fx_log_err},

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

// TODO(armansito): The following tests exercise interactions with the stash storage by mocking
// some of the fuchsia.stash FIDL library interfaces. It would be nicer if the stash service
// were to provide similar functionality for testing.
#[cfg(test)]
mod tests {
    use super::*;
    use {fidl_fuchsia_stash::{GetIteratorRequest, GetIteratorRequestStream, KeyValue,
                              StoreAccessorRequest, StoreAccessorRequestStream},
         fuchsia_async as fasync,
         futures::{stream::StreamExt, Stream},
         pin_utils::pin_mut,
         std::task::Poll::{Pending, Ready}};

    fn create_stash_accessor() -> Result<(StoreAccessorProxy, StoreAccessorRequestStream), Error> {
        let (proxy, server_end) = create_proxy::<StoreAccessorMarker>()?;
        let request_stream = server_end.into_stream()?;
        Ok((proxy, request_stream))
    }

    // Wait for the next FIDL message received from `server` and return its payload.
    fn expect_msg<T, S>(exec: &mut fasync::Executor, server: &mut S) -> Option<T>
    where
        S: Stream<Item = Result<T, fidl::Error>> + std::marker::Unpin,
    {
        match exec.run_until_stalled(&mut server.next()) {
            Ready(Some(Ok(req))) => Some(req),
            _ => None,
        }
    }

    // Emulators for StoreAccessor methods:
    fn expect_get_prefix(
        expected_prefix: &str, exec: &mut fasync::Executor, server: &mut StoreAccessorRequestStream,
    ) -> Option<GetIteratorRequestStream> {
        match expect_msg(exec, server) {
            Some(StoreAccessorRequest::GetPrefix {
                prefix,
                it,
                control_handle: _,
            }) => {
                // GetPrefix should be called for the requested `expected_prefix`.
                assert_eq!(expected_prefix, prefix);
                Some(it.into_stream().unwrap())
            }
            _ => None,
        }
    }

    fn expect_set_value(
        exec: &mut fasync::Executor, server: &mut StoreAccessorRequestStream,
    ) -> Option<(String, Value)> {
        match expect_msg(exec, server) {
            Some(StoreAccessorRequest::SetValue {
                key,
                val,
                control_handle: _,
            }) => Some((key, val)),
            _ => None,
        }
    }

    fn expect_commit(
        exec: &mut fasync::Executor, server: &mut StoreAccessorRequestStream,
    ) -> Option<()> {
        match expect_msg(exec, server) {
            Some(StoreAccessorRequest::Commit { control_handle: _ }) => Some(()),
            _ => None,
        }
    }

    // Emulators for GetIterator methods:
    fn expect_get_next(
        mut retval: Vec<KeyValue>, exec: &mut fasync::Executor,
        server: &mut GetIteratorRequestStream,
    ) -> Option<()> {
        match expect_msg(exec, server) {
            Some(GetIteratorRequest::GetNext { responder }) => {
                match responder.send(&mut retval.iter_mut()) {
                    Err(e) => panic!("Failed to send FIDL response: {:?}", e),
                    _ => Some(()),
                }
            }
            _ => None,
        }
    }

    // Replays the initial set of interactions to initialize a Stash object with the given bonding
    // data contents.
    fn initialize_stash(
        bonding_data: Vec<KeyValue>, exec: &mut fasync::Executor,
    ) -> Option<(Stash, StoreAccessorRequestStream)> {
        let (accessor_proxy, mut fake_server) =
            create_stash_accessor().expect("failed to create fake StashAccessor");
        let stash_new_future = Stash::new(accessor_proxy);
        pin_mut!(stash_new_future);

        // Send the request and expect to receive a GetIterator server handle.
        assert_eq!(
            Pending,
            exec.run_until_stalled(&mut stash_new_future).map(|_| ())
        );
        let mut get_iter_stream = expect_get_prefix(BONDING_DATA_PREFIX, exec, &mut fake_server)
            .expect("GetPrefix not called");

        let is_empty = bonding_data.is_empty();
        expect_get_next(bonding_data, exec, &mut get_iter_stream).expect("GetNext not called");

        // If `bonding_data` is non-empty then there will be one final request which we reply to
        // with an empty vector.
        if !is_empty {
            assert_eq!(
                Pending,
                exec.run_until_stalled(&mut stash_new_future).map(|_| ())
            );
            expect_get_next(vec![], exec, &mut get_iter_stream).expect("GetNext not called");
        }

        match exec.run_until_stalled(&mut stash_new_future) {
            Ready(Ok(stash)) => Some((stash, fake_server)),
            _ => None,
        }
    }

    #[test]
    fn new_stash_succeeds_with_empty_values() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        // Create a mock Stash service interface.
        let (accessor_proxy, mut fake_server) =
            create_stash_accessor().expect("failed to create fake StashAccessor");
        let stash_new_future = Stash::new(accessor_proxy);
        pin_mut!(stash_new_future);

        // Send the request and expect to receive a GetIterator server handle.
        assert_eq!(
            Pending,
            exec.run_until_stalled(&mut stash_new_future).map(|_| ())
        );
        let mut get_iter_stream =
            expect_get_prefix(BONDING_DATA_PREFIX, &mut exec, &mut fake_server)
                .expect("GetPrefix not called");

        // Reply with an empty vector.
        expect_get_next(vec![], &mut exec, &mut get_iter_stream).expect("GetNext not called");

        // The stash should be initialized with no data.
        match exec.run_until_stalled(&mut stash_new_future) {
            Ready(result) => {
                let stash = result.expect("Stash should have initialized successfully");
                assert!(stash.bonding_data.is_empty());
            }
            _ => panic!("expected Stash to initialize"),
        };
    }

    #[test]
    fn new_stash_fails_with_malformed_key_value_entry() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        // Create a mock Stash service interface.
        let (accessor_proxy, mut fake_server) =
            create_stash_accessor().expect("failed to create fake StashAccessor");
        let stash_new_future = Stash::new(accessor_proxy);
        pin_mut!(stash_new_future);

        // Send the request and expect to receive a GetIterator server handle.
        assert_eq!(
            Pending,
            exec.run_until_stalled(&mut stash_new_future).map(|_| ())
        );
        let mut get_iter_stream =
            expect_get_prefix(BONDING_DATA_PREFIX, &mut exec, &mut fake_server)
                .expect("GetPrefix not called");

        // Reply with a vector that contains a non-string value.
        let malformed_result = vec![KeyValue {
            key: "bonding-data:test1234".to_string(),
            val: Value::Intval(5),
        }];
        expect_get_next(malformed_result, &mut exec, &mut get_iter_stream)
            .expect("GetNext not called");

        // The stash should fail to initialize.
        match exec.run_until_stalled(&mut stash_new_future) {
            Ready(result) => {
                assert!(result.is_err());
            }
            _ => panic!("expected Stash to initialize"),
        };
    }

    #[test]
    fn new_stash_fails_with_malformed_json() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        // Create a mock Stash service interface.
        let (accessor_proxy, mut fake_server) =
            create_stash_accessor().expect("failed to create fake StashAccessor");
        let stash_new_future = Stash::new(accessor_proxy);
        pin_mut!(stash_new_future);

        // Send the request and expect to receive a GetIterator server handle.
        assert_eq!(
            Pending,
            exec.run_until_stalled(&mut stash_new_future).map(|_| ())
        );
        let mut get_iter_stream =
            expect_get_prefix(BONDING_DATA_PREFIX, &mut exec, &mut fake_server)
                .expect("GetPrefix not called");

        // Reply with a vector that contains a malformed JSON value
        let malformed_result = vec![KeyValue {
            key: "bonding-data:test1234".to_string(),
            val: Value::Stringval("{0}".to_string()),
        }];
        expect_get_next(malformed_result, &mut exec, &mut get_iter_stream)
            .expect("GetNext not called");

        // The stash should fail to initialize.
        match exec.run_until_stalled(&mut stash_new_future) {
            Ready(result) => {
                assert!(result.is_err());
            }
            _ => panic!("expected Stash to initialize"),
        };
    }

    #[test]
    fn new_stash_succeeds_with_values() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        // Create a mock Stash service interface.
        let (accessor_proxy, mut fake_server) =
            create_stash_accessor().expect("failed to create fake StashAccessor");
        let stash_new_future = Stash::new(accessor_proxy);
        pin_mut!(stash_new_future);

        // Send the request and expect to receive a GetIterator server handle.
        assert_eq!(
            Pending,
            exec.run_until_stalled(&mut stash_new_future).map(|_| ())
        );
        let mut get_iter_stream =
            expect_get_prefix(BONDING_DATA_PREFIX, &mut exec, &mut fake_server)
                .expect("GetPrefix not called");

        // Reply with a vector that contains bonding data for several devices.
        let valid_result = vec![
            KeyValue {
                key: "bonding-data:id-1".to_string(),
                val: Value::Stringval(
                    r#"
                    {
                       "identifier": "id-1",
                       "localAddress": "00:00:00:00:00:01",
                       "name": "Test Device 1",
                       "le": null
                    }"#
                    .to_string(),
                ),
            },
            KeyValue {
                key: "bonding-data:id-2".to_string(),
                val: Value::Stringval(
                    r#"
                    {
                       "identifier": "id-2",
                       "localAddress": "00:00:00:00:00:01",
                       "name": "Test Device 2",
                       "le": null
                    }"#
                    .to_string(),
                ),
            },
            KeyValue {
                key: "bonding-data:id-3".to_string(),
                val: Value::Stringval(
                    r#"
                    {
                       "identifier": "id-3",
                       "localAddress": "00:00:00:00:00:02",
                       "name": null,
                       "le": null
                    }"#
                    .to_string(),
                ),
            },
        ];
        expect_get_next(valid_result, &mut exec, &mut get_iter_stream).expect("GetNext not called");

        // The stash Future should remain pending until we send an empty vector.
        assert_eq!(
            Pending,
            exec.run_until_stalled(&mut stash_new_future).map(|_| ())
        );

        // The stash should initialize with bonding data after receiving empty vector.
        expect_get_next(vec![], &mut exec, &mut get_iter_stream).expect("GetNext not called");
        let stash = match exec.run_until_stalled(&mut stash_new_future) {
            Ready(result) => result.expect("expected stash to successfully initialize"),
            _ => panic!("expected Stash to initialize"),
        };

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

    #[test]
    fn store_bond_commits_entry() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (mut stash, mut store_accessor_server) =
            initialize_stash(vec![], &mut exec).expect("failed to initialize empty stash");

        let bonding_data = BondingData {
            identifier: "id-1".to_string(),
            local_address: "00:00:00:00:00:01".to_string(),
            name: None,
            le: None,
        };
        assert!(stash.store_bond(bonding_data).is_ok());

        // The data should be committed over FIDL.
        let (key, value) =
            expect_set_value(&mut exec, &mut store_accessor_server).expect("SetValue not called");
        assert_eq!("bonding-data:id-1", &key);
        assert_eq!(
            Value::Stringval(
                "{\"identifier\":\"id-1\",\"localAddress\":\"00:00:00:00:00:01\",\"name\":null,\
                 \"le\":null}"
                    .to_string()
            ),
            value
        );
        expect_commit(&mut exec, &mut store_accessor_server).expect("Commit not called");

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
    }

    #[test]
    fn list_bonds() {
        let data = vec![
            KeyValue {
                key: "bonding-data:id-1".to_string(),
                val: Value::Stringval(
                    r#"
                    {
                       "identifier": "id-1",
                       "localAddress": "00:00:00:00:00:01",
                       "name": null,
                       "le": null
                    }"#
                    .to_string(),
                ),
            },
            KeyValue {
                key: "bonding-data:id-2".to_string(),
                val: Value::Stringval(
                    r#"
                    {
                       "identifier": "id-2",
                       "localAddress": "00:00:00:00:00:01",
                       "name": null,
                       "le": null
                    }"#
                    .to_string(),
                ),
            },
        ];
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (stash, _) =
            initialize_stash(data, &mut exec).expect("failed to initialize empty stash");

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
