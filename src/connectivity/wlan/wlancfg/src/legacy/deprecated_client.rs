// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::legacy::IfaceRef,
    fidl, fidl_fuchsia_wlan_product_deprecatedclient as deprecated,
    fidl_fuchsia_wlan_sme as fidl_sme,
    futures::prelude::*,
    log::{debug, error},
};

const MAX_CONCURRENT_WLAN_REQUESTS: usize = 1000;

/// Takes in stream of deprecated client requests and handles each one.
pub async fn serve_deprecated_client(
    requests: deprecated::DeprecatedClientRequestStream,
    iface: IfaceRef,
) -> Result<(), fidl::Error> {
    requests
        .try_for_each_concurrent(MAX_CONCURRENT_WLAN_REQUESTS, |req| {
            handle_request(iface.clone(), req)
        })
        .await
}

/// Handles an individual request from the deprecated client API.
async fn handle_request(
    iface: IfaceRef,
    req: deprecated::DeprecatedClientRequest,
) -> Result<(), fidl::Error> {
    match req {
        deprecated::DeprecatedClientRequest::Status { responder } => {
            debug!("Deprecated WLAN client API used for status request");
            let mut r = status(&iface).await;
            responder.send(&mut r)
        }
    }
}

/// Produces a status representing the state where no client interface is present.
fn no_client_status() -> deprecated::WlanStatus {
    deprecated::WlanStatus { state: deprecated::State::NoClient, current_ap: None }
}

/// Manages the calling of client SME status and translation into a format that is compatible with
/// the deprecated client API.
async fn status(iface: &IfaceRef) -> deprecated::WlanStatus {
    let iface = match iface.get() {
        Ok(iface) => iface,
        Err(_) => return no_client_status(),
    };

    let status = match iface.sme.status().await {
        Ok(status) => status,
        Err(e) => {
            // An error here indicates that the SME channel is broken.
            error!("Failed to query status: {}", e);
            return no_client_status();
        }
    };

    deprecated::WlanStatus {
        state: convert_state(&status),
        current_ap: extract_current_ap(&status),
    }
}

/// Translates a client SME's status information into a deprecated client state.
fn convert_state(status: &fidl_sme::ClientStatusResponse) -> deprecated::State {
    match status {
        fidl_sme::ClientStatusResponse::Connected(_) => deprecated::State::Associated,
        fidl_sme::ClientStatusResponse::Connecting(_) => deprecated::State::Associating,
        fidl_sme::ClientStatusResponse::Idle(_) => deprecated::State::Disassociated,
    }
}

/// Parses a Client SME's status and extracts AP SSID and RSSI if applicable.
fn extract_current_ap(status: &fidl_sme::ClientStatusResponse) -> Option<Box<deprecated::Ap>> {
    match status {
        fidl_sme::ClientStatusResponse::Connected(serving_ap_info) => {
            let ssid = String::from_utf8_lossy(&serving_ap_info.ssid).to_string();
            let rssi_dbm = serving_ap_info.rssi_dbm;
            Some(Box::new(deprecated::Ap { ssid, rssi_dbm }))
        }
        fidl_sme::ClientStatusResponse::Connecting(_) | fidl_sme::ClientStatusResponse::Idle(_) => {
            None
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::legacy::Iface, fidl::endpoints::create_proxy,
        fidl_fuchsia_wlan_common as fidl_common, fuchsia_async as fasync, futures::task::Poll,
        pin_utils::pin_mut, wlan_common::assert_variant,
    };

    struct TestValues {
        iface: IfaceRef,
        sme_stream: fidl_sme::ClientSmeRequestStream,
    }

    fn test_setup() -> TestValues {
        let (sme, server) =
            create_proxy::<fidl_sme::ClientSmeMarker>().expect("failed to create ClientSmeProxy");

        let iface = Iface { sme, iface_id: 0 };
        let iface_ref = IfaceRef::new();
        iface_ref.set_if_empty(iface);

        TestValues {
            iface: iface_ref,
            sme_stream: server.into_stream().expect("failed to create ClientSmeRequestStream"),
        }
    }

    #[fuchsia::test]
    fn test_no_client() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let iface = IfaceRef::new();
        let status_fut = status(&iface);
        pin_mut!(status_fut);

        // Expect that no client is reported and the AP status information is empty.
        assert_variant!(
            exec.run_until_stalled(&mut status_fut),
            Poll::Ready(deprecated::WlanStatus {
                state: deprecated::State::NoClient,
                current_ap: None,
            })
        );
    }

    #[fuchsia::test]
    fn test_broken_sme() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Drop the SME request stream so that the client request will fail.
        drop(test_values.sme_stream);

        let status_fut = status(&test_values.iface);
        pin_mut!(status_fut);

        // Expect that no client is reported and the AP status information is empty.
        assert_variant!(
            exec.run_until_stalled(&mut status_fut),
            Poll::Ready(deprecated::WlanStatus {
                state: deprecated::State::NoClient,
                current_ap: None,
            })
        );
    }

    #[fuchsia::test]
    fn test_disconnected_client() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let status_fut = status(&test_values.iface);
        pin_mut!(status_fut);

        // Expect an SME status request and send back a response indicating that the SME is neither
        // connected nor connecting.
        assert_variant!(exec.run_until_stalled(&mut status_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Status { responder }))) => {
                responder.send(&mut fidl_sme::ClientStatusResponse::Idle(fidl_sme::Empty{})).expect("could not send sme response")
            }
        );

        // Expect a disconnected status.
        assert_variant!(
            exec.run_until_stalled(&mut status_fut),
            Poll::Ready(deprecated::WlanStatus {
                state: deprecated::State::Disassociated,
                current_ap: None,
            })
        );
    }

    #[fuchsia::test]
    fn test_connecting_client() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let status_fut = status(&test_values.iface);
        pin_mut!(status_fut);

        // Expect an SME status request and send back a response indicating that the SME is
        // connecting.
        assert_variant!(exec.run_until_stalled(&mut status_fut), Poll::Pending);
        assert_variant!(
                    exec.run_until_stalled(&mut test_values.sme_stream.next()),
                    Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Status { responder }))) => {
                        responder.send(&mut fidl_sme::ClientStatusResponse::Connecting("test_ssid".as_bytes().to_vec()))
        .expect("could not send sme response")
                    }
                );

        // Expect a connecting status.
        assert_variant!(
            exec.run_until_stalled(&mut status_fut),
            Poll::Ready(deprecated::WlanStatus {
                state: deprecated::State::Associating,
                current_ap: None,
            })
        );
    }

    #[fuchsia::test]
    fn test_connected_client() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let ssid = "test_ssid";
        let rssi_dbm = -70;
        let status_fut = status(&test_values.iface);
        pin_mut!(status_fut);

        // Expect an SME status request and send back a response indicating that the SME is
        // connected.
        assert_variant!(exec.run_until_stalled(&mut status_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Status { responder }))) => {
                responder.send(&mut fidl_sme::ClientStatusResponse::Connected(
                    fidl_sme::ServingApInfo{
                        bssid: [0, 0, 0, 0, 0, 0],
                        ssid: ssid.as_bytes().to_vec(),
                        rssi_dbm,
                        snr_db: 0,
                        channel: fidl_common::WlanChannel {
                            primary: 1,
                            cbw: fidl_common::ChannelBandwidth::Cbw20,
                            secondary80: 0,
                        },
                        protection: fidl_sme::Protection::Unknown,
                    })).expect("could not send sme response")
            }
        );

        // Expect a connected status.
        let expected_current_ap =
            Some(Box::new(deprecated::Ap { ssid: ssid.to_string(), rssi_dbm }));
        assert_variant!(
            exec.run_until_stalled(&mut status_fut),
            Poll::Ready(deprecated::WlanStatus {
                state: deprecated::State::Associated,
                current_ap,
            }) => {
                assert_eq!(current_ap, expected_current_ap);
            }
        );
    }
}
