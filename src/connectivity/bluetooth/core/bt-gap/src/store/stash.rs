// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_stash::{
        GetIteratorMarker, KeyValue, SecureStoreMarker, StoreAccessorMarker, StoreAccessorProxy,
        Value,
    },
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        error::Error as BtError,
        inspect::Inspectable,
        types::{Address, BondingData, HostData, PeerId},
    },
    fuchsia_inspect,
    futures::{
        channel::{mpsc, oneshot},
        future::{Future, FutureExt},
        stream::StreamExt,
    },
    log::{error, info, warn},
    serde_json,
    std::collections::HashMap,
};

#[cfg(test)]
use {
    fuchsia_bluetooth::types::{LeBondData, OneOrBoth},
    fuchsia_inspect::testing::DiagnosticsHierarchyGetter,
    std::collections::HashSet,
};

use crate::store::{
    keys::{
        bonding_data_key, host_data_key, host_id_from_key, BONDING_DATA_PREFIX, HOST_DATA_PREFIX,
    },
    serde::{
        BondingDataDeserializer, BondingDataSerializer, HostDataDeserializer, HostDataSerializer,
    },
};

#[cfg(test)]
use crate::store::in_memory::InMemoryStore;

/// These requests define the API surface for Stash. Each request signifies an atomic transaction that
/// the bt-gap stash can take
#[derive(Debug)]
pub(crate) enum Request {
    /// Store 1 or more Bonds in the stash.
    StoreBonds(Vec<BondingData>, oneshot::Sender<Result<(), Error>>),

    /// Completely remove a Peer and all its bonds from the stash.
    RmPeer(PeerId, oneshot::Sender<Result<(), Error>>),

    /// Updates the host data for the host with the given identity address.
    StoreHostData(Address, HostData, oneshot::Sender<Result<(), Error>>),

    /// Returns the local host data for the given local `address`.
    GetHostData(Address, oneshot::Sender<Option<HostData>>),

    /// Returns an iterator over the bonding data entries for the local adapter with the given
    /// `address`. Returns None if no such data exists.
    ListBonds(Address, oneshot::Sender<Option<Vec<BondingData>>>),
}

/// Size (in items) of the Stash Request channel buffer. It is possible for multiple items to be
/// queued at once, as the host-dispatcher can enqueue requests in response to both host activity
/// and also the activity of its fidl clients. Therefore we need >0 extra slots. 128 has been
/// un-scientifically chosen as a number which is estimated to be:
///   a) small enough that the size of the buffer will have negligible memory impact
///   b) large enough to prevent send blocking in all but the rarest cases
/// It is considered currently that further effort determining an optimum size will have little
/// value; if that changes that we should more empirically evaluate an effective buffer size
const STASH_MSG_QUEUE_CAPACITY: usize = 128;

/// Clients interface with the Stash via the mechanism of a multiple-producer, single-consumer
/// queue. By handling all requests via this queue, we enforce linearization (and hence atomicity)
/// of stash updates
#[derive(Clone, Debug)]
pub struct Stash(mpsc::Sender<Request>);

impl Stash {
    pub fn store_bond(&mut self, bond: BondingData) -> impl Future<Output = Result<(), Error>> {
        self.send_req(move |send| Request::StoreBonds(vec![bond], send)).map(|r| r.and_then(|r| r))
    }
    pub fn store_bonds(
        &mut self,
        bonds: Vec<BondingData>,
    ) -> impl Future<Output = Result<(), Error>> {
        self.send_req(move |send| Request::StoreBonds(bonds, send)).map(|r| r.and_then(|r| r))
    }
    pub fn rm_peer(&mut self, peer: PeerId) -> impl Future<Output = Result<(), Error>> {
        self.send_req(move |send| Request::RmPeer(peer, send)).map(|r| r.and_then(|r| r))
    }
    pub fn store_host_data(
        &mut self,
        local_address: Address,
        data: HostData,
    ) -> impl Future<Output = Result<(), Error>> {
        self.send_req(move |send| Request::StoreHostData(local_address, data, send))
            .map(|r| r.and_then(|r| r))
    }
    pub fn list_bonds(
        &mut self,
        local_address: Address,
    ) -> impl Future<Output = Result<Option<Vec<BondingData>>, Error>> {
        self.send_req(move |send| Request::ListBonds(local_address, send))
    }
    pub fn get_host_data(
        &mut self,
        local_address: Address,
    ) -> impl Future<Output = Result<Option<HostData>, Error>> {
        self.send_req(move |send| Request::GetHostData(local_address, send))
    }

    /// Construct a Request with one half of a oneshot channel, and use the second half to await
    /// the result of the request. This formulation ensures that the correct return type is used
    fn send_req<T, F>(&mut self, build_request: F) -> impl Future<Output = Result<T, Error>>
    where
        F: FnOnce(oneshot::Sender<T>) -> Request,
    {
        let (send, recv) = oneshot::channel();
        let sent = self.0.try_send(build_request(send));
        async {
            match sent {
                Ok(_) => match recv.await {
                    Err(oneshot::Canceled) => {
                        return Err(format_err!("Response future was canceled"))
                    }
                    Ok(r) => Ok(r),
                },
                Err(e) => Err(format_err!("Error communicating with bt-gap store: {}", e)),
            }
        }
    }

    #[cfg(test)]
    pub fn in_memory_mock() -> Stash {
        let (sender, receiver) = mpsc::channel::<Request>(STASH_MSG_QUEUE_CAPACITY);
        let mut store = InMemoryStore::default();
        fasync::Task::spawn(
            receiver.for_each(move |request| futures::future::ready(store.handle_request(request))),
        )
        .detach();
        Stash(sender)
    }
}

async fn run_stash(mut inbox: mpsc::Receiver<Request>, mut stash: StashInner) -> Result<(), Error> {
    while let Some(event) = inbox.next().await {
        match event {
            Request::StoreBonds(bonds, signal) => {
                let response = stash.store_bonds(bonds).await;
                if let Err(_) = signal.send(response) {
                    return Err(format_err!("Failed to send response"));
                }
            }
            Request::RmPeer(peer, signal) => {
                let response = stash.rm_peer(peer).await;
                if let Err(_) = signal.send(response) {
                    return Err(format_err!("Failed to send response"));
                }
            }
            Request::StoreHostData(address, data, signal) => {
                let response = stash.store_host_data(&address, data).await;
                if let Err(_) = signal.send(response) {
                    return Err(format_err!("Failed to send response"));
                }
            }
            Request::ListBonds(address, signal) => {
                let response = stash.list_bonds(&address);
                if let Err(_) = signal.send(response) {
                    return Err(format_err!("Failed to send response"));
                };
            }
            Request::GetHostData(address, signal) => {
                let response = stash.get_host_data(&address);
                if let Err(_) = signal.send(response) {
                    return Err(format_err!("Failed to send response"));
                };
            }
        }
    }
    Ok(())
}

/// Stash manages persistent data that is stored in bt-gap's component-specific storage. Data is
/// persisted in JSON format using the facilities provided by the serde library (see the
/// declarations in serde.rs for the description of the data format).
///
/// The stash currently stores the following types of data:
///
/// Bonding Data
/// ============
/// Data for all bonded peers are each stored as a unique entry. The key for each bonding data
/// entry has the following format:
///
///     "bonding-data:<device-id>"
///
/// where <device-id> is a unique device identifier generated by the bt-host that has a bond with
/// the peer. The structure of the key allows all bonding data to be fetched from the stash by
/// requesting the "bonding-data:" prefix. Individual entries can be fetched and stored by providing
/// the complete key.
///
/// Each bonding data entry contains the local bt-host identity address that it belongs to.
///
/// Host Data
/// =========
/// Data specific to a local bt-host identity are stored as a unique entry. The key for each host
/// data entry has the following format:
///
///     "host-data:<host-identity-address>"
///
/// where <host-identity-address> is a Bluetooth device address (e.g.
/// "host-data:01:02:03:04:05:06").
#[derive(Debug)]
struct StashInner {
    /// The proxy to the Fuchsia stash service. This is assumed to have been initialized as a
    /// read/write capable accessor with the identity of the current component.
    proxy: StoreAccessorProxy,

    /// In-memory state of the bonding data stash. Each entry is hierarchically indexed by a
    /// local Bluetooth host identity and the resolved peer address.
    bonding_data: HashMap<Address, HashMap<PeerId, Inspectable<BondingData>>>,

    /// Persisted data for a particular local Bluetooth host, indexed by local Bluetooth host
    /// identity.
    host_data: HashMap<Address, HostData>,

    /// Handle to inspect data
    inspect: fuchsia_inspect::Node,
}

fn bond_inspect_identifier(peer_id: PeerId) -> String {
    format!("bond {}", peer_id)
}

fn insert_inspectable_bonds(
    data: &mut HashMap<Address, HashMap<PeerId, Inspectable<BondingData>>>,
    inspect: &fuchsia_inspect::Node,
    bonds: Vec<BondingData>,
) {
    for bond in bonds {
        let (local_address, identifier) = (bond.local_address, bond.identifier);
        let node = inspect.create_child(bond_inspect_identifier(identifier));
        let bond = Inspectable::new(bond, node);
        // Update the in memory cache.
        let host_bonds = data.entry(local_address).or_insert(HashMap::new());
        if host_bonds.insert(identifier, bond).is_some() {
            warn!("Replaced bond data for {} peer id {}", local_address, identifier);
        }
    }
}

/// Returns true if the underlying data in `lhs` is equivalent to `rhs`, aside from the
/// PeerId field, which is a Fuchsia-specific concept.
fn is_duplicate_bond(lhs: &BondingData, rhs: &BondingData) -> bool {
    let rhs_with_lhs_id = BondingData { identifier: lhs.identifier.clone(), ..rhs.clone() };
    *lhs == rhs_with_lhs_id
}

impl StashInner {
    /// Updates the bonding data for a given device. Creates a new entry if one matching this
    /// device does not exist.
    async fn store_bonds(&mut self, bonds: Vec<BondingData>) -> Result<(), Error> {
        for bond in bonds.iter() {
            info!("storing bond (id: {})", bond.identifier);
            // Persist the serialized blob.
            let serialized = serde_json::to_string(&BondingDataSerializer::new(&bond))?;
            self.proxy
                .set_value(&bonding_data_key(bond.identifier), &mut Value::Stringval(serialized))?;
        }
        self.proxy.flush().await?.map_err(|e| format_err!("Failed to flush to stash: {:?}", e))?;

        insert_inspectable_bonds(&mut self.bonding_data, &self.inspect, bonds);
        Ok(())
    }

    /// Returns an iterator over the bonding data entries for the local adapter with the given
    /// `address`. Returns None if no such data exists.
    fn list_bonds(&self, local_address: &Address) -> Option<Vec<BondingData>> {
        Some(
            self.bonding_data
                .get(local_address)?
                .values()
                .into_iter()
                .map(|bd| -> BondingData { (*bd).clone() })
                .collect(),
        )
    }

    /// Removes persisted bond for a peer and removes its information from any adapters that have
    /// it. Returns an error for failures but not if the peer isn't found.
    async fn rm_peer(&mut self, peer_id: PeerId) -> Result<(), Error> {
        info!("rm_peer (id: {})", peer_id);

        // Delete the persisted bond blob.
        self.proxy.delete_value(&bonding_data_key(peer_id))?;
        self.proxy.flush().await?.map_err(|e| format_err!("Failed to flush to stash: {:?}", e))?;

        // Delete peer from memory cache of all adapters.
        self.bonding_data.values_mut().for_each(|m| m.retain(|k, _| *k != peer_id));
        Ok(())
    }

    /// Returns the local host data for the given local `address`.
    fn get_host_data(&self, local_address: &Address) -> Option<HostData> {
        self.host_data.get(local_address).cloned()
    }

    /// Updates the host data for the host with the given identity address.
    async fn store_host_data(&mut self, local_addr: &Address, data: HostData) -> Result<(), Error> {
        info!("store_host_data (local address: {})", local_addr);

        // Persist the serialized blob.
        let serialized = serde_json::to_string(&HostDataSerializer(&data.clone().into()))?;
        self.proxy.set_value(&host_data_key(local_addr), &mut Value::Stringval(serialized))?;
        self.proxy.flush().await?.map_err(|e| format_err!("Failed to flush to stash: {:?}", e))?;

        // Update the in memory cache.
        let _ = self.host_data.insert(local_addr.clone(), data);
        Ok(())
    }

    // Initializes the stash using the given `accessor`. This asynchronously loads existing
    // stash data. Returns an error in case of failure.
    async fn new(
        accessor: StoreAccessorProxy,
        inspect: fuchsia_inspect::Node,
    ) -> Result<StashInner, Error> {
        let bonding_data = StashInner::load_bonds(&accessor, &inspect).await?;
        let host_data = StashInner::load_host_data(&accessor).await?;
        Ok(StashInner { proxy: accessor, bonding_data, host_data, inspect })
    }

    fn process_loaded_bonds(
        raw_bonds: Vec<KeyValue>,
        seen_addresses: &mut HashMap<(Address, Address), BondingData>,
        duplicate_ids: &mut Vec<PeerId>,
    ) -> Result<(), Error> {
        for key_value in raw_bonds {
            let bond = if let Value::Stringval(json) = key_value.val {
                BondingDataDeserializer::from_json(&json)
            } else {
                error!("stash malformed: bonding data should be a string");
                Err(format_err!("failed to initialize stash"))
            }?;
            if let Some((dupe, dupe_id)) =
                seen_addresses.get(&(bond.local_address, bond.address)).map(|b| (b, b.identifier))
            {
                // Generally, Fuchsia disallows restoration of multiple BondingDatas from the same
                // local address to the same peer address. However, some system bootstrap flows cause
                // the same underlying bond (i.e. security keys + local-peer address tuple) to be
                // restored more than once under different Peer IDs. To be resilient to this flow,
                // we deduplicate bonds which differ only in their PeerId from the Store as a
                // special case.
                if !is_duplicate_bond(&bond, dupe) {
                    warn!("stash malformed: cannot load multiple distinct bonds for same peer (address: {:?})", bond.address);
                    return Err(format_err!("multiple distinct bonds for same peer in store"));
                }
                duplicate_ids.push(dupe_id);
            }
            let _ = seen_addresses.insert((bond.local_address, bond.address), bond);
        }
        Ok(())
    }

    async fn load_bonds<'a>(
        accessor: &'a StoreAccessorProxy,
        inspect: &'a fuchsia_inspect::Node,
    ) -> Result<HashMap<Address, HashMap<PeerId, Inspectable<BondingData>>>, Error> {
        // Obtain a list iterator for all cached bonding data.
        let (iter, server_end) = create_proxy::<GetIteratorMarker>()?;
        accessor.get_prefix(BONDING_DATA_PREFIX, server_end)?;

        let mut bonding_map = HashMap::new();
        let mut seen_addresses = HashMap::new();
        let mut duplicate_ids = Vec::new();
        loop {
            let next = iter.get_next().await?;
            if next.is_empty() {
                break;
            }
            Self::process_loaded_bonds(next, &mut seen_addresses, &mut duplicate_ids)?;
        }
        let bonds = seen_addresses.into_values().collect();
        insert_inspectable_bonds(&mut bonding_map, &inspect, bonds);
        for id in duplicate_ids {
            info!("removing duplicate bond for peer id {:?} from store", id);
            accessor.delete_value(&bonding_data_key(id))?;
        }
        accessor.flush().await?.map_err(|e| format_err!("Failed to flush to stash: {:?}", e))?;
        Ok(bonding_map)
    }

    async fn load_host_data(
        accessor: &StoreAccessorProxy,
    ) -> Result<HashMap<Address, HostData>, Error> {
        // Obtain a list iterator for all cached host data.
        let (iter, server_end) = create_proxy::<GetIteratorMarker>()?;
        accessor.get_prefix(HOST_DATA_PREFIX, server_end)?;

        let mut host_data_map = HashMap::new();
        loop {
            let next = iter.get_next().await?;
            if next.is_empty() {
                break;
            }
            for key_value in next {
                let host_address = host_id_from_key(&key_value.key)?;
                let host_address = Address::public_from_str(&host_address)?;
                if let Value::Stringval(json) = key_value.val {
                    let host_data = HostDataDeserializer::from_json(&json)?;
                    if host_data_map.insert(host_address, host_data.into()).is_some() {
                        warn!("Replaced host data for {} while loading", host_address);
                    }
                } else {
                    error!("stash malformed: host data should be a string");
                    return Err(BtError::new("failed to initialize stash").into());
                }
            }
        }
        Ok(host_data_map)
    }
}

/// Connects to the stash service and initializes a Stash object. This function obtains
/// read/write capability to the component-specific storage identified by `component_id`.
pub async fn init_stash(
    component_id: &str,
    inspect: fuchsia_inspect::Node,
) -> Result<Stash, Error> {
    let stash_svc = fuchsia_component::client::connect_to_protocol::<SecureStoreMarker>()?;
    stash_svc.identify(component_id)?;

    let (proxy, server_end) = create_proxy::<StoreAccessorMarker>()?;
    stash_svc.create_accessor(false, server_end)?;

    let inner = StashInner::new(proxy, inspect).await?;
    let (stash, stash_run) = build_stash(inner);
    fasync::Task::spawn(stash_run.map(|r| {
        if let Err(e) = r {
            error!("Error running stash: {}", e);
        }
    }))
    .detach();
    Ok(stash)
}

fn build_stash(inner: StashInner) -> (Stash, impl Future<Output = Result<(), Error>>) {
    let (sender, receiver) = mpsc::channel::<Request>(STASH_MSG_QUEUE_CAPACITY);
    (Stash(sender), run_stash(receiver, inner))
}

// These tests access stash in a hermetic environment and thus it's ok for state to leak between
// test runs, regardless of test failure. Each test clears out the state in stash before performing
// its test logic.
#[cfg(test)]
mod tests {
    use super::*;
    use {
        core::hash::Hash, fidl_fuchsia_bluetooth_sys::Key,
        fuchsia_component::client::connect_to_protocol, futures::select, pin_utils::pin_mut,
    };

    static TEST_INSPECT_ROOT: &'static str = "test";
    // create_stash_accessor will create a new accessor to stash scoped under the given test name.
    // All preexisting data in stash under this identity is deleted before the accessor is
    // returned.
    async fn create_stash_accessor(test_name: &str) -> Result<StoreAccessorProxy, Error> {
        let stashserver = connect_to_protocol::<SecureStoreMarker>()?;

        // Identify
        stashserver.identify(&(BONDING_DATA_PREFIX.to_owned() + test_name))?;

        // Create an accessor
        let (acc, server_end) = create_proxy()?;
        stashserver.create_accessor(false, server_end)?;

        // Clear all data in stash under our identity
        acc.delete_prefix("")?;
        acc.flush().await?.map_err(|e| format_err!("Failed to flush to stash: {:?}", e))?;

        Ok(acc)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn new_stash_succeeds_with_empty_values() {
        let inspect = fuchsia_inspect::Inspector::new().root().create_child(TEST_INSPECT_ROOT);

        // Create a Stash service interface.
        let accessor = create_stash_accessor("new_stash_succeeds_with_empty_values")
            .await
            .expect("failed to create StashAccessor");
        let stash = StashInner::new(accessor, inspect).await.expect("expected Stash to initialize");

        // The stash should be initialized with no data.
        assert!(stash.bonding_data.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn new_stash_fails_with_malformed_key_value_entry() {
        let inspect = fuchsia_inspect::Inspector::new().root().create_child(TEST_INSPECT_ROOT);

        // Create a Stash service interface.
        let accessor = create_stash_accessor("new_stash_fails_with_malformed_key_value_entry")
            .await
            .expect("failed to create StashAccessor");

        // Set a key/value that contains a non-string value.
        accessor
            .set_value("bonding-data:test1234", &mut Value::Intval(5))
            .expect("failed to set a bonding data value");
        accessor
            .flush()
            .await
            .expect("failed to flush a bonding data value")
            .expect("failed to flush a bonding data value");

        // The stash should fail to initialize.
        assert!(StashInner::new(accessor, inspect).await.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn new_stash_fails_with_malformed_json() {
        let inspect = fuchsia_inspect::Inspector::new().root().create_child(TEST_INSPECT_ROOT);

        // Create a mock Stash service interface.
        let accessor = create_stash_accessor("new_stash_fails_with_malformed_json")
            .await
            .expect("failed to create StashAccessor");

        // Set a vector that contains a malformed JSON value
        accessor
            .set_value("bonding-data:test1234", &mut Value::Stringval("{0}".to_string()))
            .expect("failed to set a bonding data value");
        accessor
            .flush()
            .await
            .expect("failed to flush a bonding data value")
            .expect("failed to flush a bonding data value");

        // The stash should fail to initialize.
        assert!(StashInner::new(accessor, inspect).await.is_err());
    }

    fn host_data_1() -> HostData {
        HostData {
            irk: Some(Key { value: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16] }),
        }
    }

    fn host_data_2() -> HostData {
        HostData {
            irk: Some(Key { value: [16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1] }),
        }
    }

    fn host_text_1() -> Value {
        Value::Stringval(
            "{\"irk\":{\"value\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]}}".to_string(),
        )
    }

    fn host_text_2() -> Value {
        Value::Stringval(
            "{\"irk\":{\"value\":[16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1]}}".to_string(),
        )
    }

    fn default_le_data() -> LeBondData {
        LeBondData {
            connection_parameters: None,
            services: vec![],
            peer_ltk: None,
            local_ltk: None,
            irk: None,
            csrk: None,
        }
    }

    fn bond_data_1() -> BondingData {
        BondingData {
            identifier: PeerId(1),
            address: Address::Random([3, 0, 0, 0, 0, 0]),
            local_address: Address::Public([1, 0, 0, 0, 0, 0]),
            name: Some("Test Device 1".to_string()),
            data: OneOrBoth::Left(default_le_data()),
        }
    }
    fn bond_data_2() -> BondingData {
        BondingData {
            identifier: PeerId(2),
            address: Address::Random([4, 0, 0, 0, 0, 0]),
            local_address: Address::Public([1, 0, 0, 0, 0, 0]),
            name: Some("Test Device 2".to_string()),
            data: OneOrBoth::Left(default_le_data()),
        }
    }

    fn bond_data_3() -> BondingData {
        BondingData {
            identifier: PeerId(3),
            address: Address::Random([3, 0, 0, 0, 0, 0]),
            local_address: Address::Public([2, 0, 0, 0, 0, 0]),
            name: None,
            data: OneOrBoth::Left(default_le_data()),
        }
    }

    fn bond_data_4_dupes_3() -> BondingData {
        BondingData {
            identifier: PeerId(4),
            address: Address::Random([3, 0, 0, 0, 0, 0]),
            local_address: Address::Public([2, 0, 0, 0, 0, 0]),
            name: None,
            data: OneOrBoth::Left(default_le_data()),
        }
    }

    #[rustfmt::skip]
    fn bond_entry_1() -> Value {
        Value::Stringval(
            "{\
                \"identifier\":1,\
                \"address\":{\
                    \"type\":\"random\",\
                    \"value\":[3,0,0,0,0,0]\
                },\
                \"hostAddress\":{\
                    \"type\":\"public\",\
                    \"value\":[1,0,0,0,0,0]\
                },\
                \"name\":\"Test Device 1\",\
                \"le\":{\
                    \"connectionParameters\":null,\
                    \"peerLtk\":null,\
                    \"localLtk\":null,\
                    \"irk\":null,\
                    \"csrk\":null\
                },\
                \"bredr\":null\
            }"
            .to_string(),
        )
    }

    fn bond_entry_2() -> Value {
        Value::Stringval(
            r#"
            {
                "identifier": 2,
                "hostAddress": {
                    "type": "public",
                    "value": [1,0,0,0,0,0]
                },
                "address": {
                    "type": "random",
                    "value": [4,0,0,0,0,0]
                },
                "name": "Test Device 2",
                "le": {
                    "connectionParameters": null,
                    "peerLtk": null,
                    "localLtk": null,
                    "irk": null,
                    "csrk": null
                },
                "bredr": null
            }"#
            .to_string(),
        )
    }

    fn bond_entry_3() -> Value {
        Value::Stringval(
            r#"
            {
                "identifier": 3,
                "hostAddress": {
                    "type": "public",
                    "value": [2,0,0,0,0,0]
                },
                "address": {
                    "type": "random",
                    "value": [3,0,0,0,0,0]
                },
                "name": null,
                "le": {
                    "connectionParameters": null,
                    "peerLtk": null,
                    "localLtk": null,
                    "irk": null,
                    "csrk": null
                },
                "bredr": null
            }"#
            .to_string(),
        )
    }

    fn bond_entry_4_dupes_3() -> Value {
        Value::Stringval(
            r#"
            {
                "identifier": 4,
                "hostAddress": {
                    "type": "public",
                    "value": [2,0,0,0,0,0]
                },
                "address": {
                    "type": "random",
                    "value": [3,0,0,0,0,0]
                },
                "name": null,
                "le": {
                    "connectionParameters": null,
                    "peerLtk": null,
                    "localLtk": null,
                    "irk": null,
                    "csrk": null
                },
                "bredr": null
            }"#
            .to_string(),
        )
    }

    // This entry has the same hostAddress and address fields as entry 3, but populates the BR/EDR
    // bond data field instead of the LE bond data field.
    fn bond_entry_5_same_addrs_3() -> Value {
        Value::Stringval(
            r#"
            {
                "identifier": 5,
                "hostAddress": {
                    "type": "public",
                    "value": [2,0,0,0,0,0]
                },
                "address": {
                    "type": "random",
                    "value": [3,0,0,0,0,0]
                },
                "name": null,
                "le": null,
                "bredr": {
                    "rolePreference": null,
                    "services": [],
                    "linkKey": null
                }
            }"#
            .to_string(),
        )
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn new_stash_succeeds_with_values() {
        let inspect = fuchsia_inspect::Inspector::new().root().create_child(TEST_INSPECT_ROOT);

        // Create a Stash service interface.
        let accessor = create_stash_accessor("new_stash_succeeds_with_values")
            .await
            .expect("failed to create StashAccessor");

        // Insert values into stash that contain bonding data for several devices.
        accessor.set_value("bonding-data:1", &mut bond_entry_1()).expect("failed to set value");
        accessor.set_value("bonding-data:2", &mut bond_entry_2()).expect("failed to set value");
        accessor.set_value("bonding-data:3", &mut bond_entry_3()).expect("failed to set value");
        accessor
            .flush()
            .await
            .expect("failed to flush a bonding data value")
            .expect("failed to flush a bonding data value");

        // The stash should initialize with bonding data stored in stash
        let stash = StashInner::new(accessor, inspect).await.expect("stash failed to initialize");

        // There should be devices registered for two local addresses.
        assert_eq!(2, stash.bonding_data.len());

        // The first local address should have two devices associated with it.
        let local = stash
            .bonding_data
            .get(&Address::Public([1, 0, 0, 0, 0, 0]))
            .expect("could not find local address entries");
        assert_eq!(2, local.len());
        let bond: &BondingData = &*local.get(&PeerId(1)).expect("could not find device");
        assert_eq!(&bond_data_1(), bond);
        let bond: &BondingData = &*local.get(&PeerId(2)).expect("could not find device");
        assert_eq!(&bond_data_2(), bond);

        // The second local address should have one device associated with it.
        let local = stash
            .bonding_data
            .get(&Address::Public([2, 0, 0, 0, 0, 0]))
            .expect("could not find local address entries");
        assert_eq!(1, local.len());
        let bond: &BondingData = &*local.get(&PeerId(3)).expect("could not find device");
        assert_eq!(&bond_data_3(), bond);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn new_stash_filters_duplicate_bonds() {
        let inspector = fuchsia_inspect::Inspector::new();
        let inspect = inspector.root().create_child(TEST_INSPECT_ROOT);
        // Create a Stash service interface.
        let accessor = create_stash_accessor("new_stash_filters_duplicate_bonds")
            .await
            .expect("failed to create StashAccessor");

        // Insert values into stash that contain bonding data for several devices. Other tests use
        // simpler identifiers (e.g. `bonding-data:X`), but these cause issues when verifying Stash
        // interactions with the store, as Stash uses the full 16-bit, zero-padded fmt::Display
        // PeerId impl to create identifiers for the store.
        let (id_3_key, id_4_key) = (
            bonding_data_key(bond_data_3().identifier),
            bonding_data_key(bond_data_4_dupes_3().identifier),
        );
        accessor.set_value(&id_3_key, &mut bond_entry_3()).expect("failed to set value");
        accessor.set_value(&id_4_key, &mut bond_entry_4_dupes_3()).expect("failed to set value");
        accessor
            .flush()
            .await
            .expect("failed to flush a bonding data value")
            .expect("failed to flush a bonding data value");

        // The stash should initialize with bonding data stored in stash
        let stash =
            StashInner::new(accessor.clone(), inspect).await.expect("stash failed to initialize");

        // Although we added two bond entries for local host address [2, 0, ...] with distinct peer
        // IDs, they use the same address, so they should be deduplicated in the store, with no
        // guarantees about which bond is retained.
        let local = stash
            .bonding_data
            .get(&Address::Public([2, 0, 0, 0, 0, 0]))
            .expect("could not find local address entries");
        assert_eq!(1, local.len());

        // The duplicate should also be removed from the store so that the store matches what's in
        // memory, leaving only one entry.
        let (iter, server_end) = create_proxy::<GetIteratorMarker>().unwrap();
        accessor.get_prefix(BONDING_DATA_PREFIX, server_end).expect("failed to fetch bond data");
        let res = iter.get_next().await.unwrap();
        assert_eq!(1, res.len());
        assert!(iter.get_next().await.unwrap().is_empty());

        // The inspect hierarchy should contain exactly one bond node, deduplicated from the two
        // in the original store.
        let inspect_hierarchy = inspector.get_diagnostics_hierarchy();
        let test_hierarchy =
            inspect_hierarchy.get_child(TEST_INSPECT_ROOT).expect("missing test hierarchy node");
        let bond_3_record =
            test_hierarchy.get_child(&bond_inspect_identifier(bond_data_3().identifier));
        let bond_4_record =
            test_hierarchy.get_child(&bond_inspect_identifier(bond_data_4_dupes_3().identifier));
        if bond_3_record.is_some() {
            assert!(
                !bond_4_record.is_some(),
                "expected one deduplicated bond in Inspect, found both"
            );
        } else {
            assert!(bond_4_record.is_some(), "expected one bond record in Inspect, found none");
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn new_stash_fails_loading_same_addrs_different_bond() {
        fuchsia_syslog::init().unwrap();
        let inspect = fuchsia_inspect::Inspector::new().root().create_child(TEST_INSPECT_ROOT);

        // Create a Stash service interface.
        let accessor = create_stash_accessor("new_stash_fails_loading_same_addrs_different_bond")
            .await
            .expect("failed to create StashAccessor");

        accessor.set_value("bonding-data:3", &mut bond_entry_3()).expect("failed to set value");
        accessor
            .set_value(&"bonding-data:5", &mut bond_entry_5_same_addrs_3())
            .expect("failed to set value");
        accessor
            .flush()
            .await
            .expect("failed to flush a bonding data value")
            .expect("failed to flush a bonding data value");

        // Bond entry 5 uses the same local and peer addresses as bond entry 3, but the security
        // data itself differs between the entries. This indicates that the store is in an invalid
        // state, so we expect to fail initialization of the Stash.
        assert!(StashInner::new(accessor.clone(), inspect).await.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn store_bond_commits_entry() {
        let mut stash = setup_stash("store_bond_commits_entry", vec![]).await;
        let accessor = stash.proxy.clone();

        assert!(stash.store_bonds(vec![bond_data_1()]).await.is_ok());

        // Make sure that the in-memory cache has been updated.
        assert_eq!(1, stash.bonding_data.len());
        let bond: &BondingData = &*stash
            .bonding_data
            .get(&Address::Public([1, 0, 0, 0, 0, 0]))
            .unwrap()
            .get(&PeerId(1))
            .unwrap();
        assert_eq!(&bond_data_1(), bond);

        // The new data should be accessible over FIDL.
        let result = accessor.get_value("bonding-data:0000000000000001").await;
        let bond_data = result.expect("failed to get value").map(|x| *x);
        assert_eq!(bond_data, Some(bond_entry_1()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn list_bonds() {
        let initial_data =
            vec![("bonding-data:1", bond_entry_1()), ("bonding-data:2", bond_entry_2())];
        let stash = setup_stash("list_bonds", initial_data).await;

        // Should return None for unknown address.
        assert_eq!(stash.list_bonds(&Address::Public([0, 0, 0, 0, 0, 0])), None);

        let bonds = stash
            .list_bonds(&Address::Public([1, 0, 0, 0, 0, 0]))
            .expect("expected to find address");
        let ids: HashSet<PeerId> = bonds.iter().map(|bond| bond.identifier).collect();
        assert_eq!(ids, set_of(vec![PeerId(1), PeerId(2)]));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_host_data() {
        let initial_data = vec![
            ("host-data:00:00:00:00:00:01", host_text_1()),
            ("host-data:00:00:00:00:00:02", host_text_2()),
        ];
        let stash = setup_stash("get_host_data", initial_data).await;

        // Should return None for unknown identity address.
        assert!(stash.get_host_data(&Address::Public([0, 0, 0, 0, 0, 0])).is_none());

        let host_data = stash
            .get_host_data(&Address::Public([1, 0, 0, 0, 0, 0]))
            .expect("expected to find HostData");
        assert_eq!(host_data_1(), host_data);

        let host_data = stash
            .get_host_data(&Address::Public([2, 0, 0, 0, 0, 0]))
            .expect("expected to find HostData");
        assert_eq!(host_data_2(), host_data);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn rm_peer() {
        let initial_data =
            vec![("bonding-data:1", bond_entry_1()), ("bonding-data:2", bond_entry_2())];
        let mut stash = setup_stash("rm_peer", initial_data).await;

        // OK to remove some unknown peer...
        assert!(stash.rm_peer(PeerId(0)).await.is_ok());

        // ...or known peer.
        assert!(stash.rm_peer(PeerId(1)).await.is_ok());

        let local = stash
            .bonding_data
            .get(&Address::Public([1, 0, 0, 0, 0, 0]))
            .expect("could not find local address entries");
        assert_eq!(1, local.len());
        assert!(local.get(&PeerId(1)).is_none());
        let bond: &BondingData = &*(local.get(&PeerId(2)).expect("could not find device"));
        assert_eq!(&bond_data_2(), bond);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn store_host_data() {
        let host_address = Address::Public([1, 0, 0, 0, 0, 0]);
        let mut stash = setup_stash("store_host_data", vec![]).await;
        let accessor = stash.proxy.clone();

        assert!(stash.store_host_data(&host_address, host_data_1()).await.is_ok());

        // Make sure the in-memory cache has been updated.
        assert_eq!(Some(&host_data_1()), stash.host_data.get(&host_address));
        assert_eq!(1, stash.host_data.len());

        // The new data should be accessible over FIDL.
        let host_text = accessor.get_value("host-data:00:00:00:00:00:01").await;
        let host_text = host_text.expect("failed to get value").map(|x| *x);
        assert_eq!(host_text, Some(host_text_1()));

        // It should be possible to overwrite the IRK.
        assert!(stash.store_host_data(&host_address, host_data_2()).await.is_ok());

        // Make sure the in-memory cache has been updated.
        assert_eq!(Some(&host_data_2()), stash.host_data.get(&host_address));
        assert_eq!(1, stash.host_data.len());

        // The new data should be accessible over FIDL.
        let host_text = accessor.get_value("host-data:00:00:00:00:00:01").await;
        let host_text = host_text.expect("failed to get value").map(|x| *x);
        assert_eq!(host_text, Some(host_text_2()));
    }

    async fn setup_stash(name: &'static str, entries: Vec<(&'static str, Value)>) -> StashInner {
        let inspect = fuchsia_inspect::Inspector::new().root().create_child(TEST_INSPECT_ROOT);
        let accessor = create_stash_accessor(name).await.expect("failed to create StashAccessor");

        // Insert intial bonding data values into stash
        for (id, mut entry) in entries {
            accessor.set_value(id, &mut entry).expect("failed to set value");
        }
        accessor
            .flush()
            .await
            .expect("failed to flush a bonding data value")
            .expect("failed to flush a bonding data value");
        StashInner::new(accessor, inspect).await.expect("stash failed to initialize")
    }

    fn set_of<I>(elems: I) -> HashSet<I::Item>
    where
        I: IntoIterator,
        I::Item: Eq + Hash,
    {
        elems.into_iter().collect()
    }

    async fn run_with_stash<F, T, Fut>(inner: StashInner, f: F) -> Result<T, Error>
    where
        F: FnOnce(Stash) -> Fut,
        Fut: Future<Output = Result<T, Error>>,
    {
        let (stash, run_stash) = build_stash(inner);
        let run_fn = f(stash);

        pin_mut!(run_stash);
        pin_mut!(run_fn);
        select! {
            result = run_fn.fuse() => result,
            run = run_stash.fuse() => match run {
                Ok(_) => return Err(format_err!("Stash receiver stopped unexpectedly")),
                Err(e) => Err(e)
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn request_list_bonds() -> Result<(), Error> {
        let initial_data =
            vec![("bonding-data:1", bond_entry_1()), ("bonding-data:2", bond_entry_2())];
        let stash = setup_stash("request_list_bonds", initial_data).await;

        run_with_stash(stash, move |mut s: Stash| {
            async move {
                // Should return None for unknown address.
                let bonds = s.list_bonds(Address::Public([0, 0, 0, 0, 0, 0])).await?;
                assert_eq!(bonds, None);

                // Should return expected elements for known address
                let bonds = s.list_bonds(Address::Public([1, 0, 0, 0, 0, 0])).await?;
                let ids =
                    bonds.expect("expected to find address").iter().map(|b| b.identifier).collect();
                assert_eq!(set_of(vec![PeerId(1), PeerId(2)]), ids);
                Ok(())
            }
        })
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn request_store_bonds() -> Result<(), Error> {
        let stash = setup_stash("request_store_bonds", vec![]).await;
        let accessor = stash.proxy.clone();

        run_with_stash(stash, move |mut s: Stash| {
            async move {
                s.store_bond(bond_data_1()).await?;

                // The new data should be accessible over FIDL.
                let result = accessor.get_value("bonding-data:0000000000000001").await;
                let bond_data = result.expect("failed to get value").map(|x| *x);
                assert_eq!(bond_data, Some(bond_entry_1()));
                Ok(())
            }
        })
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn request_rm_peer() -> Result<(), Error> {
        let initial_data =
            vec![("bonding-data:1", bond_entry_1()), ("bonding-data:2", bond_entry_2())];
        let stash = setup_stash("request_rm_peer", initial_data).await;

        run_with_stash(stash, move |mut s: Stash| {
            async move {
                // OK to remove some unknown peer...
                s.rm_peer(PeerId(0)).await?;

                // ...or known peer.
                s.rm_peer(PeerId(1)).await?;

                // Should return only non-removed element for known address
                let bonds = s.list_bonds(bond_data_2().local_address).await?;
                let bonds = bonds.expect("expected to find address");
                assert_eq!(bonds, vec![bond_data_2()]);
                Ok(())
            }
        })
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn request_get_host_data() -> Result<(), Error> {
        let initial_data = vec![
            ("host-data:00:00:00:00:00:01", host_text_1()),
            ("host-data:00:00:00:00:00:02", host_text_2()),
        ];
        let stash = setup_stash("request_get_host_data", initial_data).await;
        run_with_stash(stash, move |mut s: Stash| {
            async move {
                // Should return None for unknown identity address.
                assert!(s.get_host_data(Address::Public([0, 0, 0, 0, 0, 0])).await?.is_none());

                let host_data = s.get_host_data(Address::Public([1, 0, 0, 0, 0, 0])).await?;
                assert_eq!(Some(host_data_1()), host_data);

                let host_data = s.get_host_data(Address::Public([2, 0, 0, 0, 0, 0])).await?;
                assert_eq!(Some(host_data_2()), host_data);
                Ok(())
            }
        })
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn request_store_host_data() -> Result<(), Error> {
        let stash = setup_stash("request_store_host_data", vec![]).await;
        let accessor = stash.proxy.clone();
        run_with_stash(stash, move |mut s: Stash| {
            async move {
                let host_address = Address::Public([1, 0, 0, 0, 0, 0]);

                let host_data = host_data_1();
                assert!(s.store_host_data(host_address, host_data).await.is_ok());

                // Make sure the in-memory cache has been updated.
                let host_data = s.get_host_data(host_address).await?;
                assert_eq!(Some(host_data_1()), host_data);

                // The new data should be accessible over FIDL.
                let host_text = accessor.get_value("host-data:00:00:00:00:00:01").await;
                let host_text = host_text.expect("failed to get value").map(|x| *x);
                assert_eq!(host_text, Some(host_text_1()));

                // It should be possible to overwrite the IRK.
                let host_data = host_data_2();
                assert!(s.store_host_data(host_address, host_data).await.is_ok());

                // Make sure the in-memory cache has been updated.
                let host_data = s.get_host_data(host_address).await?;
                assert_eq!(Some(host_data_2()), host_data);

                // The new data should be accessible over FIDL.
                let host_text = accessor.get_value("host-data:00:00:00:00:00:01").await;
                let host_text = host_text.expect("failed to get value").map(|x| *x);
                assert_eq!(host_text, Some(host_text_2()));
                Ok(())
            }
        })
        .await
    }
}
