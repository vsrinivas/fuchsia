// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async::{self, TimeoutExt};
use wlantap;
use futures::prelude::*;
use futures::channel::mpsc;
use std::sync::Arc;
use wlantap_client::Wlantap;
use zx::{self, prelude::*};
use futures::task::Context;

type EventStream = wlantap::WlantapPhyEventStream;

pub struct TestHelper {
    _wlantap: Wlantap,
    proxy: Arc<wlantap::WlantapPhyProxy>,
    event_stream: Option<EventStream>,
}

struct TestHelperFuture<F: Future, H>
    where H: FnMut(wlantap::WlantapPhyEvent) -> ()
{
    event_stream: Option<EventStream>,
    event_handler: H,
    main_future: F,
}

impl<F: Future, H> Future for TestHelperFuture<F, H>
    where H: FnMut(wlantap::WlantapPhyEvent) -> ()
{
    type Item = (F::Item, EventStream);
    type Error = (F::Error, EventStream);

    fn poll(&mut self, cx: &mut Context) -> Poll<(F::Item, EventStream), (F::Error, EventStream)> {
        match self.main_future.poll(cx) {
            Err(e) => Err((e, self.event_stream.take().unwrap())),
            Ok(Async::Ready(item)) => Ok(Async::Ready((item, self.event_stream.take().unwrap()))),
            Ok(Async::Pending) => {
                let stream = self.event_stream.as_mut().unwrap();
                loop {
                    match stream.poll_next(cx) {
                        Err(e) => panic!("WlantapPhy event stream returned an error: {:?}", e),
                        Ok(Async::Ready(None)) => panic!("Unexpected end of the WlantapPhy event stream"),
                        Ok(Async::Ready(Some(event))) => (self.event_handler)(event),
                        Ok(Async::Pending) => return Ok(Async::Pending),
                    }
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
            receiver.next()
        ).unwrap();
    }

    pub fn proxy(&self) -> Arc<wlantap::WlantapPhyProxy> {
        self.proxy.clone()
    }

    pub fn run<F: Future, H>(&mut self, exec: &mut async::Executor, timeout: zx::Duration,
                             context: &str, event_handler: H, future: F)
        -> Result<F::Item, F::Error>
        where H: FnMut(wlantap::WlantapPhyEvent) -> ()
    {
        let res = exec.run_singlethreaded(
            TestHelperFuture{
                event_stream: Some(self.event_stream.take().unwrap()),
                event_handler,
                main_future: future
            }
            .on_timeout(timeout.after_now(),
                        || panic!("Did not complete in time: {}", context)).unwrap());
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
