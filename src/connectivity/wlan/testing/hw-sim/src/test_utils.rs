// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_service::WlanMarker,
    fidl_fuchsia_wlan_tap as wlantap,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::{self as zx, prelude::*},
    futures::{channel::oneshot, ready, task::Context, Future, FutureExt, Poll, StreamExt},
    std::{marker::Unpin, pin::Pin, sync::Arc},
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

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        match this.main_future.poll_unpin(cx) {
            Poll::Ready(x) => Poll::Ready((x, this.event_stream.take().unwrap())),
            Poll::Pending => {
                let stream = this.event_stream.as_mut().unwrap();
                loop {
                    let event = ready!(stream.poll_next_unpin(cx))
                        .expect("Unexpected end of the WlantapPhy event stream")
                        .expect("WlantapPhy event stream returned an error");
                    (this.event_handler)(event);
                }
            }
        }
    }
}

impl TestHelper {
    pub fn begin_test(exec: &mut fasync::Executor, config: wlantap::WlantapPhyConfig) -> Self {
        // If injected, wlancfg does not start automatically in a test component.
        // Connecting to the service to start wlancfg so that it can create new interfaces.
        let _wlan_proxy = connect_to_service::<WlanMarker>().expect("starting wlancfg");

        let wlantap = Wlantap::open_from_isolated_devmgr().expect("Failed to open wlantapctl");
        let proxy = wlantap.create_phy(config).expect("Failed to create wlantap PHY");
        let event_stream = Some(proxy.take_event_stream());
        let mut helper = TestHelper { _wlantap: wlantap, proxy: Arc::new(proxy), event_stream };
        helper.wait_for_wlanmac_start(exec);
        helper
    }

    fn wait_for_wlanmac_start(&mut self, exec: &mut fasync::Executor) {
        let (sender, receiver) = oneshot::channel::<()>();
        let mut sender = Some(sender);
        self.run(
            exec,
            5.seconds(),
            "receive a WlanmacStart event",
            move |event| match event {
                wlantap::WlantapPhyEvent::WlanmacStart { .. } => {
                    sender.take().map(|s| s.send(()));
                }
                _ => {}
            },
            receiver,
        )
        .unwrap_or_else(|oneshot::Canceled| panic!());
    }

    pub fn proxy(&self) -> Arc<wlantap::WlantapPhyProxy> {
        self.proxy.clone()
    }

    pub fn run<R, F, H>(
        &mut self,
        exec: &mut fasync::Executor,
        timeout: zx::Duration,
        context: &str,
        event_handler: H,
        future: F,
    ) -> R
    where
        H: FnMut(wlantap::WlantapPhyEvent),
        F: Future<Output = R> + Unpin,
    {
        let (item, stream) = exec.run_singlethreaded(
            TestHelperFuture {
                event_stream: Some(self.event_stream.take().unwrap()),
                event_handler,
                main_future: future,
            }
            .on_timeout(timeout.after_now(), || panic!("Did not complete in time: {}", context)),
        );
        self.event_stream = Some(stream);
        item
    }
}

pub struct RetryWithBackoff {
    deadline: zx::Time,
    prev_delay: zx::Duration,
    delay: zx::Duration,
}

impl RetryWithBackoff {
    pub fn new(timeout: zx::Duration) -> Self {
        RetryWithBackoff {
            deadline: zx::Time::after(timeout),
            prev_delay: 0.millis(),
            delay: 1.millis(),
        }
    }

    pub fn sleep_unless_timed_out(&mut self) -> bool {
        if zx::Time::after(0.millis()) > self.deadline {
            false
        } else {
            ::std::cmp::min(zx::Time::after(self.delay), self.deadline).sleep();
            let new_delay = self.prev_delay + self.delay;
            self.prev_delay = self.delay;
            self.delay = new_delay;
            true
        }
    }
}
