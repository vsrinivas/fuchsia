// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Manages Scan requests for the Client Policy API.
use {
    crate::{policy::client::ClientPtr, util::security_from_sme_protection},
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_sme as fidl_sme,
    futures::prelude::*,
    log::{debug, error, info},
    std::collections::HashMap,
};

// Arbitrary count of networks (ssid/security pairs) to output per request
const OUTPUT_CHUNK_NETWORK_COUNT: usize = 5;

// An internal version of fidl_policy::Bss that can be cloned
#[derive(Clone)]
struct Bss {
    /// MAC address for the AP interface.
    bssid: [u8; 6],
    /// Calculated received signal strength for the beacon/probe response.
    rssi: i8,
    /// Operating frequency for this network (in MHz).
    frequency: u32,
    /// Realtime timestamp for this scan result entry.
    timestamp_nanos: i64,
}
impl From<&fidl_sme::BssInfo> for Bss {
    fn from(bss: &fidl_sme::BssInfo) -> Bss {
        Bss {
            bssid: bss.bssid,
            rssi: bss.rx_dbm,
            frequency: 0,       // TODO(mnck): convert channel to freq
            timestamp_nanos: 0, // TODO(mnck): find where this comes from
        }
    }
}
impl From<Bss> for fidl_policy::Bss {
    fn from(input: Bss) -> Self {
        fidl_policy::Bss {
            bssid: Some(input.bssid),
            rssi: Some(input.rssi),
            frequency: Some(input.frequency),
            timestamp_nanos: Some(input.timestamp_nanos),
        }
    }
}

// An internal version of fidl_policy::ScanResult that can be cloned
#[derive(Clone)]
struct ScanResult {
    /// Network properties used to distinguish between networks and to group
    /// individual APs.
    id: fidl_policy::NetworkIdentifier,
    /// Individual access points offering the specified network.
    entries: Vec<Bss>,
    /// Indication if the detected network is supported by the implementation.
    compatibility: fidl_policy::Compatibility,
}
impl From<ScanResult> for fidl_policy::ScanResult {
    fn from(input: ScanResult) -> Self {
        fidl_policy::ScanResult {
            id: Some(input.id),
            entries: Some(input.entries.into_iter().map(fidl_policy::Bss::from).collect()),
            compatibility: Some(input.compatibility),
        }
    }
}

/// Handles incoming scan requests by creating a new SME scan request, and returning
/// scan results or errors to the output iterator.
pub async fn handle_scan(
    client: ClientPtr,
    output_iterator: fidl::endpoints::ServerEnd<fidl_policy::ScanResultIteratorMarker>,
) {
    match request_sme_scan(client).await {
        Ok(txn) => {
            info!("Sent scan request to SME successfully");
            let mut stream = txn.take_event_stream();
            let mut scanned_networks = vec![];
            while let Some(Ok(event)) = stream.next().await {
                match event {
                    fidl_sme::ScanTransactionEvent::OnResult { aps: new_aps } => {
                        debug!("Received scan results from SME");
                        scanned_networks.extend(new_aps);
                    }
                    fidl_sme::ScanTransactionEvent::OnFinished {} => {
                        info!("Finished getting scan results from SME");
                        send_scan_results(output_iterator, convert_scan_info(&scanned_networks))
                            .await
                            .unwrap_or_else(|e| error!("Failed to send scan results: {}", e));
                        return;
                    }
                    fidl_sme::ScanTransactionEvent::OnError { error } => {
                        error!("Scan error from SME: {:?}", error);
                        send_scan_error(output_iterator, fidl_policy::ScanErrorCode::GeneralError)
                            .await
                            .unwrap_or_else(|e| error!("Failed to send scan results: {}", e));
                        return;
                    }
                };
            }
        }
        Err(error) => {
            error!("Failed to send scan request to SME: {:?}", error);
            send_scan_error(output_iterator, fidl_policy::ScanErrorCode::GeneralError)
                .await
                .unwrap_or_else(|e| error!("Failed to send scan results: {}", e));
        }
    };
}

pub fn clone_sme_bss_info(bss: &fidl_sme::BssInfo) -> fidl_sme::BssInfo {
    fidl_sme::BssInfo {
        bssid: bss.bssid.clone(),
        ssid: bss.ssid.clone(),
        rx_dbm: bss.rx_dbm,
        channel: bss.channel,
        protection: bss.protection,
        compatible: bss.compatible,
    }
}

/// Converts array of fidl_sme::BssInfo to array of internal ScanResult.
/// There is one BssInfo per BSSID. In contrast, there is one ScanResult per
/// SSID, with information for multiple BSSs contained within it.
fn convert_scan_info(scanned_networks: &[fidl_sme::BssInfo]) -> Vec<ScanResult> {
    let mut bss_by_network: HashMap<fidl_policy::NetworkIdentifier, Vec<fidl_sme::BssInfo>> =
        HashMap::new();
    for bss in scanned_networks.iter() {
        if let Some(security) = security_from_sme_protection(bss.protection) {
            bss_by_network
                .entry(fidl_policy::NetworkIdentifier { ssid: bss.ssid.to_vec(), type_: security })
                .or_insert(vec![])
                .push(clone_sme_bss_info(&bss));
        } else {
            // TODO(mnck): log a metric here
            error!("Unknown security type: {:?}", bss.protection);
        }
    }
    let mut scan_results: Vec<ScanResult> = bss_by_network
        .iter()
        .map(|(network, bss_infos)| ScanResult {
            id: network.clone(),
            entries: bss_infos.iter().map(Bss::from).collect(),
            compatibility: if bss_infos.iter().any(|bss| bss.compatible) {
                fidl_policy::Compatibility::Supported
            } else {
                fidl_policy::Compatibility::DisallowedNotSupported
            },
        })
        .collect();

    scan_results.sort_by(|a, b| a.id.ssid.cmp(&b.id.ssid));
    return scan_results;
}

/// Send batches of results to the output iterator when getNext() is called on it.
/// Close the channel when no results are remaining.
async fn send_scan_results(
    output_iterator: fidl::endpoints::ServerEnd<fidl_policy::ScanResultIteratorMarker>,
    scanned_networks: Vec<ScanResult>,
) -> Result<(), fidl::Error> {
    let mut chunks = scanned_networks.chunks(OUTPUT_CHUNK_NETWORK_COUNT);
    // Wait to get a request for a chunk of scan results
    let (mut stream, ctrl) = output_iterator.into_stream_and_control_handle()?;
    loop {
        if let Some(req) = stream.try_next().await? {
            let fidl_policy::ScanResultIteratorRequest::GetNext { responder } = req;
            if let Some(chunk) = chunks.next() {
                let mut next_result: fidl_policy::ScanResultIteratorGetNextResult = Ok(chunk
                    .into_iter()
                    .map(|r| fidl_policy::ScanResult::from(r.clone()))
                    .collect());
                responder.send(&mut next_result)?;
            } else {
                // When no results are left, send an empty vec and close the channel.
                let mut next_result: fidl_policy::ScanResultIteratorGetNextResult = Ok(vec![]);
                responder.send(&mut next_result)?;
                ctrl.shutdown();
                break;
            }
        } else {
            // This will happen if the iterator request stream was closed and we expected to send
            // another response.
            info!("Peer closed channel for getting scan results unexpectedly");
        }
    }
    Ok(())
}

/// On the next request for results, send an error to the output iterator and
/// shut it down.
async fn send_scan_error(
    output_iterator: fidl::endpoints::ServerEnd<fidl_policy::ScanResultIteratorMarker>,
    error_code: fidl_policy::ScanErrorCode,
) -> Result<(), fidl::Error> {
    // Wait to get a request for a chunk of scan results
    let (mut stream, ctrl) = output_iterator.into_stream_and_control_handle()?;
    if let Some(req) = stream.try_next().await? {
        let fidl_policy::ScanResultIteratorRequest::GetNext { responder } = req;
        let mut err: fidl_policy::ScanResultIteratorGetNextResult = Err(error_code);
        responder.send(&mut err)?;
        ctrl.shutdown();
    } else {
        // This will happen if the iterator request stream was closed and we expected to send
        // another response.
        info!("Peer closed channel for getting scan results unexpectedly");
    }
    Ok(())
}

/// Attempts to issue a new scan request to the currently active SME.
async fn request_sme_scan(client: ClientPtr) -> Result<fidl_sme::ScanTransactionProxy, Error> {
    let client = client.lock();
    let client_sme =
        client.access_sme().ok_or_else(|| format_err!("no active client interface"))?;

    let mut request =
        fidl_sme::ScanRequest { timeout: 5, scan_type: fidl_common::ScanType::Passive };
    let (local, remote) =
        fidl::endpoints::create_proxy().context(format!("failed to create sme proxy"))?;
    client_sme.scan(&mut request, remote).context(format!("failed to connect to sme"))?;
    Ok(local)
}
