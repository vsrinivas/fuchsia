// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{test_utils, wlancfg_helper},
    fidl_fuchsia_wlan_device_service::{DeviceMonitorProxy, QueryIfaceResponse},
    fidl_fuchsia_wlan_sme::{ApSmeProxy, ClientSmeProxy},
    fidl_fuchsia_wlan_tap::WlantapPhyConfig,
    fuchsia_zircon::DurationNum,
    tracing::info,
};

pub struct CreateDeviceHelper<'a> {
    dev_monitor: &'a DeviceMonitorProxy,
    iface_ids: Vec<u16>,
}

impl<'a> CreateDeviceHelper<'a> {
    pub fn new(dev_monitor: &'a DeviceMonitorProxy) -> CreateDeviceHelper<'a> {
        return CreateDeviceHelper { dev_monitor, iface_ids: vec![] };
    }

    pub async fn create_device(
        &mut self,
        config: WlantapPhyConfig,
        network_config: Option<wlancfg_helper::NetworkConfigBuilder>,
    ) -> Result<(test_utils::TestHelper, u16), anyhow::Error> {
        let helper = match network_config {
            Some(network_config) => {
                test_utils::TestHelper::begin_ap_test(config, network_config).await
            }
            None => test_utils::TestHelper::begin_test(config).await,
        };

        let iface_id = get_first_matching_iface_id(self.dev_monitor, |iface| {
            !self.iface_ids.contains(&iface.id)
        })
        .await;
        self.iface_ids.push(iface_id);

        Ok((helper, iface_id))
    }
}

/// Queries wlandevicemonitor service and return the first iface id that makes |filter(iface)| true.
/// Panics after timeout expires.
pub async fn get_first_matching_iface_id<F: Fn(&QueryIfaceResponse) -> bool>(
    monitor: &DeviceMonitorProxy,
    filter: F,
) -> u16 {
    // Sleep between queries to make main future yield.
    let mut infinite_timeout =
        super::test_utils::RetryWithBackoff::infinite_with_max_interval(10.seconds());
    // Verbose logging of DeviceServiceProxy calls inserted to assist debugging
    // flakes such as https://fxbug.dev/85468.
    let mut attempt = 1;
    loop {
        info!("Calling list_ifaces(): attempt {}", attempt);
        let ifaces = monitor.list_ifaces().await.expect("getting iface list");
        {
            for iface_id in ifaces {
                info!("Calling query_iface({})", iface_id);
                let result = monitor.query_iface(iface_id).await.expect("querying iface info");
                match result {
                    Ok(resp) if filter(&resp) => return iface_id,
                    Err(e) => panic!("query_iface {} failed: {}", iface_id, e),
                    _ => (),
                }
            }
        }
        info!("Failed to find a suitable iface.");
        // unwrap() will never fail since there is an infinite deadline.
        infinite_timeout.sleep_unless_after_deadline_verbose().await.unwrap();
        attempt += 1;
    }
}

/// Wrapper function to get an ApSmeProxy from wlandevicemonitor with an |iface_id| assumed to be valid.
pub async fn get_ap_sme(monitor: &DeviceMonitorProxy, iface_id: u16) -> ApSmeProxy {
    let (proxy, remote) = fidl::endpoints::create_proxy().expect("fail to create fidl endpoints");
    monitor
        .get_ap_sme(iface_id, remote)
        .await
        .expect("failed to request ap sme")
        .expect("ap sme request completed with error");
    proxy
}

/// Wrapper function to get a ClientSmeProxy from wlandevicemonitor with an |iface_id| assumed to be valid.
pub async fn get_client_sme(monitor: &DeviceMonitorProxy, iface_id: u16) -> ClientSmeProxy {
    let (proxy, remote) = fidl::endpoints::create_proxy().expect("fail to create fidl endpoints");
    monitor
        .get_client_sme(iface_id, remote)
        .await
        .expect("failed to request client sme")
        .expect("client sme request completed with error");
    proxy
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_wlan_common::WlanMacRole::*,
        fidl_fuchsia_wlan_device_service::{
            DeviceMonitorMarker, DeviceMonitorRequest, QueryIfaceResponse,
        },
        futures::{pin_mut, StreamExt},
        std::task::Poll,
        wlan_common::assert_variant,
    };

    fn fake_query_iface_response() -> QueryIfaceResponse {
        QueryIfaceResponse { role: Client, id: 0, phy_id: 0, phy_assigned_id: 0, sta_addr: [0; 6] }
    }

    fn test_matching_iface_id<F: Fn(&QueryIfaceResponse) -> bool>(
        filter: F,
        mut list_response: Vec<u16>,
        query_responses: Vec<QueryIfaceResponse>,
        expected_id: Option<u16>,
    ) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("creating executor");
        let (proxy, remote) =
            fidl::endpoints::create_proxy::<DeviceMonitorMarker>().expect("creating proxy");
        let mut request_stream = remote.into_stream().expect("getting request stream");

        let iface_id_fut = get_first_matching_iface_id(&proxy, filter);
        pin_mut!(iface_id_fut);

        // This line advances get_first_matching_iface_id to the point where it calls list_ifaces()
        // and waits for a response.
        assert_variant!(exec.run_until_stalled(&mut iface_id_fut), Poll::Pending);
        // The fake server receives the call as a request.
        let responder = assert_variant!(exec.run_singlethreaded(&mut request_stream.next()),
                                         Some(Ok(DeviceMonitorRequest::ListIfaces{responder})) => responder);
        // The fake response is sent.
        responder.send(&mut list_response[..]).expect("sending list ifaces response");

        for query_resp in query_responses {
            // This line advances the future to the point where it calls query_iface(id) and waits
            // for a response.
            assert_variant!(exec.run_until_stalled(&mut iface_id_fut), Poll::Pending);
            // The fake server receives the call as a request.
            let (id, responder) = assert_variant!(
                exec.run_singlethreaded(&mut request_stream.next()),
                Some(Ok(DeviceMonitorRequest::QueryIface{iface_id, responder})) => (iface_id, responder));
            assert_eq!(id, query_resp.id);
            // The fake response is sent.
            responder.send(&mut Ok(query_resp)).expect("sending query iface response");
        }

        match expected_id {
            Some(id) => {
                let got_id = assert_variant!(exec.run_until_stalled(&mut iface_id_fut),
                                            Poll::Ready(id) => id);
                assert_eq!(got_id, id);
            }
            None => assert_variant!(exec.run_until_stalled(&mut iface_id_fut), Poll::Pending),
        }
    }

    #[test]
    fn no_iface() {
        test_matching_iface_id(|_iface| true, vec![], vec![], None);
    }

    #[test]
    fn found_ap_iface() {
        test_matching_iface_id(
            |iface| iface.role == Ap,
            vec![0, 3],
            vec![
                fake_query_iface_response(),
                QueryIfaceResponse { role: Ap, id: 3, ..fake_query_iface_response() },
            ],
            Some(3),
        );
    }

    #[test]
    fn ifaces_exist_but_no_match() {
        test_matching_iface_id(
            |iface| iface.role == Client,
            vec![0],
            vec![QueryIfaceResponse { role: Ap, ..fake_query_iface_response() }],
            None,
        )
    }
}
