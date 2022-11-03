// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        wlancfg_helper::{
            init_client_controller, start_ap_and_wait_for_confirmation, NetworkConfigBuilder,
        },
        BeaconInfo, EventHandlerBuilder, ScanResults,
    },
    fidl::endpoints::create_proxy,
    fidl::prelude::*,
    fidl_fuchsia_io as fio, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_tap as wlantap,
    fuchsia_async::{DurationExt, Time, TimeoutExt, Timer},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon::{self as zx, prelude::*},
    futures::{channel::oneshot, FutureExt, StreamExt},
    ieee80211::Ssid,
    pin_utils::pin_mut,
    std::{
        convert::TryFrom,
        fs::File,
        future::Future,
        marker::Unpin,
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
    tracing::{debug, info},
    wlan_common::test_utils::ExpectWithin,
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
        let mut helper = TestHelper::create_phy_and_helper(config).await;
        helper.wait_for_wlan_softmac_start().await;
        helper
    }
    pub async fn begin_ap_test(
        config: wlantap::WlantapPhyConfig,
        network_config: NetworkConfigBuilder,
    ) -> Self {
        let mut helper = TestHelper::create_phy_and_helper(config).await;
        start_ap_and_wait_for_confirmation(network_config).await;
        helper.wait_for_wlan_softmac_start().await;
        helper
    }
    async fn create_phy_and_helper(config: wlantap::WlantapPhyConfig) -> Self {
        // If injected, wlancfg does not start automatically in a test component.
        // Connecting to the service to start wlancfg so that it can create new interfaces.
        let _wlan_proxy =
            connect_to_protocol::<fidl_policy::ClientProviderMarker>().expect("starting wlancfg");

        // The IsolatedDevMgr in CFv2 does not allow a configuration to block until a device is
        // ready. Wait indefinitely here until the device becomes available.
        let raw_dir = File::open("/dev").expect("failed to open /dev");
        let zircon_channel =
            fdio::clone_channel(&raw_dir).expect("failed to clone directory channel");
        let async_channel = fuchsia_async::Channel::from_channel(zircon_channel)
            .expect("failed to create async channel from zircon channel");
        let dir = fio::DirectoryProxy::from_channel(async_channel);
        info!("Waiting for /dev/sys/test/wlantapctl to appear.");
        device_watcher::recursive_wait_and_open_node(&dir, "sys/test/wlantapctl").await.unwrap();

        // Trigger creation of wlantap serviced phy and iface for testing.
        let wlantap = Wlantap::open().expect("Failed to open wlantapctl");
        let proxy = wlantap.create_phy(config).expect("Failed to create wlantap PHY");
        let event_stream = Some(proxy.take_event_stream());
        TestHelper { _wlantap: wlantap, proxy: Arc::new(proxy), event_stream, is_stopped: false }
    }
    async fn wait_for_wlan_softmac_start(&mut self) {
        let (sender, receiver) = oneshot::channel::<()>();
        let mut sender = Some(sender);
        self.run_until_complete_or_timeout(
            120.seconds(),
            "receive a WlanSoftmacStart event",
            move |event| match event {
                wlantap::WlantapPhyEvent::WlanSoftmacStart { .. } => {
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
    next_delay: zx::Duration,
    max_delay: zx::Duration,
}
impl RetryWithBackoff {
    pub fn new(timeout: zx::Duration) -> Self {
        RetryWithBackoff {
            deadline: Time::after(timeout),
            prev_delay: 0.millis(),
            next_delay: 1.millis(),
            max_delay: std::i64::MAX.nanos(),
        }
    }
    pub fn infinite_with_max_interval(max_delay: zx::Duration) -> Self {
        Self { deadline: Time::INFINITE, max_delay, ..Self::new(0.nanos()) }
    }

    /// Return Err if the deadline was exceeded when this function was called.
    /// Otherwise, sleep for a little longer (following Fibonacci series) or up
    /// to the deadline, whichever is soonest. If a sleep occurred, this function
    /// returns Ok. The value contained in both Ok and Err is the zx::Duration
    /// until or after the deadline when the function returns.
    async fn sleep_unless_after_deadline_(
        &mut self,
        verbose: bool,
    ) -> Result<zx::Duration, zx::Duration> {
        // Add an inner scope up to just after Timer::new to ensure all
        // time assignments are dropped after the sleep occurs. This
        // prevents misusing them after the sleep since they are all
        // no longer correct after the clock moves.
        {
            if Time::after(0.millis()) > self.deadline {
                if verbose {
                    info!("Skipping sleep. Deadline exceeded.");
                }
                return Err(self.deadline - Time::now());
            }

            let sleep_deadline = std::cmp::min(Time::after(self.next_delay), self.deadline);
            if verbose {
                let micros = sleep_deadline.into_nanos() / 1_000;
                info!("Sleeping until {}.{} 😴", micros / 1_000_000, micros % 1_000_000);
            }

            Timer::new(sleep_deadline).await;
        }

        // If the next delay interval exceeds max_delay (even if by overflow),
        // then saturate at max_delay.
        if self.next_delay < self.max_delay {
            let next_delay = std::cmp::min(
                self.max_delay,
                self.prev_delay.into_nanos().saturating_add(self.next_delay.into_nanos()).nanos(),
            );
            self.prev_delay = self.next_delay;
            self.next_delay = next_delay;
        }

        Ok(self.deadline - Time::now())
    }

    pub async fn sleep_unless_after_deadline(&mut self) -> Result<zx::Duration, zx::Duration> {
        self.sleep_unless_after_deadline_(false).await
    }

    pub async fn sleep_unless_after_deadline_verbose(
        &mut self,
    ) -> Result<zx::Duration, zx::Duration> {
        self.sleep_unless_after_deadline_(true).await
    }
}

pub type ScanResult = (Ssid, [u8; 6], bool, i8);

pub async fn scan_for_networks<'a>(
    phy: &Arc<wlantap::WlantapPhyProxy>,
    beacons: Vec<BeaconInfo<'a>>,
    helper: &mut TestHelper,
) -> Vec<ScanResult> {
    // Create a client controller.
    let (client_controller, _update_stream) = init_client_controller().await;
    let scan_event =
        EventHandlerBuilder::new().on_start_scan(ScanResults::new(phy, beacons)).build();
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
    // Run the scan routine for up to 70s; WLAN policy should have a timeout before this.
    let scanned_networks = helper
        .run_until_complete_or_timeout(70.seconds(), "receive a scan response", scan_event, fut)
        .await;

    let mut scan_results: Vec<ScanResult> = Vec::new();
    for result in scanned_networks {
        let id = result.id.expect("empty network ID");
        let ssid = Ssid::try_from(id.ssid).unwrap();
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
/// This function returns `Ok(r)`, where `r` is the return value from `main_future`,
/// if `main_future` completes before the `timeout` duration. Otherwise, `Err(())` is returned.
pub async fn timeout_after<R, F: Future<Output = R> + Unpin>(
    timeout: zx::Duration,
    main_future: &mut F,
) -> Result<R, ()> {
    async { Ok(main_future.await) }.on_timeout(timeout.after_now(), || Err(())).await
}
