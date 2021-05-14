// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl::epitaph::ChannelEpitaphExt,
    fidl_fuchsia_wlan_device_service::{self as fidl_svc, DeviceMonitorRequest},
    fuchsia_zircon as zx,
    futures::TryStreamExt,
};

pub(crate) async fn serve_monitor_requests(
    mut req_stream: fidl_svc::DeviceMonitorRequestStream,
) -> Result<(), Error> {
    while let Some(req) = req_stream.try_next().await.context("error running DeviceService")? {
        match req {
            DeviceMonitorRequest::ListPhys { responder } => responder.send(&mut vec![]),
            DeviceMonitorRequest::GetDevPath { phy_id: _, responder } => responder.send(None),
            DeviceMonitorRequest::GetSupportedMacRoles { phy_id: _, responder } => {
                responder.send(None)
            }
            DeviceMonitorRequest::WatchDevices { watcher, control_handle: _ } => {
                watcher.into_channel().close_with_epitaph(zx::Status::NOT_SUPPORTED)
            }
            DeviceMonitorRequest::GetCountry { phy_id: _, responder } => {
                responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))
            }
            DeviceMonitorRequest::SetCountry { req: _, responder } => {
                responder.send(zx::Status::NOT_SUPPORTED.into_raw())
            }
            DeviceMonitorRequest::ClearCountry { req: _, responder } => {
                responder.send(zx::Status::NOT_SUPPORTED.into_raw())
            }
            DeviceMonitorRequest::CreateIface { req: _, responder } => {
                responder.send(zx::Status::NOT_SUPPORTED.into_raw(), None)
            }
            DeviceMonitorRequest::DestroyIface { req: _, responder } => {
                responder.send(zx::Status::NOT_SUPPORTED.into_raw())
            }
        }?;
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_proxy, Proxy},
        fidl_fuchsia_wlan_device as fidl_dev, fuchsia_async as fasync,
        futures::task::Poll,
        pin_utils::pin_mut,
        wlan_common::assert_variant,
    };

    struct TestValues {
        proxy: fidl_svc::DeviceMonitorProxy,
        req_stream: fidl_svc::DeviceMonitorRequestStream,
    }

    fn test_setup() -> TestValues {
        let (proxy, requests) = create_proxy::<fidl_svc::DeviceMonitorMarker>()
            .expect("failed to create DeviceMonitor proxy");
        let req_stream = requests.into_stream().expect("failed to create request stream");

        TestValues { proxy, req_stream }
    }

    #[test]
    fn test_list_phys() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Request the list of available PHYs.
        let list_fut = test_values.proxy.list_phys();
        pin_mut!(list_fut);
        assert_variant!(exec.run_until_stalled(&mut list_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The future to list the PHYs should complete and no PHYs should be present.
        assert_variant!(
            exec.run_until_stalled(&mut list_fut),
            Poll::Ready(Ok(phys)) => {
                assert!(phys.is_empty())
            }
        );
    }

    #[test]
    fn test_get_dev_path() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Query a PHY's dev path.
        let query_fut = test_values.proxy.get_dev_path(0);
        pin_mut!(query_fut);
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The attempt to query the PHY's information should fail.
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Ready(Ok(None)));
    }

    #[test]
    fn test_query_phy() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Query a PHY's MAC roles.
        let query_fut = test_values.proxy.get_supported_mac_roles(0);
        pin_mut!(query_fut);
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The attempt to query the PHY's information should fail.
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Ready(Ok(None)));
    }

    #[test]
    fn test_watch_devices() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Watch for new devices.
        let (watcher_proxy, watcher_server_end) =
            fidl::endpoints::create_proxy().expect("failed to create watcher proxy");
        test_values.proxy.watch_devices(watcher_server_end).expect("failed to watch devices");

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The channel should be terminated with an epitaph.
        let chan =
            watcher_proxy.into_channel().expect("failed to convert watcher proxy into channel");
        let mut buffer = zx::MessageBuf::new();
        let epitaph_fut = chan.recv_msg(&mut buffer);
        pin_mut!(epitaph_fut);
        assert_variant!(exec.run_until_stalled(&mut epitaph_fut), Poll::Ready(Ok(_)));

        use fidl::encoding::{decode_transaction_header, Decodable, Decoder, EpitaphBody};
        let (header, tail) =
            decode_transaction_header(buffer.bytes()).expect("failed decoding header");
        let mut msg = Decodable::new_empty();
        Decoder::decode_into::<EpitaphBody>(&header, tail, &mut [], &mut msg)
            .expect("failed decoding body");
        assert_eq!(msg.error, zx::Status::NOT_SUPPORTED);
        assert!(chan.is_closed());
    }

    #[test]
    fn test_get_country() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Query a PHY's country.
        let query_fut = test_values.proxy.get_country(0);
        pin_mut!(query_fut);
        assert_variant!(exec.run_until_stalled(&mut query_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The future to query the PHY's country should fail.
        assert_variant!(
            exec.run_until_stalled(&mut query_fut),
            Poll::Ready(Ok(Err(status))) => {
                assert_eq!(status, zx::Status::NOT_SUPPORTED.into_raw());
            }
        );
    }

    #[test]
    fn test_set_country() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Set a PHY's country.
        let set_fut = test_values
            .proxy
            .set_country(&mut fidl_svc::SetCountryRequest { phy_id: 0, alpha2: [0, 0] });
        pin_mut!(set_fut);
        assert_variant!(exec.run_until_stalled(&mut set_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The future to set the PHY's country should fail.
        assert_variant!(
            exec.run_until_stalled(&mut set_fut),
            Poll::Ready(Ok(status)) => {
                assert_eq!(status, zx::Status::NOT_SUPPORTED.into_raw());
            }
        );
    }

    #[test]
    fn test_clear_country() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Clear a PHY's country.
        let clear_fut =
            test_values.proxy.clear_country(&mut fidl_svc::ClearCountryRequest { phy_id: 0 });
        pin_mut!(clear_fut);
        assert_variant!(exec.run_until_stalled(&mut clear_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The future to clear the PHY's country should fail.
        assert_variant!(
            exec.run_until_stalled(&mut clear_fut),
            Poll::Ready(Ok(status)) => {
                assert_eq!(status, zx::Status::NOT_SUPPORTED.into_raw());
            }
        );
    }

    #[test]
    fn test_create_iface() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Create an interface for a PHY.
        let mut req = fidl_svc::CreateIfaceRequest {
            phy_id: 0,
            role: fidl_dev::MacRole::Client,
            mac_addr: None,
        };
        let create_fut = test_values.proxy.create_iface(&mut req);
        pin_mut!(create_fut);
        assert_variant!(exec.run_until_stalled(&mut create_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The future to create the interface should fail.
        assert_variant!(
            exec.run_until_stalled(&mut create_fut),
            Poll::Ready(Ok((status, None))) => {
                assert_eq!(status, zx::Status::NOT_SUPPORTED.into_raw());
            }
        );
    }

    #[test]
    fn test_destroy_iface() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let service_fut = serve_monitor_requests(test_values.req_stream);
        pin_mut!(service_fut);

        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // Request the destruction of an interface
        let mut req = fidl_svc::DestroyIfaceRequest { iface_id: 0 };
        let destroy_fut = test_values.proxy.destroy_iface(&mut req);
        pin_mut!(destroy_fut);
        assert_variant!(exec.run_until_stalled(&mut destroy_fut), Poll::Pending);

        // Progress the service loop.
        assert_variant!(exec.run_until_stalled(&mut service_fut), Poll::Pending);

        // The future to create the interface should fail.
        assert_variant!(
            exec.run_until_stalled(&mut destroy_fut),
            Poll::Ready(Ok(status)) => {
                assert_eq!(status, zx::Status::NOT_SUPPORTED.into_raw());
            }
        );
    }
}
