// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use anyhow::Error;
use async_utils::hanging_get::client::HangingGetStream;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_lowpan::Identity;
use fidl_fuchsia_lowpan_device::{
    CountersMarker, DeviceChanges, DeviceExtraMarker, DeviceMarker, DeviceState, LookupMarker,
    LookupProxyInterface, Protocols,
};
use fidl_fuchsia_lowpan_test::DeviceTestMarker;
use fuchsia_async::Task;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_inspect::{LazyNode, Node, StringProperty};
use fuchsia_inspect_contrib::inspect_log;
use fuchsia_inspect_contrib::nodes::{BoundedListNode, NodeExt, TimeProperty};
use parking_lot::Mutex;
use std::collections::{HashMap, HashSet, VecDeque};
use std::sync::Arc;

type IfaceId = String;

// Limit was chosen arbitrary.
const EVENTS_LIMIT: usize = 20;
const DEAD_IFACE_LIMIT: usize = 20;

pub struct LowpanServiceTree {
    // Root of the tree
    inspector: Inspector,

    // "events" subtree
    events: Mutex<BoundedListNode>,

    // "iface-<n>" subtrees, where n is the iface ID.
    ifaces_trees: Mutex<HashMap<IfaceId, Arc<IfaceTreeHolder>>>,

    // Iface devices that have been removed but whose debug infos are still kept in Inspect tree.
    // Number of dead Iface devices are bounded by |DEAD_IFACE_LIMIT|.
    dead_ifaces: Mutex<VecDeque<Arc<IfaceTreeHolder>>>,
}

impl LowpanServiceTree {
    pub fn new(inspector: Inspector) -> Self {
        let events = inspector.root().create_child("events");
        Self {
            inspector,
            events: Mutex::new(BoundedListNode::new(events, EVENTS_LIMIT)),
            ifaces_trees: Mutex::new(HashMap::new()),
            dead_ifaces: Mutex::new(VecDeque::new()),
        }
    }

    pub fn create_iface_child(&self, iface_id: IfaceId) -> Arc<IfaceTreeHolder> {
        let child = self.inspector.root().create_child(&format!("iface-{}", iface_id));
        let holder = Arc::new(IfaceTreeHolder::new(child));
        self.ifaces_trees.lock().insert(iface_id, holder.clone());
        holder
    }

    pub fn notify_iface_removed(&self, iface_id: IfaceId) {
        let mut iface_tree_list = self.ifaces_trees.lock();
        let mut dead_iface_list = self.dead_ifaces.lock();
        if let Some(child_holder) = iface_tree_list.remove(&iface_id) {
            inspect_log!(child_holder.events.lock(), msg: "Removed");
            dead_iface_list.push_back(child_holder);
            if dead_iface_list.len() > DEAD_IFACE_LIMIT {
                dead_iface_list.pop_front();
            }
        }
    }
}

pub struct IfaceTreeHolder {
    node: Node,
    // "iface-*/events" subtree
    events: Mutex<BoundedListNode>,
    // "iface-*/status" subtree
    status: Mutex<IfaceStatusNode>,
    // "iface-*/counters" subtree
    counters: Mutex<LazyNode>,
    // "iface-*/neighbors" subtree
    neighbors: Mutex<LazyNode>,
}

impl IfaceTreeHolder {
    pub fn new(node: Node) -> Self {
        let events = node.create_child("events");
        let status = node.create_child("status");
        Self {
            node,
            events: Mutex::new(BoundedListNode::new(events, EVENTS_LIMIT)),
            status: Mutex::new(IfaceStatusNode::new(status)),
            counters: Mutex::new(LazyNode::default()),
            neighbors: Mutex::new(LazyNode::default()),
        }
    }

    pub fn update_status(&self, new_state: DeviceState) {
        let new_connectivity_state = match new_state.connectivity_state {
            Some(state) => format!("{:?}", state),
            None => String::from(""),
        };

        let online_states: HashSet<String> =
            vec![String::from("Attaching"), String::from("Attached"), String::from("Isolated")]
                .into_iter()
                .collect();
        let mut status = self.status.lock();
        let mut status_change_messages = Vec::new();
        if online_states.contains(&new_connectivity_state)
            && !online_states.contains(&status.connectivity_state_value)
        {
            status._online_since = Some(status.node.create_time("online_since"));
        } else if !online_states.contains(&new_connectivity_state) {
            status._online_since = None;
        }
        if new_connectivity_state != status.connectivity_state_value {
            status_change_messages.push(format!(
                "connectivity_state:{}->{}",
                status.connectivity_state_value, new_connectivity_state
            ));
            status.connectivity_state_value = new_connectivity_state.clone();
            status._connectivity_state =
                status.node.create_string("connectivity_state", new_connectivity_state);
        }

        let new_role = match new_state.role {
            Some(role) => format!("{:?}", role),
            None => String::from(""),
        };
        if new_role != status.role_value {
            status_change_messages.push(format!("role:{}->{}\n", status.role_value, new_role));
            status.role_value = new_role.clone();
            status._role = status.node.create_string("role", new_role);
        }
        let joined_status_messages = status_change_messages.join(";");
        if !joined_status_messages.is_empty() {
            inspect_log!(self.events.lock(), msg: joined_status_messages);
        }
    }

    pub fn update_identity(&self, new_identity: Identity) {
        let mut status = self.status.lock();
        let new_net_type = new_identity.net_type.unwrap_or("".to_string());
        let mut status_change_messages = Vec::new();
        if new_net_type != status.net_type_value {
            status_change_messages
                .push(format!("net_type:{}->{}", status.net_type_value, new_net_type));
            status.net_type_value = new_net_type.clone();
            status._net_type = status.node.create_string("net_type", new_net_type);
        }

        let new_channel = match new_identity.channel {
            None => String::new(),
            Some(channel) => channel.to_string(),
        };
        if new_channel != status.channel_value {
            status_change_messages
                .push(format!("channel:{}->{}", status.channel_value, new_channel));

            status.channel_value = new_channel.clone();
            status._channel = status.node.create_string("channel", new_channel);
        }
        let joined_status_messages = status_change_messages.join(";");
        if !joined_status_messages.is_empty() {
            inspect_log!(self.events.lock(), msg: joined_status_messages);
        }
    }
}

pub struct IfaceStatusNode {
    // Iface state values.
    connectivity_state_value: String,
    role_value: String,
    channel_value: String,
    net_type_value: String,

    node: Node,
    // Properties of "iface-*/status" node.
    _connectivity_state: StringProperty,
    _online_since: Option<TimeProperty>,
    _role: StringProperty,
    _channel: StringProperty,
    _net_type: StringProperty,
}
impl IfaceStatusNode {
    pub fn new(node: Node) -> Self {
        Self {
            connectivity_state_value: String::from(""),
            role_value: String::from(""),
            channel_value: String::from(""),
            net_type_value: String::from(""),
            node,
            _connectivity_state: StringProperty::default(),
            _online_since: None,
            _role: StringProperty::default(),
            _channel: StringProperty::default(),
            _net_type: StringProperty::default(),
        }
    }
}

pub async fn watch_device_changes<
    LP: 'static
        + LookupProxyInterface<
            WatchDevicesResponseFut = fidl::client::QueryResponseFut<DeviceChanges>,
        >,
>(
    inspect_tree: Arc<LowpanServiceTree>,
    lookup: Arc<LP>,
) {
    let mut device_table: HashMap<String, Arc<Task<()>>> = HashMap::new();
    let lookup_clone = lookup.clone();
    let mut lookup_stream = HangingGetStream::new(lookup_clone, |lookup| lookup.watch_devices());
    loop {
        match lookup_stream.next().await {
            None => {
                fx_log_debug!("LoWPAN device lookup stream finished");
                break;
            }
            Some(Ok(devices)) => {
                for available_device in devices.added.iter() {
                    inspect_log!(
                        inspect_tree.events.lock(),
                        msg: format!("{}:available", available_device)
                    );
                    let future = monitor_device(
                        lookup.clone(),
                        available_device.clone().to_string(),
                        inspect_tree.create_iface_child(available_device.clone()),
                    )
                    .map(|x| {
                        if x.is_err() {
                            fx_log_warn!("Failed to monitor LoWPAN device.");
                        }
                    });

                    device_table
                        .insert(available_device.to_string(), Arc::new(Task::spawn(future)));
                }
                for unavailable_device in devices.removed.iter() {
                    inspect_log!(
                        inspect_tree.events.lock(),
                        msg: format!("{}:unavailable", unavailable_device)
                    );
                    device_table.remove(unavailable_device);
                    inspect_tree.notify_iface_removed(unavailable_device.to_string());
                }
            }
            Some(Err(e)) => {
                fx_log_warn!("LoWPAN device lookup stream returned err: {} ", e);
                break;
            }
        }
    }
}

pub async fn start_inspect_process(inspect_tree: Arc<LowpanServiceTree>) -> Result<(), Error> {
    let lookup = connect_to_protocol::<LookupMarker>()?;
    watch_device_changes(inspect_tree, Arc::new(lookup)).await;
    Ok::<(), Error>(())
}

async fn monitor_device<LP: 'static + LookupProxyInterface>(
    lookup: Arc<LP>,
    name: String,
    iface_tree: Arc<IfaceTreeHolder>,
) -> Result<(), Error> {
    let (device_client, device_server) = create_endpoints::<DeviceMarker>()?;
    let (device_extra_client, device_extra_server) = create_endpoints::<DeviceExtraMarker>()?;
    let (device_test_client, device_test_server) = create_endpoints::<DeviceTestMarker>()?;
    let (counters_client, counters_server) = create_endpoints::<CountersMarker>()?;
    lookup
        .lookup_device(
            &name,
            Protocols {
                device: Some(device_server),
                device_extra: Some(device_extra_server),
                device_test: Some(device_test_server),
                device_route: None,
                device_route_extra: None,
                counters: Some(counters_server),
                ..Protocols::EMPTY
            },
        )
        .map(|x| match x {
            Ok(Ok(())) => Ok(()),
            Ok(Err(x)) => Err(format_err!("Service Error: {:?}", x)),
            Err(x) => Err(x.into()),
        })
        .await
        .context(format!("Unable to find {:?}", name))?;
    let device = device_client.into_proxy().context("into_proxy() failed")?;
    let device_extra = device_extra_client.into_proxy().context("into_proxy() failed")?;
    let device_test = device_test_client.into_proxy().context("into_proxy() failed")?;
    let counters = counters_client.into_proxy().context("into_proxy() failed")?;

    {
        // "iface-*/counters" node
        let mut lazy_counters = iface_tree.counters.lock();
        *lazy_counters = iface_tree.node.create_lazy_child("counters", move || {
            let counters_clone = counters.clone();
            async move {
                let inspector = Inspector::new();
                match counters_clone.get().await {
                    Ok(all_counters) => {
                        let mac_counters =
                            [("tx", all_counters.mac_tx), ("rx", all_counters.mac_rx)];

                        for (mac_counter_for_str, mac_counter_option) in mac_counters {
                            if let Some(mac_counter) = mac_counter_option {
                                inspector.root().record_int(
                                    format!("{}_frames", mac_counter_for_str),
                                    mac_counter.total.unwrap_or(0).into(),
                                );
                                inspector.root().record_int(
                                    format!("{}_unicast", mac_counter_for_str),
                                    mac_counter.unicast.unwrap_or(0).into(),
                                );
                                inspector.root().record_int(
                                    format!("{}_broadcast", mac_counter_for_str),
                                    mac_counter.broadcast.unwrap_or(0).into(),
                                );
                                inspector.root().record_int(
                                    format!("{}_ack_requested", mac_counter_for_str),
                                    mac_counter.ack_requested.unwrap_or(0).into(),
                                );
                                inspector.root().record_int(
                                    format!("{}_acked", mac_counter_for_str),
                                    mac_counter.acked.unwrap_or(0).into(),
                                );
                                inspector.root().record_int(
                                    format!("{}_no_ack_requested", mac_counter_for_str),
                                    mac_counter.no_ack_requested.unwrap_or(0).into(),
                                );
                                inspector.root().record_int(
                                    format!("{}_data", mac_counter_for_str),
                                    mac_counter.data.unwrap_or(0).into(),
                                );
                                inspector.root().record_int(
                                    format!("{}_data_poll", mac_counter_for_str),
                                    mac_counter.data_poll.unwrap_or(0).into(),
                                );
                                inspector.root().record_int(
                                    format!("{}_beacon", mac_counter_for_str),
                                    mac_counter.beacon.unwrap_or(0).into(),
                                );
                                inspector.root().record_int(
                                    format!("{}_beacon_request", mac_counter_for_str),
                                    mac_counter.beacon_request.unwrap_or(0).into(),
                                );
                                inspector.root().record_int(
                                    format!("{}_other", mac_counter_for_str),
                                    mac_counter.other.unwrap_or(0).into(),
                                );
                                inspector.root().record_int(
                                    format!("{}_address_filtered", mac_counter_for_str),
                                    mac_counter.address_filtered.unwrap_or(0).into(),
                                );

                                if mac_counter_for_str == "tx" {
                                    inspector.root().record_int(
                                        format!("{}_retries", mac_counter_for_str),
                                        mac_counter.retries.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        format!("{}_direct_max_retry_expiry", mac_counter_for_str),
                                        mac_counter.direct_max_retry_expiry.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        format!(
                                            "{}_indirect_max_retry_expiry",
                                            mac_counter_for_str
                                        ),
                                        mac_counter.indirect_max_retry_expiry.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        format!("{}_err_cca", mac_counter_for_str),
                                        mac_counter.err_cca.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        format!("{}_err_abort", mac_counter_for_str),
                                        mac_counter.err_abort.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        format!("{}_err_busy_channel", mac_counter_for_str),
                                        mac_counter.err_busy_channel.unwrap_or(0).into(),
                                    );
                                } else {
                                    inspector.root().record_int(
                                        format!("{}_dest_addr_filtered", mac_counter_for_str),
                                        mac_counter.dest_addr_filtered.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        format!("{}_duplicated", mac_counter_for_str),
                                        mac_counter.duplicated.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        format!("{}_err_no_frame", mac_counter_for_str),
                                        mac_counter.err_no_frame.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        format!("{}_err_unknown_neighbor", mac_counter_for_str),
                                        mac_counter.err_unknown_neighbor.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        format!("{}_err_invalid_src_addr", mac_counter_for_str),
                                        mac_counter.err_invalid_src_addr.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        format!("{}_err_sec", mac_counter_for_str),
                                        mac_counter.err_sec.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        format!("{}_err_fcs", mac_counter_for_str),
                                        mac_counter.err_fcs.unwrap_or(0).into(),
                                    );
                                }

                                inspector.root().record_int(
                                    format!("{}_err_other", mac_counter_for_str),
                                    mac_counter.err_other.unwrap_or(0).into(),
                                );
                            }
                        }

                        // Log coex counters
                        let coex_counters =
                            [("tx", all_counters.coex_tx), ("rx", all_counters.coex_rx)];
                        inspector.root().record_child("coex_counters", |coex_counters_child| {
                            for (coex_counter_for_str, coex_counter_option) in coex_counters {
                                if let Some(coex_counter) = coex_counter_option {
                                    if let Some(val) = coex_counter.requests {
                                        coex_counters_child.record_uint(
                                            format!("{}_requests", coex_counter_for_str),
                                            val.into(),
                                        );
                                    }
                                    if let Some(val) = coex_counter.grant_immediate {
                                        coex_counters_child.record_uint(
                                            format!("{}_grant_immediate", coex_counter_for_str),
                                            val.into(),
                                        );
                                    }
                                    if let Some(val) = coex_counter.grant_wait {
                                        coex_counters_child.record_uint(
                                            format!("{}_grant_wait", coex_counter_for_str),
                                            val.into(),
                                        );
                                    }
                                    if let Some(val) = coex_counter.grant_wait_activated {
                                        coex_counters_child.record_uint(
                                            format!(
                                                "{}_grant_wait_activated",
                                                coex_counter_for_str
                                            ),
                                            val.into(),
                                        );
                                    }
                                    if let Some(val) = coex_counter.grant_wait_timeout {
                                        coex_counters_child.record_uint(
                                            format!("{}_grant_wait_timeout", coex_counter_for_str),
                                            val.into(),
                                        );
                                    }
                                    if let Some(val) = coex_counter.grant_deactivated_during_request
                                    {
                                        coex_counters_child.record_uint(
                                            format!(
                                                "{}_grant_deactivated_during_request",
                                                coex_counter_for_str
                                            ),
                                            val.into(),
                                        );
                                    }
                                    if let Some(val) = coex_counter.delayed_grant {
                                        coex_counters_child.record_uint(
                                            format!("{}_delayed_grant", coex_counter_for_str),
                                            val.into(),
                                        );
                                    }
                                    if let Some(val) = coex_counter.avg_delay_request_to_grant_usec
                                    {
                                        coex_counters_child.record_uint(
                                            format!(
                                                "{}_avg_delay_request_to_grant_usec",
                                                coex_counter_for_str
                                            ),
                                            val.into(),
                                        );
                                    }
                                    if let Some(val) = coex_counter.grant_none {
                                        coex_counters_child.record_uint(
                                            format!("{}_grant_none", coex_counter_for_str),
                                            val.into(),
                                        );
                                    }
                                }
                            }
                        });
                        if let Some(val) = all_counters.coex_saturated {
                            inspector.root().record_bool("coex_saturated", val.into());
                        }
                    }
                    Err(e) => {
                        fx_log_warn!("Error in logging counters. Error: {}", e);
                    }
                };
                Ok(inspector)
            }
            .boxed()
        });

        // "iface-*/neighbors" node
        let mut lazy_neighbors = iface_tree.neighbors.lock();
        *lazy_neighbors = iface_tree.node.create_lazy_child("neighbors", move || {
            let device_test_clone = device_test.clone();
            async move {
                let inspector = Inspector::new();
                match device_test_clone.get_neighbor_table().await {
                    Ok(neighbor_table) => {
                        let mut index = -1;
                        for neighbor_info in neighbor_table {
                            let neighbor_info_c = neighbor_info.clone();
                            index += 1;
                            inspector.root().record_lazy_child(format!("{}", index), move || {
                                let neighbor_info_clone = neighbor_info_c.clone();
                                async move {
                                    let inspector = Inspector::new();
                                    inspector.root().record_int(
                                        "short_address",
                                        neighbor_info_clone.short_address.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_string(
                                        "age",
                                        neighbor_info_clone.age.unwrap_or(0).to_string(),
                                    );
                                    inspector.root().record_bool(
                                        "is_child",
                                        neighbor_info_clone.is_child.unwrap_or(false).into(),
                                    );
                                    inspector.root().record_int(
                                        "link_frame_count",
                                        neighbor_info_clone.link_frame_count.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        "mgmt_frame_count",
                                        neighbor_info_clone.mgmt_frame_count.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        "rssi",
                                        neighbor_info_clone.last_rssi_in.unwrap_or(0).into(),
                                    );
                                    inspector.root().record_int(
                                        "thread_mode",
                                        neighbor_info_clone.thread_mode.unwrap_or(0).into(),
                                    );
                                    Ok(inspector)
                                }
                                .boxed()
                            });
                        }
                    }
                    Err(e) => {
                        fx_log_warn!("Error in logging neighbors. Error: {}", e);
                    }
                };
                Ok(inspector)
            }
            .boxed()
        });
    }

    let mut device_stream_handler =
        HangingGetStream::new(device, |device| device.watch_device_state())
            .map_ok(|state| {
                iface_tree.update_status(state);
            })
            .try_collect::<()>();
    let mut device_extra_stream_handler =
        HangingGetStream::new(device_extra, |device| device.watch_identity())
            .map_ok(|identity| {
                iface_tree.update_identity(identity);
            })
            .try_collect::<()>();

    (futures::select! {
        ret = device_stream_handler => ret,
        ret = device_extra_stream_handler => ret,
    })?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use fuchsia_async::Time;
    use fuchsia_async::TimeoutExt;
    use fuchsia_component::client::launch;
    use fuchsia_component::client::launcher;
    use fuchsia_inspect::assert_data_tree;
    use fuchsia_inspect::testing::AnyProperty;

    #[fasync::run(4, test)]
    async fn test_watch_device_changes() {
        let lookup = connect_to_protocol::<LookupMarker>().unwrap();

        let inspector = fuchsia_inspect::Inspector::new();
        let inspector_clone = inspector.clone();
        let inspect_tree = Arc::new(LowpanServiceTree::new(inspector_clone));
        let look_up = Arc::new(lookup);

        assert_data_tree!(inspector, root: {
            "events":{},
        });

        let launcher = launcher().unwrap();
        let driver_url =
            "fuchsia-pkg://fuchsia.com/lowpan-dummy-driver#meta/lowpan-dummy-driver.cmx";
        let _driver =
            launch(&launcher, driver_url.to_string(), None).context("launch dummy driver").unwrap();

        let _res = watch_device_changes(inspect_tree.clone(), look_up.clone())
            .on_timeout(Time::after(fuchsia_zircon::Duration::from_seconds(5)), || {
                fx_log_info!("test_watch_device_changes: watch_device_changes timed out");
            })
            .await;

        assert_data_tree!(inspector, root: {
            "events":{
                "0":{
                    "@time": AnyProperty,
                    "msg": "lowpan0:available"
                }
            },
            "iface-lowpan0":{
                "events": {
                    "0": {
                        "@time": AnyProperty,
                        "msg": "connectivity_state:->Ready"
                    }
                },
                "status": {
                    "connectivity_state": "Ready",
                },
                "counters": contains {
                    "rx_ack_requested": AnyProperty,
                    "rx_acked": AnyProperty,
                    "rx_address_filtered": AnyProperty,
                    "rx_beacon": AnyProperty,
                    "rx_beacon_request": AnyProperty,
                    "rx_broadcast": AnyProperty,
                    "rx_data": AnyProperty,
                    "rx_data_poll": AnyProperty,
                    "rx_dest_addr_filtered": AnyProperty,
                    "rx_duplicated": AnyProperty,
                    "rx_err_fcs": AnyProperty,
                    "rx_err_invalid_src_addr": AnyProperty,
                    "rx_err_no_frame": AnyProperty,
                    "rx_err_other": AnyProperty,
                    "rx_err_sec": AnyProperty,
                    "rx_err_unknown_neighbor": AnyProperty,
                    "rx_frames": AnyProperty,
                    "rx_no_ack_requested": AnyProperty,
                    "rx_other": AnyProperty,
                    "rx_unicast": AnyProperty,
                    "tx_ack_requested": AnyProperty,
                    "tx_acked": AnyProperty,
                    "tx_address_filtered": AnyProperty,
                    "tx_beacon": AnyProperty,
                    "tx_beacon_request": AnyProperty,
                    "tx_broadcast": AnyProperty,
                    "tx_data": AnyProperty,
                    "tx_data_poll": AnyProperty,
                    "tx_direct_max_retry_expiry": AnyProperty,
                    "tx_err_abort": AnyProperty,
                    "tx_err_busy_channel": AnyProperty,
                    "tx_err_cca": AnyProperty,
                    "tx_err_other": AnyProperty,
                    "tx_frames": AnyProperty,
                    "tx_indirect_max_retry_expiry": AnyProperty,
                    "tx_no_ack_requested": AnyProperty,
                    "tx_other": AnyProperty,
                    "tx_retries": AnyProperty,
                    "tx_unicast": AnyProperty,
                },
                "neighbors": {
                    "0": {
                        "age": AnyProperty,
                        "is_child": AnyProperty,
                        "link_frame_count": AnyProperty,
                        "mgmt_frame_count": AnyProperty,
                        "rssi": AnyProperty,
                        "short_address": AnyProperty,
                        "thread_mode": AnyProperty,
                    }
                }
            }
        });
        assert_data_tree!(inspector, root: contains {
            "iface-lowpan0": contains {
                "counters": contains {
                    "coex_counters": {
                        "tx_requests": AnyProperty,
                        "tx_grant_immediate": AnyProperty,
                        "tx_grant_wait": AnyProperty,
                        "tx_grant_wait_activated": AnyProperty,
                        "tx_grant_wait_timeout": AnyProperty,
                        "tx_grant_deactivated_during_request": AnyProperty,
                        "tx_delayed_grant": AnyProperty,
                        "tx_avg_delay_request_to_grant_usec": AnyProperty,
                        "rx_requests": AnyProperty,
                        "rx_grant_immediate": AnyProperty,
                        "rx_grant_wait": AnyProperty,
                        "rx_grant_wait_activated": AnyProperty,
                        "rx_grant_wait_timeout": AnyProperty,
                        "rx_grant_deactivated_during_request": AnyProperty,
                        "rx_delayed_grant": AnyProperty,
                        "rx_avg_delay_request_to_grant_usec": AnyProperty,
                        "rx_grant_none": AnyProperty,
                    },
                    "coex_saturated": false,
                }
            }
        });
    }
}
