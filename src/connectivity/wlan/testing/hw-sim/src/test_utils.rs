// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::{create_endpoints, create_proxy},
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_service::WlanMarker,
    fidl_fuchsia_wlan_tap as wlantap,
    fuchsia_async::{Time, Timer},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::{self as zx, prelude::*},
    futures::{channel::oneshot, FutureExt, StreamExt},
    std::{
        future::Future,
        marker::Unpin,
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
    wlan_common::test_utils::ExpectWithin,
    wlantap_client::Wlantap,
};

type EventStream = wlantap::WlantapPhyEventStream;

pub struct TestHelper {
    _wlantap: Wlantap,
    proxy: Arc<wlantap::WlantapPhyProxy>,
    event_stream: Option<EventStream>,
}

struct TestHelperFuture<F, H>
where
    F: Future + Unpin,
    H: FnMut(wlantap::WlantapPhyEvent),
{
    event_stream: Option<EventStream>,
    event_handler: H,
    main_future: F,
}

impl<F, H> Unpin for TestHelperFuture<F, H>
where
    F: Future + Unpin,
    H: FnMut(wlantap::WlantapPhyEvent),
{
}

impl<F, H> Future for TestHelperFuture<F, H>
where
    F: Future + Unpin,
    H: FnMut(wlantap::WlantapPhyEvent),
{
    type Output = (F::Output, EventStream);

    /// Any events that accumulated in the |event_stream| since last poll will be passed to
    /// |event_handler| before the |main_future| is polled
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        let stream = this.event_stream.as_mut().unwrap();
        while let Poll::Ready(optional_result) = stream.poll_next_unpin(cx) {
            let event = optional_result
                .expect("Unexpected end of the WlantapPhy event stream")
                .expect("WlantapPhy event stream returned an error");
            (this.event_handler)(event);
        }
        this.main_future.poll_unpin(cx).map(|x| (x, this.event_stream.take().unwrap()))
    }
}

// Holds basic WLAN network configuration information and allows cloning and conversion to a policy
// NetworkConfig.
#[derive(Clone)]
pub struct NetworkConfigBuilder {
    ssid: Option<Vec<u8>>,
    password: Option<Vec<u8>>,
}

impl NetworkConfigBuilder {
    pub fn new() -> Self {
        Self { ssid: None, password: None }
    }

    pub fn open(self) -> Self {
        Self { password: None, ..self }
    }

    pub fn protected(self, password: &Vec<u8>) -> Self {
        Self { password: Some(password.to_vec()), ..self }
    }

    pub fn ssid(self, ssid: &Vec<u8>) -> Self {
        Self { ssid: Some(ssid.to_vec()), ..self }
    }
}

impl Into<fidl_policy::NetworkConfig> for NetworkConfigBuilder {
    fn into(self) -> fidl_policy::NetworkConfig {
        let ssid = match self.ssid {
            None => vec![],
            Some(ssid) => ssid,
        };

        let (type_, credential) = match self.password {
            None => {
                (fidl_policy::SecurityType::None, fidl_policy::Credential::None(fidl_policy::Empty))
            }
            Some(password) => {
                (fidl_policy::SecurityType::Wpa2, fidl_policy::Credential::Password(password))
            }
        };

        fidl_policy::NetworkConfig {
            id: Some(fidl_policy::NetworkIdentifier { ssid, type_ }),
            credential: Some(credential),
        }
    }
}

impl From<fidl_policy::NetworkConfig> for NetworkConfigBuilder {
    fn from(config: fidl_policy::NetworkConfig) -> NetworkConfigBuilder {
        let ssid = match config.id {
            Some(id) => Some(id.ssid),
            None => None,
        };
        let password = match config.credential {
            Some(credential) => match credential {
                fidl_policy::Credential::Password(bytes) => Some(bytes),
                fidl_policy::Credential::Psk(bytes) => Some(bytes),
                fidl_policy::Credential::None(fidl_policy::Empty) => None,
                other => panic!("invalid credential type: {:?}", other),
            },
            None => None,
        };

        NetworkConfigBuilder { ssid, password }
    }
}

impl TestHelper {
    pub async fn begin_test(config: wlantap::WlantapPhyConfig) -> Self {
        let mut helper = TestHelper::create_phy_and_helper(config);
        helper.wait_for_wlanmac_start().await;
        helper
    }

    pub async fn begin_ap_test(
        config: wlantap::WlantapPhyConfig,
        network_config: fidl_policy::NetworkConfig,
    ) -> Self {
        let mut helper = TestHelper::create_phy_and_helper(config);
        helper.wait_for_ap_start(network_config).await;
        helper.wait_for_wlanmac_start().await;
        helper
    }

    fn create_phy_and_helper(config: wlantap::WlantapPhyConfig) -> Self {
        // If injected, wlancfg does not start automatically in a test component.
        // Connecting to the service to start wlancfg so that it can create new interfaces.
        let _wlan_proxy = connect_to_service::<WlanMarker>().expect("starting wlancfg");

        let wlantap = Wlantap::open_from_isolated_devmgr().expect("Failed to open wlantapctl");
        let proxy = wlantap.create_phy(config).expect("Failed to create wlantap PHY");

        let event_stream = Some(proxy.take_event_stream());
        TestHelper { _wlantap: wlantap, proxy: Arc::new(proxy), event_stream }
    }

    async fn wait_for_ap_start(&mut self, network_config: fidl_policy::NetworkConfig) {
        let network_config = NetworkConfigBuilder::from(network_config);

        // Get a handle to control the AccessPointController.
        let ap_provider = connect_to_service::<fidl_policy::AccessPointProviderMarker>()
            .expect("connecting to AP provider");
        let (ap_controller, server_end) =
            create_proxy::<fidl_policy::AccessPointControllerMarker>()
                .expect("creating AP controller");
        let (update_client_end, _server_end) =
            create_endpoints::<fidl_policy::AccessPointStateUpdatesMarker>()
                .expect("creating AP update listener");
        let () = ap_provider
            .get_controller(server_end, update_client_end)
            .expect("getting AP controller");

        let mut retry = RetryWithBackoff::infinite_with_max_interval(120.seconds());
        loop {
            let controller = ap_controller.clone();

            // Call StartAccessPoint.  If the policy layer does not yet have an ApSmeProxy, it
            // it will attempt to create an AP interface.
            let config = network_config.clone().into();
            let connectivity_mode = fidl_policy::ConnectivityMode::Unrestricted;
            let operating_band = fidl_policy::OperatingBand::Any;

            // If the policy layer acknowledges the request to start the access point, then the
            // AP interface has been created and the test can proceed.
            match controller
                .start_access_point(config, connectivity_mode, operating_band)
                .await
                .expect("starting AP")
            {
                fidl_common::RequestStatus::Acknowledged => break,
                _ => {}
            }

            let slept = retry.sleep_unless_timed_out().await;
            assert!(slept, "unable to create AP iface");
        }
    }

    async fn wait_for_wlanmac_start(&mut self) {
        let (sender, receiver) = oneshot::channel::<()>();
        let mut sender = Some(sender);
        self.run_until_complete_or_timeout(
            120.seconds(),
            "receive a WlanmacStart event",
            move |event| match event {
                wlantap::WlantapPhyEvent::WlanmacStart { .. } => {
                    sender.take().map(|s| s.send(()));
                }
                _ => {}
            },
            receiver,
        )
        .await
        .unwrap_or_else(|oneshot::Canceled| panic!());
    }

    pub fn proxy(&self) -> Arc<wlantap::WlantapPhyProxy> {
        self.proxy.clone()
    }

    /// Will run the main future until it completes or when it has run past the specified duration.
    /// Note that any events that are observed on the event stream will be passed to the
    /// |event_handler| closure first before making progress on the main future.
    /// So if a test generates many events each of which requires significant computational time in
    /// the event handler, the main future may not be able to complete in time.
    pub async fn run_until_complete_or_timeout<R, F, H, S>(
        &mut self,
        timeout: zx::Duration,
        context: S,
        event_handler: H,
        main_future: F,
    ) -> R
    where
        H: FnMut(wlantap::WlantapPhyEvent),
        F: Future<Output = R> + Unpin,
        S: ToString,
    {
        let (item, stream) = TestHelperFuture {
            event_stream: Some(self.event_stream.take().unwrap()),
            event_handler,
            main_future,
        }
        .expect_within(timeout, format!("Did not complete in time: {}", context.to_string()))
        .await;
        self.event_stream = Some(stream);
        item
    }
}

pub struct RetryWithBackoff {
    deadline: Time,
    prev_delay: zx::Duration,
    delay: zx::Duration,
    max_delay: zx::Duration,
}

impl RetryWithBackoff {
    pub fn new(timeout: zx::Duration) -> Self {
        RetryWithBackoff {
            deadline: Time::after(timeout),
            prev_delay: 0.millis(),
            delay: 1.millis(),
            max_delay: std::i64::MAX.nanos(),
        }
    }

    pub fn infinite_with_max_interval(max_delay: zx::Duration) -> Self {
        Self { deadline: Time::INFINITE, max_delay, ..Self::new(0.nanos()) }
    }

    /// Sleep (in async term) a little longer (following Fibonacci series) after each call until
    /// timeout is reached.
    /// Return whether it has run past the deadline.
    pub async fn sleep_unless_timed_out(&mut self) -> bool {
        if Time::after(0.millis()) > self.deadline {
            false
        } else {
            let () = Timer::new(::std::cmp::min(Time::after(self.delay), self.deadline)).await;
            if self.delay < self.max_delay {
                let new_delay = std::cmp::min(self.max_delay, self.prev_delay + self.delay);
                self.prev_delay = self.delay;
                self.delay = new_delay;
            }
            true
        }
    }
}
