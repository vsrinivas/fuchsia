// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        wlancfg_helper::{
            init_client_controller, start_ap_and_wait_for_confirmation, NetworkConfigBuilder,
        },
        Beacon, EventHandlerBuilder, Sequence,
    },
    fidl::endpoints::create_proxy,
    fidl_fuchsia_wlan_policy as fidl_policy, fidl_fuchsia_wlan_tap as wlantap,
    fuchsia_async::{Time, Timer},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::{self as zx, prelude::*},
    futures::{channel::oneshot, FutureExt, StreamExt},
    log::{debug, info},
    pin_utils::pin_mut,
    std::{
        future::Future,
        marker::Unpin,
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
    wlan_common::{bss::Protection, mac::Bssid, test_utils::ExpectWithin},
    wlantap_client::Wlantap,
};

type EventStream = wlantap::WlantapPhyEventStream;

pub struct TestHelper {
    _wlantap: Wlantap,
    proxy: Arc<wlantap::WlantapPhyProxy>,
    event_stream: Option<EventStream>,
    is_stopped: bool,
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

        match this.main_future.poll_unpin(cx) {
            Poll::Pending => {
                debug!("Polled main_future. Still waiting for completion.");
                Poll::Pending
            }
            Poll::Ready(x) => {
                info!("main_future complete. No further events will be processed from the event stream.");
                Poll::Ready((x, this.event_stream.take().unwrap()))
            }
        }
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
        network_config: NetworkConfigBuilder,
    ) -> Self {
        let mut helper = TestHelper::create_phy_and_helper(config);
        start_ap_and_wait_for_confirmation(network_config).await;
        helper.wait_for_wlanmac_start().await;
        helper
    }

    fn create_phy_and_helper(config: wlantap::WlantapPhyConfig) -> Self {
        // If injected, wlancfg does not start automatically in a test component.
        // Connecting to the service to start wlancfg so that it can create new interfaces.
        let _wlan_proxy =
            connect_to_service::<fidl_policy::ClientProviderMarker>().expect("starting wlancfg");

        let wlantap = Wlantap::open_from_isolated_devmgr().expect("Failed to open wlantapctl");
        let proxy = wlantap.create_phy(config).expect("Failed to create wlantap PHY");

        let event_stream = Some(proxy.take_event_stream());
        TestHelper { _wlantap: wlantap, proxy: Arc::new(proxy), event_stream, is_stopped: false }
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
        info!("main_future started. Events will be handled by event_handler.");
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

    // stop must be called at the end of the test to tell wlantap-phy driver that the proxy's
    // channel that is about to be closed (dropped) from our end so that the driver would not try to
    // access it. Otherwise it could crash the isolated devmgr.
    pub async fn stop(mut self) {
        let () = self.proxy.shutdown().await.expect("shut down fake phy");
        self.is_stopped = true;
    }
}

impl Drop for TestHelper {
    fn drop(&mut self) {
        assert!(self.is_stopped, "Must call stop() on a TestHelper before dropping it");
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

pub struct ScanTestBeacon {
    pub channel: u8,
    pub bssid: Bssid,
    pub ssid: Vec<u8>,
    pub protection: Protection,
    pub rssi: Option<i8>,
}

fn phy_event_from_beacons<'a>(
    phy: &'a Arc<wlantap::WlantapPhyProxy>,
    beacons: &[ScanTestBeacon],
) -> impl FnMut(wlantap::WlantapPhyEvent) + 'a {
    let mut beacon_sequence = Sequence::start();

    for beacon in beacons {
        let mut beacon_to_send = Beacon::send_on_primary_channel(beacon.channel, phy)
            .bssid(beacon.bssid)
            .ssid(beacon.ssid.clone())
            .protection(beacon.protection.clone());

        match beacon.rssi {
            Some(rssi) => {
                beacon_to_send = beacon_to_send.rssi(rssi);
            }
            None => {}
        }

        beacon_sequence = beacon_sequence.then(beacon_to_send);
    }

    EventHandlerBuilder::new().on_set_channel(beacon_sequence).build()
}

pub type ScanResult = (String, [u8; 6], bool, i8);

pub async fn scan_for_networks(
    phy: &Arc<wlantap::WlantapPhyProxy>,
    beacons: &[ScanTestBeacon],
    helper: &mut TestHelper,
) -> Vec<ScanResult> {
    // Create a client controller.
    let (client_controller, _update_stream) = init_client_controller().await;
    let scan_event = phy_event_from_beacons(phy, beacons);

    // Request a scan from the policy layer.
    let fut = async move {
        let (scan_proxy, server_end) = create_proxy().unwrap();
        client_controller.scan_for_networks(server_end).expect("requesting scan");

        let mut scan_results = Vec::new();
        loop {
            let result = scan_proxy.get_next().await.expect("getting scan results");
            let mut new_scan_results = result.expect("scanning failed");
            if new_scan_results.is_empty() {
                break;
            }
            scan_results.append(&mut new_scan_results)
        }

        return scan_results;
    };
    pin_mut!(fut);

    // Run the scan routine for up to 10s.
    let scanned_networks = helper
        .run_until_complete_or_timeout(10.seconds(), "receive a scan response", scan_event, fut)
        .await;

    let mut scan_results = Vec::new();
    for result in scanned_networks {
        let id = result.id.expect("empty network ID");
        let ssid = String::from_utf8_lossy(&id.ssid).to_string();
        let compatibility = result.compatibility.expect("empty compatibility");

        for entry in result.entries.expect("empty scan entries") {
            let bssid = entry.bssid.expect("empty BSSID");
            let rssi = entry.rssi.expect("empty RSSI");
            scan_results.push((
                ssid.clone(),
                bssid,
                compatibility == fidl_policy::Compatibility::Supported,
                rssi,
            ));
        }
    }

    scan_results
}
