// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_mlme::{self, BssDescription, ScanResultCodes, ScanRequest};
use std::collections::{HashMap, VecDeque};
use std::collections::hash_map::Entry;
use std::cmp::Ordering;
use std::mem;
use std::sync::Arc;
use client::{bss::*, DeviceInfo, Ssid};

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
    device_info: Arc<DeviceInfo>,
    last_mlme_txn_id: u64,
}

#[derive(Debug, PartialEq)]
enum ScanState<D, J> {
    NotScanning,
    ScanningToJoin {
        cmd: JoinScan<J>,
        mlme_txn_id: u64,
        best_bss: Option<BssDescription>
    },
    ScanningToDiscover {
        cmd: DiscoveryScan<D>,
        mlme_txn_id: u64,
        bss_list: Vec<BssDescription>
    },
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
    #[fail(display = "Internal error occurred")]
    InternalError,
}

impl<D, J> ScanScheduler<D, J> {
    pub fn new(device_info: Arc<DeviceInfo>) -> Self {
        ScanScheduler {
            current: ScanState::NotScanning,
            pending_join: None,
            pending_discovery: VecDeque::new(),
            device_info,
            last_mlme_txn_id: 0,
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

    // Should be called for every OnScanResult event received from MLME.
    pub fn on_mlme_scan_result(&mut self, msg: fidl_mlme::ScanResult) {
        if !self.matching_mlme_txn_id(msg.txn_id) {
            return;
        }
        match &mut self.current {
            ScanState::NotScanning => {},
            ScanState::ScanningToJoin { cmd, best_bss, .. } => {
                if cmd.ssid == msg.bss.ssid.as_bytes() {
                    match best_bss {
                        Some(best_bss) if
                            compare_bss(best_bss, &msg.bss) != Ordering::Less => {},
                        other => *other = Some(msg.bss),
                    }
                }
            },
            ScanState::ScanningToDiscover { bss_list, .. } => {
                bss_list.push(msg.bss)
            }
        }
    }

    // Should be called for every OnScanEnd event received from MLME.
    // The caller is expected to take action based on the returned ScanResult.
    // If a ScanRequest is returned, the caller is responsible for forwarding it to MLME.
    pub fn on_mlme_scan_end(&mut self, msg: fidl_mlme::ScanEnd)
        -> (ScanResult<D, J>, Option<ScanRequest>)
    {
        if !self.matching_mlme_txn_id(msg.txn_id) {
            return (ScanResult::None, None);
        }
        let old_state = mem::replace(&mut self.current, ScanState::NotScanning);
        let result = match old_state {
            ScanState::NotScanning => ScanResult::None,
            ScanState::ScanningToJoin{ cmd, best_bss, .. } => {
                if self.pending_join.is_some() {
                    // The scan that just finished was superseded by a newer join scan request
                    ScanResult::CannotJoin {
                        token: cmd.token,
                        reason: JoinScanFailure::Canceled,
                    }
                } else {
                    match msg.code {
                        ScanResultCodes::Success => {
                            match best_bss {
                                None => ScanResult::CannotJoin {
                                    token: cmd.token,
                                    reason: JoinScanFailure::NoMatchingBssFound,
                                },
                                Some(bss) => ScanResult::ReadyToJoin {
                                    token: cmd.token,
                                    best_bss: bss,
                                },
                            }
                        },
                        other => ScanResult::CannotJoin {
                            token: cmd.token,
                            reason: JoinScanFailure::ScanFailed(other),
                        }
                    }
                }
            },
            ScanState::ScanningToDiscover{ cmd, bss_list, .. } => {
                ScanResult::DiscoveryFinished {
                    token: cmd.token,
                    result: convert_discovery_result(msg, bss_list),
                }
            }
        };
        let request = self.start_next_scan();
        (result, request)
    }

    // Returns the most recent join scan request, if there is one.
    pub fn get_join_scan(&self) -> Option<&JoinScan<J>> {
        if let Some(s) = &self.pending_join {
            Some(s)
        } else if let ScanState::ScanningToJoin{ cmd, .. } = &self.current {
            Some(cmd)
        } else {
            None
        }
    }

    fn matching_mlme_txn_id(&self, incoming_txn_id: u64) -> bool {
        match &self.current {
            ScanState::NotScanning => false,
            ScanState::ScanningToJoin { mlme_txn_id, .. }
                | ScanState::ScanningToDiscover { mlme_txn_id, .. }
                => *mlme_txn_id == incoming_txn_id
        }
    }

    fn start_next_scan(&mut self) -> Option<ScanRequest> {
        match &self.current {
            ScanState::NotScanning => {
                if let Some(join_scan) = self.pending_join.take() {
                    self.last_mlme_txn_id += 1;
                    let request = new_join_scan_request(
                        self.last_mlme_txn_id, &join_scan, &self.device_info);
                    self.current = ScanState::ScanningToJoin {
                        cmd: join_scan,
                        mlme_txn_id: self.last_mlme_txn_id,
                        best_bss: None,
                    };
                    Some(request)
                } else if let Some(discovery_scan) = self.pending_discovery.pop_front() {
                    self.last_mlme_txn_id += 1;
                    let request = new_discovery_scan_request(
                        self.last_mlme_txn_id, &discovery_scan, &self.device_info);
                    self.current = ScanState::ScanningToDiscover{
                        cmd: discovery_scan,
                        mlme_txn_id: self.last_mlme_txn_id,
                        bss_list: Vec::new(),
                    };
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

fn new_join_scan_request<T>(mlme_txn_id: u64,
                            join_scan: &JoinScan<T>,
                            device_info: &DeviceInfo) -> ScanRequest {
    ScanRequest {
        txn_id: mlme_txn_id,
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        bssid: WILDCARD_BSS_ID.clone(),
        // TODO(gbonik): change MLME interface to use bytes instead of string for SSID
        ssid: String::from_utf8_lossy(&join_scan.ssid).to_string(),
        scan_type: fidl_mlme::ScanTypes::Passive,
        probe_delay: 0,
        channel_list: Some(get_channels_to_scan(&device_info)),
        min_channel_time: 100,
        max_channel_time: 300,
        ssid_list: None
    }
}

fn new_discovery_scan_request<T>(mlme_txn_id: u64,
                                 _discovery_scan: &DiscoveryScan<T>,
                                 device_info: &DeviceInfo) -> ScanRequest {
    ScanRequest {
        txn_id: mlme_txn_id,
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        bssid: WILDCARD_BSS_ID.clone(),
        ssid: String::new(),
        scan_type: fidl_mlme::ScanTypes::Passive,
        probe_delay: 0,
        channel_list: Some(get_channels_to_scan(&device_info)),
        min_channel_time: 100,
        max_channel_time: 300,
        ssid_list: None
    }
}

fn convert_discovery_result(msg: fidl_mlme::ScanEnd,
                            bss_list: Vec<BssDescription>) -> DiscoveryResult {
    match msg.code {
        ScanResultCodes::Success => Ok(group_networks(bss_list)),
        ScanResultCodes::NotSupported => Err(DiscoveryError::NotSupported),
        ScanResultCodes::InvalidArgs => {
            eprintln!("Scan returned INVALID_ARGS");
            Err(DiscoveryError::InternalError)
        },
        ScanResultCodes::InternalError => Err(DiscoveryError::InternalError),
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

fn get_channels_to_scan(device_info: &DeviceInfo) -> Vec<u8> {
    SUPPORTED_CHANNELS.iter()
        .filter(|chan| device_info.supported_channels.contains(chan))
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
    use client::test_utils::fake_bss_description;

    const CLIENT_ADDR: [u8; 6] = [0x7A, 0xE7, 0x76, 0xD9, 0xF2, 0x67];

    #[test]
    fn discovery_scan() {
        let mut sched = create_sched();
        let req = sched.enqueue_scan_to_discover(DiscoveryScan { token: 10 })
            .expect("expected a ScanRequest");
        let txn_id = req.txn_id;
        sched.on_mlme_scan_result(fidl_mlme::ScanResult {
            txn_id,
            bss: fake_bss_description(b"foo".to_vec()),
        });
        sched.on_mlme_scan_result(fidl_mlme::ScanResult {
            txn_id: txn_id + 100, // mismatching transaction id
            bss: fake_bss_description(b"bar".to_vec()),
        });
        sched.on_mlme_scan_result(fidl_mlme::ScanResult {
            txn_id,
            bss: fake_bss_description(b"qux".to_vec()),
        });
        let (result, req) = sched.on_mlme_scan_end(fidl_mlme::ScanEnd {
            txn_id,
            code: fidl_mlme::ScanResultCodes::Success,
        });
        assert!(req.is_none());
        let result = match result {
            ScanResult::DiscoveryFinished { token, result } => {
                assert_eq!(10, token);
                result
            },
            _ => panic!("expected ScanResult::DiscoveryFinished")
        };
        let mut ssid_list = result.expect("expected a successful scan result")
            .into_iter().map(|ess| ess.best_bss.ssid).collect::<Vec<_>>();
        ssid_list.sort();
        assert_eq!(vec![b"foo".to_vec(), b"qux".to_vec()], ssid_list);
    }

    #[test]
    fn join_scan() {
        let mut sched = create_sched();
        let (discarded_token, req) = sched.enqueue_scan_to_join(
            JoinScan { ssid: b"foo".to_vec(), token: 10 });
        assert!(discarded_token.is_none());
        let txn_id = req.expect("expected a ScanRequest").txn_id;

        // Matching BSS with poor signal quality
        sched.on_mlme_scan_result(fidl_mlme::ScanResult {
            txn_id,
            bss: fake_bss_rcpi(b"foo".to_vec(), [1, 1, 1, 1, 1, 1], -100),
        });

        // Great signal quality but mismatching transaction ID
        sched.on_mlme_scan_result(fidl_mlme::ScanResult {
            txn_id: txn_id + 100,
            bss: fake_bss_rcpi(b"foo".to_vec(), [2, 2, 2, 2, 2, 2], -10),
        });

        // Great signal quality but mismatching SSID
        sched.on_mlme_scan_result(fidl_mlme::ScanResult {
            txn_id,
            bss: fake_bss_rcpi(b"bar".to_vec(), [3, 3, 3, 3, 3, 3], -10),
        });

        // Matching BSS with good signal quality
        sched.on_mlme_scan_result(fidl_mlme::ScanResult {
            txn_id,
            bss: fake_bss_rcpi(b"foo".to_vec(), [4, 4, 4, 4, 4, 4], -30),
        });

        // Matching BSS with decent signal quality
        sched.on_mlme_scan_result(fidl_mlme::ScanResult {
            txn_id,
            bss: fake_bss_rcpi(b"foo".to_vec(), [5, 5, 5, 5, 5, 5], -50),
        });

        let (result, req) = sched.on_mlme_scan_end(fidl_mlme::ScanEnd {
            txn_id,
            code: fidl_mlme::ScanResultCodes::Success,
        });
        assert!(req.is_none());
        let best_bss = match result {
            ScanResult::ReadyToJoin { token, best_bss } => {
                assert_eq!(10, token);
                best_bss
            },
            _ => panic!("expected ScanResult::ReadyToJoin")
        };

        // Expect the matching bss with best signal quality to be picked
        assert_eq!(fake_bss_rcpi(b"foo".to_vec(), [4, 4, 4, 4, 4, 4], -30), best_bss);
    }

    #[test]
    fn get_join_scan() {
        let mut sched = create_sched();
        assert_eq!(None, sched.get_join_scan());

        sched.enqueue_scan_to_join(JoinScan { ssid: b"foo".to_vec(), token: 10 });
        // Make sure the scanner is in the state we expect it to be: the request is
        // 'current', not 'pending'
        assert_eq!(ScanState::ScanningToJoin{
            cmd: JoinScan{ ssid: b"foo".to_vec(), token: 10 },
            mlme_txn_id: 1,
            best_bss: None,
        }, sched.current);
        assert_eq!(None, sched.pending_join);

        assert_eq!(Some(&JoinScan{ ssid: b"foo".to_vec(), token: 10 }),
                   sched.get_join_scan());

        sched.enqueue_scan_to_join(JoinScan { ssid: b"bar".to_vec(), token: 20 });
        // Again, make sure the state is what we expect. "Foo" should still be the current request,
        // while "bar" should be pending
        assert_eq!(ScanState::ScanningToJoin{
            cmd: JoinScan{ ssid: b"foo".to_vec(), token: 10 },
            mlme_txn_id: 1,
            best_bss: None,
        }, sched.current);
        assert_eq!(Some(JoinScan{ ssid: b"bar".to_vec(), token: 20 }),
                   sched.pending_join);

        // Expect the pending request to be returned since the current one will be discarded
        // once the scan finishes
        assert_eq!(Some(&JoinScan{ ssid: b"bar".to_vec(), token: 20 }),
                   sched.get_join_scan());
    }

    fn fake_bss_rcpi(ssid: Ssid, bssid: [u8; 6], rcpi_dbmh: i16) -> BssDescription {
        BssDescription {
            bssid,
            rcpi_dbmh,
            .. fake_bss_description(ssid)
        }
    }

    fn create_sched() -> ScanScheduler<i32, i32> {
        ScanScheduler::new(Arc::new(
            DeviceInfo {
                supported_channels: HashSet::new(),
                addr: CLIENT_ADDR,
            }
        ))
    }
}
