// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::{bss::BssInfo, Status as SmeStatus},
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    fuchsia_inspect::{
        BoolProperty, HistogramProperty, IntLinearHistogramProperty, IntProperty,
        LinearHistogramParams, Node, Property, StringProperty, UintProperty,
    },
    fuchsia_inspect_contrib::nodes::{BoundedListNode, NodeExt, TimeProperty},
    fuchsia_zircon as zx,
    mundane::{
        hash::{Digest, Sha256},
        hmac::hmac,
    },
    parking_lot::Mutex,
    wlan_common::{
        format::MacFmt,
        ie::{self, wsc},
        mac::MacAddr,
    },
    wlan_inspect::iface_mgr::IfaceTree,
};

/// These limits are set to capture roughly 5 to 10 recent connection attempts. An average
/// successful connection attempt would generate about 5 state events and 7 supplicant events (this
/// number may be different in error cases).
const STATE_EVENTS_LIMIT: usize = 50;
const RSN_EVENTS_LIMIT: usize = 50;

/// Limit set to capture roughly join scans for 10 recent connection attempts.
const JOIN_SCAN_EVENTS_LIMIT: usize = 10;

/// Wrapper struct SME inspection nodes
pub struct SmeTree {
    /// Inspection node to log recent state transitions, or cases where an event would that would
    /// normally cause a state transition doesn't due to an error.
    pub state_events: Mutex<BoundedListNode>,
    /// Inspection node to log EAPOL frames processed by supplicant and its output.
    pub rsn_events: Mutex<BoundedListNode>,
    /// Inspection node to log recent join scan results.
    pub join_scan_events: Mutex<BoundedListNode>,
    /// Inspect node to log periodic pulse check. For the most part, information logged in this
    /// node can be derived from (and is therefore redundant with) `state_events` node. This
    /// is logged  for two reasons:
    /// 1. To show a quick summary of latest status.
    /// 2. To show how up-to-date the latest status is (although pulse is logged within SME, it can
    ///    be thought similarly to an external entity periodically checking SME's status).
    pub last_pulse: Mutex<PulseNode>,
    /// Inspection node to log samples from SignalReport indication
    pub signal_samples: Mutex<SignalSamplesNode>,

    /// Hasher used to hash sensitive information, preserving user privacy.
    pub hasher: InspectHasher,
}

impl SmeTree {
    pub fn new(node: &Node, hash_key: [u8; 8]) -> Self {
        let state_events =
            BoundedListNode::new(node.create_child("state_events"), STATE_EVENTS_LIMIT);
        let rsn_events = BoundedListNode::new(node.create_child("rsn_events"), RSN_EVENTS_LIMIT);
        let join_scan_events =
            BoundedListNode::new(node.create_child("join_scan_events"), JOIN_SCAN_EVENTS_LIMIT);
        let pulse = PulseNode::new(node.create_child("last_pulse"));
        let signal_samples = SignalSamplesNode::new(node.create_child("signal_samples"));
        Self {
            state_events: Mutex::new(state_events),
            rsn_events: Mutex::new(rsn_events),
            join_scan_events: Mutex::new(join_scan_events),
            last_pulse: Mutex::new(pulse),
            signal_samples: Mutex::new(signal_samples),
            hasher: InspectHasher { hash_key },
        }
    }

    pub fn update_pulse(&self, new_status: SmeStatus) {
        self.last_pulse.lock().update(new_status, &self.hasher)
    }

    pub fn signal_ind(&self, ind: &fidl_mlme::SignalReportIndication) {
        self.signal_samples.lock().update(ind)
    }
}

/// Hasher used to hash sensitive information, preserving user privacy.
pub struct InspectHasher {
    hash_key: [u8; 8],
}

impl InspectHasher {
    pub fn hash(&self, bytes: &[u8]) -> String {
        hex::encode(hmac::<Sha256>(&self.hash_key, bytes).bytes())
    }

    pub fn hash_mac_addr(&self, addr: MacAddr) -> String {
        addr.to_mac_str_partial_hashed(|bytes| {
            hex::encode(hmac::<Sha256>(&self.hash_key, bytes).bytes())
        })
    }
}

impl IfaceTree for SmeTree {}

pub struct PulseNode {
    node: Node,
    _started: TimeProperty,
    last_updated: TimeProperty,
    last_link_up: Option<TimeProperty>,
    status: Option<StatusNode>,

    // Not part of Inspect node. We use it to compare new status against existing status
    last_status: Option<SmeStatus>,
}

impl PulseNode {
    fn new(node: Node) -> Self {
        let now = zx::Time::get(zx::ClockId::Monotonic);
        let started = node.create_time_at("started", now);
        let last_updated = node.create_time_at("last_updated", now);
        Self {
            node,
            _started: started,
            last_updated,
            last_link_up: None,
            status: None,
            last_status: None,
        }
    }

    pub fn update(&mut self, new_status: SmeStatus, hasher: &InspectHasher) {
        let now = zx::Time::get(zx::ClockId::Monotonic);
        self.last_updated.set_at(now);

        // This method is always called when there's a state transition, so even if the client is
        // no longer connected now, if the client was previously connected, we can conclude
        // that they were connected until now.
        let previously_connected =
            self.last_status.as_ref().map(|s| s.connected_to.is_some()).unwrap_or(false);
        if new_status.connected_to.is_some() || previously_connected {
            match &self.last_link_up {
                Some(last_link_up) => last_link_up.set_at(now),
                None => self.last_link_up = Some(self.node.create_time_at("last_link_up", now)),
            }
        }

        let old_status = self.last_status.replace(new_status);
        if old_status != self.last_status {
            // Safe to unwrap because value was inserted two lines above
            let new_status = self.last_status.as_ref().unwrap();
            match self.status.as_mut() {
                Some(status_node) => status_node.update(new_status, hasher),
                None => {
                    self.status =
                        Some(StatusNode::new(self.node.create_child("status"), new_status, hasher))
                }
            }
        }
    }
}

pub struct StatusNode {
    node: Node,
    status_str: StringProperty,
    connected_to: Option<BssInfoNode>,
    connecting_to: Option<ConnectingToNode>,
}

impl StatusNode {
    fn new(node: Node, status: &SmeStatus, hasher: &InspectHasher) -> Self {
        let status_str = node.create_string("status_str", "idle");
        let mut status_node = Self { node, status_str, connected_to: None, connecting_to: None };
        status_node.update(status, hasher);
        status_node
    }

    pub fn update(&mut self, new_status: &SmeStatus, hasher: &InspectHasher) {
        let status_str = if new_status.connected_to.is_some() {
            "connected"
        } else if new_status.connecting_to.is_some() {
            "connecting"
        } else {
            "idle"
        };
        self.status_str.set(status_str);

        match &new_status.connected_to {
            Some(bss_info) => match self.connected_to.as_mut() {
                Some(connected_to) => connected_to.update(bss_info, hasher),
                None => {
                    self.connected_to = Some(BssInfoNode::new(
                        self.node.create_child("connected_to"),
                        bss_info,
                        hasher,
                    ));
                }
            },
            None => {
                self.connected_to.take();
            }
        }
        match &new_status.connecting_to {
            Some(ssid) => match self.connecting_to.as_mut() {
                Some(connecting_to) => connecting_to.update(&ssid[..], hasher),
                None => {
                    self.connecting_to = Some(ConnectingToNode::new(
                        self.node.create_child("connecting_to"),
                        &ssid[..],
                        hasher,
                    ));
                }
            },
            None => {
                self.connecting_to.take();
            }
        }
    }
}

pub struct BssInfoNode {
    node: Node,
    bssid: StringProperty,
    bssid_hash: StringProperty,
    ssid: StringProperty,
    ssid_hash: StringProperty,
    rx_dbm: IntProperty,
    snr_db: IntProperty,
    channel: UintProperty,
    protection: StringProperty,
    _is_wmm_assoc: BoolProperty,
    _wmm_param: Option<BssWmmParamNode>,
    wsc: Option<BssWscNode>,
}

impl BssInfoNode {
    fn new(node: Node, bss_info: &BssInfo, hasher: &InspectHasher) -> Self {
        let bssid = node.create_string("bssid", bss_info.bssid.to_mac_str());
        let bssid_hash = node.create_string("bssid_hash", hasher.hash_mac_addr(bss_info.bssid));
        let ssid = node.create_string("ssid", String::from_utf8_lossy(&bss_info.ssid[..]));
        let ssid_hash = node.create_string("ssid_hash", hasher.hash(&bss_info.ssid[..]));
        let rx_dbm = node.create_int("rx_dbm", bss_info.rx_dbm as i64);
        let snr_db = node.create_int("snr_db", bss_info.snr_db as i64);
        let channel = node.create_uint("channel", bss_info.channel as u64);
        let protection = node.create_string("protection", format!("{}", bss_info.protection));
        let is_wmm_assoc = node.create_bool("is_wmm_assoc", bss_info.wmm_param.is_some());
        let wmm_param = bss_info
            .wmm_param
            .as_ref()
            .map(|p| BssWmmParamNode::new(node.create_child("wmm_param"), &p));

        let mut this = Self {
            node,
            bssid,
            bssid_hash,
            ssid,
            ssid_hash,
            rx_dbm,
            snr_db,
            channel,
            protection,
            _is_wmm_assoc: is_wmm_assoc,
            _wmm_param: wmm_param,
            wsc: None,
        };
        this.update_wsc_node(bss_info);
        this
    }

    fn update(&mut self, bss_info: &BssInfo, hasher: &InspectHasher) {
        self.bssid.set(&bss_info.bssid.to_mac_str());
        self.bssid_hash.set(&hasher.hash_mac_addr(bss_info.bssid));
        self.ssid.set(&String::from_utf8_lossy(&bss_info.ssid[..]));
        self.ssid_hash.set(&hasher.hash(&bss_info.ssid[..]));
        self.rx_dbm.set(bss_info.rx_dbm as i64);
        self.snr_db.set(bss_info.snr_db as i64);
        self.channel.set(bss_info.channel as u64);
        self.protection.set(&format!("{}", bss_info.protection));
        self.update_wsc_node(bss_info);
    }

    fn update_wsc_node(&mut self, bss_info: &BssInfo) {
        match &bss_info.probe_resp_wsc {
            Some(wsc) => match self.wsc.as_mut() {
                Some(wsc_node) => wsc_node.update(wsc),
                None => self.wsc = Some(BssWscNode::new(self.node.create_child("wsc"), wsc)),
            },
            None => {
                self.wsc.take();
            }
        }
    }
}

pub struct BssWmmParamNode {
    _node: Node,
    _wmm_info: BssWmmInfoNode,
    _ac_be: BssWmmAcParamsNode,
    _ac_bk: BssWmmAcParamsNode,
    _ac_vi: BssWmmAcParamsNode,
    _ac_vo: BssWmmAcParamsNode,
}

impl BssWmmParamNode {
    fn new(node: Node, wmm_param: &ie::WmmParam) -> Self {
        let wmm_info =
            BssWmmInfoNode::new(node.create_child("wmm_info"), wmm_param.wmm_info.ap_wmm_info());
        let ac_be = BssWmmAcParamsNode::new(node.create_child("ac_be"), wmm_param.ac_be_params);
        let ac_bk = BssWmmAcParamsNode::new(node.create_child("ac_bk"), wmm_param.ac_bk_params);
        let ac_vi = BssWmmAcParamsNode::new(node.create_child("ac_vi"), wmm_param.ac_vi_params);
        let ac_vo = BssWmmAcParamsNode::new(node.create_child("ac_vo"), wmm_param.ac_vo_params);
        Self {
            _node: node,
            _wmm_info: wmm_info,
            _ac_be: ac_be,
            _ac_bk: ac_bk,
            _ac_vi: ac_vi,
            _ac_vo: ac_vo,
        }
    }
}

pub struct BssWmmInfoNode {
    _node: Node,
    _param_set_count: UintProperty,
    _uapsd: BoolProperty,
}

impl BssWmmInfoNode {
    fn new(node: Node, info: ie::ApWmmInfo) -> Self {
        let param_set_count =
            node.create_uint("param_set_count", info.parameter_set_count() as u64);
        let uapsd = node.create_bool("uapsd", info.uapsd());
        Self { _node: node, _param_set_count: param_set_count, _uapsd: uapsd }
    }
}

pub struct BssWmmAcParamsNode {
    _node: Node,
    _aifsn: UintProperty,
    _acm: BoolProperty,
    _ecw_min: UintProperty,
    _ecw_max: UintProperty,
    _txop_limit: UintProperty,
}

impl BssWmmAcParamsNode {
    fn new(node: Node, ac_params: ie::WmmAcParams) -> Self {
        let aifsn = node.create_uint("aifsn", ac_params.aci_aifsn.aifsn() as u64);
        let acm = node.create_bool("acm", ac_params.aci_aifsn.acm());
        let ecw_min = node.create_uint("ecw_min", ac_params.ecw_min_max.ecw_min() as u64);
        let ecw_max = node.create_uint("ecw_max", ac_params.ecw_min_max.ecw_max() as u64);
        let txop_limit = node.create_uint("txop_limit", ac_params.txop_limit as u64);
        Self {
            _node: node,
            _aifsn: aifsn,
            _acm: acm,
            _ecw_min: ecw_min,
            _ecw_max: ecw_max,
            _txop_limit: txop_limit,
        }
    }
}

pub struct BssWscNode {
    _node: Node,
    manufacturer: StringProperty,
    model_name: StringProperty,
    model_number: StringProperty,
    device_name: StringProperty,
}

impl BssWscNode {
    fn new(node: Node, wsc: &wsc::ProbeRespWsc) -> Self {
        let manufacturer =
            node.create_string("manufacturer", String::from_utf8_lossy(&wsc.manufacturer[..]));
        let model_name =
            node.create_string("model_name", String::from_utf8_lossy(&wsc.model_name[..]));
        let model_number =
            node.create_string("model_number", String::from_utf8_lossy(&wsc.model_number[..]));
        let device_name =
            node.create_string("device_name", String::from_utf8_lossy(&wsc.device_name[..]));

        Self { _node: node, manufacturer, model_name, model_number, device_name }
    }

    fn update(&mut self, wsc: &wsc::ProbeRespWsc) {
        self.manufacturer.set(&String::from_utf8_lossy(&wsc.manufacturer[..]));
        self.model_name.set(&String::from_utf8_lossy(&wsc.model_name[..]));
        self.model_number.set(&String::from_utf8_lossy(&wsc.model_number[..]));
        self.device_name.set(&String::from_utf8_lossy(&wsc.device_name[..]));
    }
}

pub struct ConnectingToNode {
    _node: Node,
    ssid: StringProperty,
    ssid_hash: StringProperty,
}

impl ConnectingToNode {
    fn new(node: Node, ssid: &[u8], hasher: &InspectHasher) -> Self {
        let ssid_hash = node.create_string("ssid_hash", hasher.hash(ssid));
        let ssid = node.create_string("ssid", String::from_utf8_lossy(ssid));
        Self { _node: node, ssid, ssid_hash }
    }

    fn update(&mut self, ssid: &[u8], hasher: &InspectHasher) {
        self.ssid.set(&String::from_utf8_lossy(ssid));
        self.ssid_hash.set(&hasher.hash(ssid));
    }
}

pub struct SignalSamplesNode {
    _node: Node,
    snr_histogram: IntLinearHistogramProperty,
}

impl SignalSamplesNode {
    fn new(node: Node) -> Self {
        let snr_histogram = node.create_int_linear_histogram(
            "snr_histogram",
            LinearHistogramParams { floor: 0, step_size: 1, buckets: 129 },
        );
        Self { _node: node, snr_histogram }
    }

    fn update(&mut self, ind: &fidl_mlme::SignalReportIndication) {
        self.snr_histogram.insert(ind.snr_db as i64);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::client::test_utils,
        fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty, Inspector},
    };

    #[test]
    fn test_inspect_update_pulse() {
        let hasher = InspectHasher { hash_key: [7; 8] };
        let inspector = Inspector::new();
        let root = inspector.root();
        let mut pulse = PulseNode::new(root.create_child("last_pulse"));

        // SME is idle. Pulse node should not have any field except "last_updated" and "status"
        let status = SmeStatus { connected_to: None, connecting_to: None };
        pulse.update(status, &hasher);
        assert_inspect_tree!(inspector, root: {
            last_pulse: {
                started: AnyProperty,
                last_updated: AnyProperty,
                status: { status_str: "idle" }
            }
        });

        // SME is connecting. Check that "connecting_to" field now appears, and that existing
        // fields are still kept.
        let status = SmeStatus { connected_to: None, connecting_to: Some(b"foo".to_vec()) };
        pulse.update(status, &hasher);
        assert_inspect_tree!(inspector, root: {
            last_pulse: {
                started: AnyProperty,
                last_updated: AnyProperty,
                status: {
                    status_str: "connecting",
                    connecting_to: { ssid: "foo", ssid_hash: AnyProperty }
                },
            }
        });

        // SME is connected. Aside from verifying that existing fields are kept, key things we
        // want to check are that "last_link_up" and "connected_to" are populated, and
        // "connecting_to" is cleared out.
        let status =
            SmeStatus { connected_to: Some(test_utils::fake_bss_info()), connecting_to: None };
        pulse.update(status, &hasher);
        assert_inspect_tree!(inspector, root: {
            last_pulse: {
                started: AnyProperty,
                last_updated: AnyProperty,
                last_link_up: AnyProperty,
                status: {
                    status_str: "connected",
                    connected_to: contains {
                        ssid: "foo",
                        ssid_hash: AnyProperty,
                        bssid: AnyProperty,
                        bssid_hash: AnyProperty,
                    },
                },
            }
        });

        // SME is idle. The "connected_to" field is cleared out.
        let status = SmeStatus { connected_to: None, connecting_to: None };
        pulse.update(status, &hasher);
        assert_inspect_tree!(inspector, root: {
            last_pulse: {
                started: AnyProperty,
                last_updated: AnyProperty,
                last_link_up: AnyProperty,
                status: { status_str: "idle" },
            }
        });
    }
}
