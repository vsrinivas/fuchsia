// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_control::{
        ControlMarker, InputCapabilityType, OutputCapabilityType, PairingDelegateMarker,
    },
    fidl_fuchsia_bluetooth_host::{HostMarker, HostRequest},
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_bluetooth::types::{Address, HostId},
    fuchsia_zircon::DurationNum,
    futures::{join, TryStreamExt},
    matches::assert_matches,
    std::path::Path,
};

use crate::{
    host_device::HostDevice,
    host_dispatcher,
    services::control::{self, input_cap_to_sys, output_cap_to_sys},
};

// Open a channel, spawn the stream, queue a message and close the remote end before running the
// loop and see if that halts within a timeout
#[fuchsia_async::run_singlethreaded(test)]
async fn close_channel_when_client_dropped() -> Result<(), Error> {
    let (client, server) = fidl::endpoints::create_proxy_and_stream::<ControlMarker>()?;
    let hd = crate::host_dispatcher::test::make_simple_test_dispatcher();
    let serve_until_done = control::run(hd, server);

    // Send a FIDL request - we use set_io_capabilities as it does not depend on a host being
    // available
    let fidl_sent =
        client.set_io_capabilities(InputCapabilityType::None, OutputCapabilityType::None);
    assert_matches!(fidl_sent, Ok(()));

    // Before receiving a response, drop our end of the channel so that the remote end should
    // terminate
    std::mem::drop(client);

    let timeout = 5.seconds();
    // As we have dropped the client, this should terminate successfully before the timeout
    serve_until_done.on_timeout(timeout.after_now(), move || Err(format_err!("Timed out"))).await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_set_pairing_delegate_successful() -> Result<(), Error> {
    let dispatcher = host_dispatcher::test::make_simple_test_dispatcher();

    let address = Address::Public([1, 2, 3, 4, 5, 6]);
    let host_id = HostId(42);
    let (host_proxy, host_server) = fidl::endpoints::create_proxy_and_stream::<HostMarker>()?;
    let host_device = HostDevice::mock(host_id, address, Path::new("/dev/host"), host_proxy);
    dispatcher.add_test_host(host_id, host_device);

    let (control, control_requests) = fidl::endpoints::create_proxy_and_stream::<ControlMarker>()?;
    let run_control_service = control::run(dispatcher, control_requests);

    let expected_input = InputCapabilityType::None;
    let expected_output = OutputCapabilityType::None;
    assert!(control.set_io_capabilities(expected_input, expected_output).is_ok());
    let (delegate, _ignored_delegate_server) =
        fidl::endpoints::create_request_stream::<PairingDelegateMarker>()?;
    let set_delegate =
        async move { assert!(control.set_pairing_delegate(Some(delegate)).await.is_ok()) };
    let run_host = async {
        let mut received = false;
        let _ignored_result = host_server
            .try_for_each(|request| match request {
                HostRequest::SetPairingDelegate { input, output, .. } => {
                    assert_eq!(input, input_cap_to_sys(expected_input));
                    assert_eq!(output, output_cap_to_sys(expected_output));
                    received = true;
                    futures::future::ok(())
                }
                _ => {
                    panic!("Unexpected HostRequest");
                }
            })
            .await;
        assert!(received);
    };

    join!(run_control_service, set_delegate, run_host).0
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_set_pairing_delegate_no_capabilities() -> Result<(), Error> {
    let dispatcher = host_dispatcher::test::make_simple_test_dispatcher();

    let address = Address::Public([1, 2, 3, 4, 5, 6]);
    let host_id = HostId(42);
    let (host_proxy, host_server) = fidl::endpoints::create_proxy_and_stream::<HostMarker>()?;
    let host_device = HostDevice::mock(host_id, address, Path::new("/dev/host"), host_proxy);
    dispatcher.add_test_host(host_id, host_device);

    let (control, control_requests) = fidl::endpoints::create_proxy_and_stream::<ControlMarker>()?;
    let run_control_service = control::run(dispatcher, control_requests);

    // Set a pairing delegate via Control, but have not set the IO capabilities
    let (delegate, _ignored_delegate_server) =
        fidl::endpoints::create_request_stream::<PairingDelegateMarker>()?;
    let set_delegate =
        async move { assert!(control.set_pairing_delegate(Some(delegate)).await.is_ok()) };

    // We don't expect the host to be notified, as we have not set IO capabilities yet
    let run_host_expect_no_call = async {
        let _ignored_result = host_server
            .try_for_each(|request| -> futures::future::Ready<Result<(), fidl::Error>> {
                match request {
                    HostRequest::SetPairingDelegate { .. } => {
                        panic!("Should not have received SetPairingDelegate call");
                    }
                    _ => {
                        panic!("Unexpected HostRequest");
                    }
                }
            })
            .await;
    };

    join!(run_control_service, set_delegate, run_host_expect_no_call).0
}
