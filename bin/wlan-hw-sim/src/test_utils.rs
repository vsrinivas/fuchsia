// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async::{self, TimeoutExt, temp::TempStreamExt};
use wlantap;
use futures::prelude::*;
use futures::channel::mpsc;
use std::sync::Arc;
use wlantap_client::Wlantap;
use zx::{self, prelude::*};
use futures::task::Context;
use std::marker::Unpin;
use std::mem::PinMut;

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
{}

impl<T, E, F, H> Future for TestHelperFuture<F, H>
where
    F: Future<Output = Result<T, E>> + Unpin,
    H: FnMut(wlantap::WlantapPhyEvent),
{
    type Output = Result<(T, EventStream), (E, EventStream)>;

    fn poll(mut self: PinMut<Self>, cx: &mut Context) -> Poll<Self::Output> {
        let this = &mut *self;
        match this.main_future.poll_unpin(cx) {
            Poll::Ready(Err(e)) => Poll::Ready(Err((e, this.event_stream.take().unwrap()))),
            Poll::Ready(Ok(item)) => Poll::Ready(Ok((item, this.event_stream.take().unwrap()))),
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
    pub fn begin_test(exec: &mut async::Executor,
                      config: wlantap::WlantapPhyConfig) -> Self {
        let wlantap = Wlantap::open().expect("Failed to open wlantapctl");
        let proxy = wlantap.create_phy(config).expect("Failed to create wlantap PHY");
        let event_stream = Some(proxy.take_event_stream());
        let mut helper = TestHelper {
            _wlantap: wlantap,
            proxy: Arc::new(proxy),
            event_stream
        };
        helper.wait_for_wlanmac_start(exec);
        helper
    }

    fn wait_for_wlanmac_start(&mut self, exec: &mut async::Executor) {
        let (mut sender, receiver) = mpsc::channel::<()>(1);
        self.run(exec, 5.seconds(), "receive a WlanmacStart event",
            move |event| {
                match event {
                    wlantap::WlantapPhyEvent::WlanmacStart{ .. } => {
                        sender.try_send(()).unwrap();
                    },
                    _ => {}
                }
            },
            receiver.map(Ok).try_into_future()
        ).unwrap_or_else(|()| unreachable!());
    }

    pub fn proxy(&self) -> Arc<wlantap::WlantapPhyProxy> {
        self.proxy.clone()
    }

    pub fn run<T, E, F, H>(&mut self, exec: &mut async::Executor, timeout: zx::Duration,
                             context: &str, event_handler: H, future: F)
        -> Result<T, E>
    where
        H: FnMut(wlantap::WlantapPhyEvent),
        F: Future<Output = Result<T, E>> + Unpin,
    {
        let res = exec.run_singlethreaded(
            TestHelperFuture {
                event_stream: Some(self.event_stream.take().unwrap()),
                event_handler,
                main_future: future,
            }
            .on_timeout(timeout.after_now(),
                        || panic!("Did not complete in time: {}", context)));
        match res {
            Ok((item, stream)) => {
                self.event_stream = Some(stream);
                Ok(item)
            },
            Err((err, stream)) => {
                self.event_stream = Some(stream);
                Err(err)
            }
        }
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
            deadline: timeout.after_now(),
            prev_delay: 0.millis(),
            delay: 1.millis(),
        }
    }

    pub fn sleep_unless_timed_out(&mut self) -> bool {
        if 0.millis().after_now() > self.deadline {
            false
        } else {
            ::std::cmp::min(self.delay.after_now(), self.deadline).sleep();
            let new_delay = self.prev_delay + self.delay;
            self.prev_delay = self.delay;
            self.delay = new_delay;
            true
        }
    }
}
