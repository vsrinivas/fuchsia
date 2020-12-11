// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

use {
    fidl_fuchsia_wlan_sme as fidl_sme, fuchsia_async as fasync,
    futures::{prelude::*, stream::StreamFuture, task::Poll},
    wlan_common::assert_variant,
};

pub fn poll_sme_req(
    exec: &mut fasync::Executor,
    next_sme_req: &mut StreamFuture<fidl_fuchsia_wlan_sme::ClientSmeRequestStream>,
) -> Poll<fidl_fuchsia_wlan_sme::ClientSmeRequest> {
    exec.run_until_stalled(next_sme_req).map(|(req, stream)| {
        *next_sme_req = stream.into_future();
        req.expect("did not expect the SME request stream to end")
            .expect("error polling SME request stream")
    })
}

pub fn validate_sme_scan_request_and_send_results(
    exec: &mut fasync::Executor,
    sme_stream: &mut fidl_sme::ClientSmeRequestStream,
    expected_scan_request: &fidl_sme::ScanRequest,
    mut scan_results: Vec<fidl_sme::BssInfo>,
) {
    // Check that a scan request was sent to the sme and send back results
    assert_variant!(
        exec.run_until_stalled(&mut sme_stream.next()),
        Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
            txn, req, control_handle: _
        }))) => {
            // Validate the request
            assert_eq!(req, *expected_scan_request);
            // Send all the APs
            let (_stream, ctrl) = txn
                .into_stream_and_control_handle().expect("error accessing control handle");
            ctrl.send_on_result(&mut scan_results.iter_mut())
                .expect("failed to send scan data");

            // Send the end of data
            ctrl.send_on_finished()
                .expect("failed to send scan data");
        }
    );
}
