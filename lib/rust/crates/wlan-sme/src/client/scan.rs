// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_mlme::{self, MlmeEvent, BssDescription, ScanResultCodes, ScanRequest};
use std::collections::VecDeque;
use super::super::MlmeRequest;

// Scans can be performed for two different purposes:
//      1) Discover available wireless networks. These scans are initiated by the "user",
//         i.e. a client process that talks to SME, and thus have 'tokens' to identify
//         a specific user request.
//      2) Join a specific network. These scans can only be initiated by the SME state machine
//         itself.

// An SME-initiated scan request for the purpose of joining a network
pub struct JoinScan {
    pub ssid: String
}

// A "user"-initiated scan request for the purpose of discovering available networks
pub struct DiscoveryScan<T> {
    pub token: T,
}

type UserTxnId = u64;

pub struct ScanScheduler<T> {
    // The currently running scan. We assume that MLME can handle a single concurrent scan
    // regardless of its own state.
    current: ScanState<T>,
    // A pending scan request for the purpose of joining a network. This type of request
    // always takes priority over discovery scans. There can only be one such pending request:
    // if SME requests another one, the newer one wins and overwrites an existing request.
    pending_join: Option<JoinScan>,
    // A queue of pending discovery requests from the user
    pending_discovery: VecDeque<DiscoveryScan<T>>,
}

enum ScanState<T> {
    NotScanning,
    ScanningToJoin(JoinScan),
    ScanningToDiscover(DiscoveryScan<T>),
}

// A "join" scan can fail for two reasons:
#[derive(Clone, Copy, Debug)]
pub enum JoinScanFailure {
    // MLME returned a set of BSS descriptions, but none of them matched our criteria
    // (SSID, RSN, etc)
    NoMatchingBssFound,
    // MLME returned an error code
    ScanFailed(ScanResultCodes),
}

// A reaction to MLME's ScanConfirm event
pub enum ScanResult<T> {
    // No reaction: the scan results are not relevant anymore, or we received
    // an unexpected ScanConfirm.
    None,
    // "Join" scan has finished successfully and a matching BSS was found.
    // The SME state machine can now send a JoinRequest to MLME if it desires so.
    ReadyToJoin {
        best_bss: BssDescription,
    },
    // "Join" scan was unsuccessful.
    CannotJoin(JoinScanFailure),
    // "Discovery" scan has finished, either successfully or not.
    // SME is expected to forward the result to the user.
    DiscoveryFinished {
        token: T,
        result: fidl_mlme::ScanConfirm,
    },
}

impl<T> ScanScheduler<T> {
    pub fn new() -> Self {
        ScanScheduler {
            current: ScanState::NotScanning,
            pending_join: None,
            pending_discovery: VecDeque::new(),
        }
    }

    // Initiate a "join" scan. If there is an existing pending "join" scan request, it will
    // be discarded and replaced with the new one. If there is a currently running "join" scan,
    // its results will be discarded, too.
    //
    // The scan might or might not begin immediately.
    // If a ScanRequest is returned, the caller is responsible for forwarding it to MLME.
    pub fn enqueue_scan_to_join(&mut self, s: JoinScan) -> Option<ScanRequest> {
        self.pending_join = Some(s);
        self.start_next_scan()
    }

    // Initiate a "discovery" scan. The scan might or might not begin immediately.
    // If a ScanRequest is returned, the caller is responsible for forwarding it to MLME.
    pub fn enqueue_scan_to_discover(&mut self, s: DiscoveryScan<T>) -> Option<ScanRequest> {
        self.pending_discovery.push_back(s);
        self.start_next_scan()
    }

    // Should be called for every ScanConfirm message received from MLME.
    // The caller is expected to take action based on the returned ScanResult.
    // If a ScanRequest is returned, the caller is responsible for forwarding it to MLME.
    pub fn on_mlme_scan_confirm(&mut self, msg: fidl_mlme::ScanConfirm)
        -> (ScanResult<T>, Option<ScanRequest>)
    {
        let old_state = ::std::mem::replace(&mut self.current, ScanState::NotScanning);
        let result = match old_state {
            ScanState::NotScanning => {
                eprintln!("Unexpected ScanConfirm message from MLME");
                ScanResult::None
            },
            ScanState::ScanningToJoin(join_scan) => {
                if self.pending_join.is_some() {
                    // The scan that just finished was superseded by a newer join scan request:
                    // ignore the confirmation entirely
                    ScanResult::None
                } else {
                    match msg.result_code {
                        ScanResultCodes::Success => {
                            match best_bss_to_join(msg.bss_description_set, &join_scan.ssid) {
                                None => ScanResult::CannotJoin(JoinScanFailure::NoMatchingBssFound),
                                Some(bss) => ScanResult::ReadyToJoin { best_bss: bss },
                            }
                        },
                        other => ScanResult::CannotJoin(JoinScanFailure::ScanFailed(other)),
                    }
                }
            },
            ScanState::ScanningToDiscover(discover_scan) => {
                ScanResult::DiscoveryFinished {
                    token: discover_scan.token,
                    result: msg,
                }
            }
        };
        let request = self.start_next_scan();
        (result, request)
    }

    fn start_next_scan(&mut self) -> Option<ScanRequest> {
        match &self.current {
            &ScanState::NotScanning => {
                if let Some(join_scan) = self.pending_join.take() {
                    let request = new_join_scan_request(&join_scan);
                    self.current = ScanState::ScanningToJoin(join_scan);
                    Some(request)
                } else if let Some(discovery_scan) = self.pending_discovery.pop_front() {
                    let request = new_discovery_scan_request(&discovery_scan);
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

fn new_join_scan_request(join_scan: &JoinScan) -> ScanRequest {
    ScanRequest {
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        bssid: WILDCARD_BSS_ID.clone(),
        ssid: join_scan.ssid.clone(),
        scan_type: fidl_mlme::ScanTypes::Passive,
        probe_delay: 0,
        channel_list: None,
        min_channel_time: 100,
        max_channel_time: 300,
        ssid_list: None
    }
}

fn new_discovery_scan_request<T>(discovery_scan: &DiscoveryScan<T>) -> ScanRequest {
    ScanRequest {
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        bssid: WILDCARD_BSS_ID.clone(),
        ssid: String::new(),
        scan_type: fidl_mlme::ScanTypes::Passive,
        probe_delay: 0,
        channel_list: None,
        min_channel_time: 100,
        max_channel_time: 300,
        ssid_list: None
    }
}

fn best_bss_to_join(bss_set: Vec<BssDescription>, ssid: &String) -> Option<BssDescription> {
    bss_set.into_iter()
        .filter(|bss_desc| &bss_desc.ssid == ssid)
        .max_by_key(|bss_desc| bss_desc.rcpi_dbmh)
}
