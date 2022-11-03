// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

use {
    fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_zircon as zx,
    futures::{prelude::*, stream::StreamFuture, task::Poll},
    log::debug,
    wlan_common::assert_variant,
};

#[track_caller]
pub fn poll_sme_req(
    exec: &mut fasync::TestExecutor,
    next_sme_req: &mut StreamFuture<fidl_fuchsia_wlan_sme::ClientSmeRequestStream>,
) -> Poll<fidl_fuchsia_wlan_sme::ClientSmeRequest> {
    exec.run_until_stalled(next_sme_req).map(|(req, stream)| {
        *next_sme_req = stream.into_future();
        req.expect("did not expect the SME request stream to end")
            .expect("error polling SME request stream")
    })
}

#[track_caller]
pub fn validate_sme_scan_request_and_send_results(
    exec: &mut fasync::TestExecutor,
    sme_stream: &mut fidl_sme::ClientSmeRequestStream,
    expected_scan_request: &fidl_sme::ScanRequest,
    scan_results: Vec<fidl_sme::ScanResult>,
) {
    // Check that a scan request was sent to the sme and send back results
    assert_variant!(
        exec.run_until_stalled(&mut sme_stream.next()),
        Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
            req, responder,
        }))) => {
            // Validate the request
            assert_eq!(&req, expected_scan_request);
            // Send all the APs
            responder.send(&mut Ok(scan_results)).expect("failed to send scan data");
        }
    );
}

/// It takes an indeterminate amount of time for the scan module to either send the results
/// to the location sensor, or be notified by the component framework that the location
/// sensor's channel is closed / non-existent. This function continues trying to advance the
/// future until the next expected event happens (e.g. an event is present on the sme stream
/// for the expected active scan).
#[track_caller]
pub fn poll_for_and_validate_sme_scan_request_and_send_results(
    exec: &mut fasync::TestExecutor,
    network_selection_fut: &mut (impl futures::Future + std::marker::Unpin),
    sme_stream: &mut fidl_sme::ClientSmeRequestStream,
    expected_scan_request: &fidl_sme::ScanRequest,
    scan_results: Vec<fidl_sme::ScanResult>,
) {
    let mut counter = 0;
    let sme_stream_result = loop {
        counter += 1;
        if counter > 1000 {
            panic!("Failed to progress network selection future until active scan");
        };
        let sleep_duration = zx::Duration::from_millis(2);
        exec.run_singlethreaded(fasync::Timer::new(sleep_duration.after_now()));
        assert_variant!(
            exec.run_until_stalled(network_selection_fut),
            Poll::Pending,
            "Did not get 'poll::Pending' on network_selection_fut"
        );
        match exec.run_until_stalled(&mut sme_stream.next()) {
            Poll::Pending => continue,
            other_result => {
                debug!("Required {} iterations to get an SME stream message", counter);
                break other_result;
            }
        }
    };

    assert_variant!(
        sme_stream_result,
        Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
            req, responder
        }))) => {
            // Validate the request
            assert_eq!(req, *expected_scan_request);
            // Send all the APs
            responder.send(&mut Ok(scan_results)).expect("failed to send scan data");
        }
    );
}
