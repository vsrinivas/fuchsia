// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_mlme::{self, BssDescription, ScanResultCodes, ScanRequest};
use std::collections::{HashMap, VecDeque};
use std::collections::hash_map::Entry;
use std::cmp::Ordering;
use std::mem;
use std::sync::Arc;
use client::{bss::*, DeviceCapabilities, Ssid};

// Scans can be performed for two different purposes:
//      1) Discover available wireless networks. These scans are initiated by the "user",
//         i.e. a client process that talks to SME, and thus have 'tokens' to identify
//         a specific user request.
//      2) Join a specific network. These scans can only be initiated by the SME state machine
//         itself.

// An SME-initiated scan request for the purpose of joining a network
#[derive(Debug, PartialEq)]
pub struct JoinScan<T> {
    pub ssid: Ssid,
    pub token: T,
}

// A "user"-initiated scan request for the purpose of discovering available networks
#[derive(Debug, PartialEq)]
pub struct DiscoveryScan<T> {
    pub token: T,
}

pub struct ScanScheduler<D, J> {
    // The currently running scan. We assume that MLME can handle a single concurrent scan
    // regardless of its own state.
    current: ScanState<D, J>,
    // A pending scan request for the purpose of joining a network. This type of request
    // always takes priority over discovery scans. There can only be one such pending request:
    // if SME requests another one, the newer one wins and overwrites an existing request.
    pending_join: Option<JoinScan<J>>,
    // A queue of pending discovery requests from the user
    pending_discovery: VecDeque<DiscoveryScan<D>>,
    device_caps: Arc<DeviceCapabilities>
}

#[derive(Debug, PartialEq)]
enum ScanState<D, J> {
    NotScanning,
    ScanningToJoin(JoinScan<J>),
    ScanningToDiscover(DiscoveryScan<D>),
}

#[derive(Clone, Copy, Debug)]
pub enum JoinScanFailure {
    // MLME returned a set of BSS descriptions, but none of them matched our criteria
    // (SSID, RSN, etc)
    NoMatchingBssFound,
    // MLME returned an error code
    ScanFailed(ScanResultCodes),
    // Scan was canceled or superseded by another request
    Canceled
}

// A reaction to MLME's ScanConfirm event
pub enum ScanResult<D, J> {
    // No reaction: the scan results are not relevant anymore, or we received
    // an unexpected ScanConfirm.
    None,
    // "Join" scan has finished successfully and a matching BSS was found.
    // The SME state machine can now send a JoinRequest to MLME if it desires so.
    ReadyToJoin {
        token: J,
        best_bss: BssDescription,
    },
    // "Join" scan was unsuccessful.
    CannotJoin{
        token: J,
        reason: JoinScanFailure,
    },
    // "Discovery" scan has finished, either successfully or not.
    // SME is expected to forward the result to the user.
    DiscoveryFinished {
        token: D,
        result: DiscoveryResult,
    },
}

pub type DiscoveryResult = Result<Vec<EssInfo>, DiscoveryError>;

#[derive(Debug, Fail)]
pub enum DiscoveryError {
    #[fail(display = "Scanning not supported by device")]
    NotSupported,
}

impl<D, J> ScanScheduler<D, J> {
    pub fn new(device_caps: Arc<DeviceCapabilities>) -> Self {
        ScanScheduler {
            current: ScanState::NotScanning,
            pending_join: None,
            pending_discovery: VecDeque::new(),
            device_caps,
        }
    }

    // Initiate a "join" scan.
    //
    // If there is an existing pending "join" scan request, it will be discarded and replaced
    // with the new one. In this case, the token of the discarded request will be returned.
    //
    // If there is a currently running "join" scan, its results will be discarded, too.
    //
    // The scan might or might not begin immediately.
    // If a ScanRequest is returned, the caller is responsible for forwarding it to MLME.
    pub fn enqueue_scan_to_join(&mut self, s: JoinScan<J>)
        -> (Option<J>, Option<ScanRequest>)
    {
        let old_token = mem::replace(&mut self.pending_join, Some(s))
            .map(|p| p.token);
        (old_token, self.start_next_scan())
    }

    // Initiate a "discovery" scan. The scan might or might not begin immediately.
    // If a ScanRequest is returned, the caller is responsible for forwarding it to MLME.
    pub fn enqueue_scan_to_discover(&mut self, s: DiscoveryScan<D>) -> Option<ScanRequest> {
        self.pending_discovery.push_back(s);
        self.start_next_scan()
    }

    // Should be called for every ScanConfirm message received from MLME.
    // The caller is expected to take action based on the returned ScanResult.
    // If a ScanRequest is returned, the caller is responsible for forwarding it to MLME.
    pub fn on_mlme_scan_confirm(&mut self, msg: fidl_mlme::ScanConfirm)
        -> (ScanResult<D, J>, Option<ScanRequest>)
    {
        let old_state = mem::replace(&mut self.current, ScanState::NotScanning);
        let result = match old_state {
            ScanState::NotScanning => {
                eprintln!("Unexpected ScanConfirm message from MLME");
                ScanResult::None
            },
            ScanState::ScanningToJoin(join_scan) => {
                if self.pending_join.is_some() {
                    // The scan that just finished was superseded by a newer join scan request
                    ScanResult::CannotJoin {
                        token: join_scan.token,
                        reason: JoinScanFailure::Canceled,
                    }
                } else {
                    match msg.result_code {
                        ScanResultCodes::Success => {
                            match best_bss_to_join(msg.bss_description_set, &join_scan.ssid) {
                                None => ScanResult::CannotJoin{
                                    token: join_scan.token,
                                    reason: JoinScanFailure::NoMatchingBssFound,
                                },
                                Some(bss) => ScanResult::ReadyToJoin {
                                    token: join_scan.token,
                                    best_bss: bss,
                                },
                            }
                        },
                        other => ScanResult::CannotJoin {
                            token: join_scan.token,
                            reason: JoinScanFailure::ScanFailed(other),
                        }
                    }
                }
            },
            ScanState::ScanningToDiscover(discover_scan) => {
                ScanResult::DiscoveryFinished {
                    token: discover_scan.token,
                    result: convert_discovery_result(msg),
                }
            }
        };
        let request = self.start_next_scan();
        (result, request)
    }

    // Returns the most recent join scan request, if there is one.
    pub fn get_join_scan(&self) -> Option<&JoinScan<J>> {
        if let &Some(ref s) = &self.pending_join {
            Some(s)
        } else if let &ScanState::ScanningToJoin(ref s) = &self.current {
            Some(s)
        } else {
            None
        }
    }

    fn start_next_scan(&mut self) -> Option<ScanRequest> {
        match &self.current {
            &ScanState::NotScanning => {
                if let Some(join_scan) = self.pending_join.take() {
                    let request = new_join_scan_request(&join_scan, &self.device_caps);
                    self.current = ScanState::ScanningToJoin(join_scan);
                    Some(request)
                } else if let Some(discovery_scan) = self.pending_discovery.pop_front() {
                    let request = new_discovery_scan_request(&discovery_scan, &self.device_caps);
                    self.current = ScanState::ScanningToDiscover(discovery_scan);
                    Some(request)
                } else {
                    None
                }
            },
            _ => None
        }
    }

}

const WILDCARD_BSS_ID: [u8; 6] = [ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff ];

fn new_join_scan_request<T>(join_scan: &JoinScan<T>,
                            device_caps: &DeviceCapabilities) -> ScanRequest {
    ScanRequest {
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        bssid: WILDCARD_BSS_ID.clone(),
        // TODO(gbonik): change MLME interface to use bytes instead of string for SSID
        ssid: String::from_utf8_lossy(&join_scan.ssid).to_string(),
        scan_type: fidl_mlme::ScanTypes::Passive,
        probe_delay: 0,
        channel_list: Some(get_channels_to_scan(&device_caps)),
        min_channel_time: 100,
        max_channel_time: 300,
        ssid_list: None
    }
}

fn new_discovery_scan_request<T>(_discovery_scan: &DiscoveryScan<T>,
                                 device_caps: &DeviceCapabilities) -> ScanRequest {
    ScanRequest {
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        bssid: WILDCARD_BSS_ID.clone(),
        ssid: String::new(),
        scan_type: fidl_mlme::ScanTypes::Passive,
        probe_delay: 0,
        channel_list: Some(get_channels_to_scan(&device_caps)),
        min_channel_time: 100,
        max_channel_time: 300,
        ssid_list: None
    }
}

fn convert_discovery_result(msg: fidl_mlme::ScanConfirm) -> DiscoveryResult {
    match msg.result_code {
        ScanResultCodes::Success => Ok(group_networks(msg.bss_description_set)),
        ScanResultCodes::NotSupported => Err(DiscoveryError::NotSupported)
    }
}

fn group_networks(bss_set: Vec<BssDescription>) -> Vec<EssInfo> {
    let mut best_bss_by_ssid: HashMap<Ssid, BssDescription> = HashMap::new();
    for bss in bss_set {
        match best_bss_by_ssid.entry(bss.ssid.bytes().collect()) {
            Entry::Vacant(e) => { e.insert(bss); },
            Entry::Occupied(mut e) =>
                if compare_bss(e.get(), &bss) == Ordering::Less {
                    e.insert(bss);
                }
        };
    }
    best_bss_by_ssid.values()
        .map(|bss| EssInfo {
                best_bss: convert_bss_description(&bss)
            })
        .collect()
}

fn best_bss_to_join(bss_set: Vec<BssDescription>, ssid: &[u8]) -> Option<BssDescription> {
    bss_set.into_iter()
        .filter(|bss_desc| bss_desc.ssid.as_bytes() == ssid)
        .max_by(compare_bss)
}

fn get_channels_to_scan(device_caps: &DeviceCapabilities) -> Vec<u8> {
    SUPPORTED_CHANNELS.iter()
        .filter(|chan| device_caps.supported_channels.contains(chan))
        .map(|chan| *chan)
        .collect()
}

const SUPPORTED_CHANNELS: &[u8] = &[
    // 5GHz UNII-1
    36, 40, 44, 48,
    // 5GHz UNII-2 Middle
    52, 56, 60, 64,
    // 5GHz UNII-2 Extended
    100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
    // 5GHz UNII-3
    149, 153, 157, 161, 165,
    // 2GHz
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
];

#[cfg(test)]
mod tests {
    use super::*;

    use std::collections::HashSet;

    #[test]
    fn get_join_scan() {
        let mut sched = create_sched();
        assert_eq!(None, sched.get_join_scan());

        sched.enqueue_scan_to_join(JoinScan { ssid: b"foo".to_vec(), token: 10 });
        // Make sure the scanner is in the state we expect it to be: the request is
        // 'current', not 'pending'
        assert_eq!(ScanState::ScanningToJoin(JoinScan{ ssid: b"foo".to_vec(), token: 10 }),
                   sched.current);
        assert_eq!(None, sched.pending_join);

        assert_eq!(Some(&JoinScan{ ssid: b"foo".to_vec(), token: 10 }),
                   sched.get_join_scan());

        sched.enqueue_scan_to_join(JoinScan { ssid: b"bar".to_vec(), token: 20 });
        // Again, make sure the state is what we expect. "Foo" should still be the current request,
        // while "bar" should be pending
        assert_eq!(ScanState::ScanningToJoin(JoinScan{ ssid: b"foo".to_vec(), token: 10 }),
                   sched.current);
        assert_eq!(Some(JoinScan{ ssid: b"bar".to_vec(), token: 20 }),
                   sched.pending_join);

        // Expect the pending request to be returned since the current one will be discarded
        // once the scan finishes
        assert_eq!(Some(&JoinScan{ ssid: b"bar".to_vec(), token: 20 }),
                   sched.get_join_scan());
    }

    fn create_sched() -> ScanScheduler<i32, i32> {
        ScanScheduler::new(Arc::new(
            DeviceCapabilities {
                supported_channels: HashSet::new(),
            }
        ))
    }
}
