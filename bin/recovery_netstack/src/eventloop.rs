// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The special-purpose event loop used by the recovery netstack.

#![allow(unused)]

use ethernet as eth;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;

use std::fs::File;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use futures::future::{AbortHandle, Abortable};
use futures::prelude::*;

use netstack_core::{
    add_device_route, handle_timeout, receive_frame, set_ip_addr, Context, DeviceId,
    DeviceLayerEventDispatcher, EventDispatcher, Ipv4Addr, Mac, StackState, Subnet, TimerId,
    TransportLayerEventDispatcher, UdpEventDispatcher,
};

/// The event loop.
pub struct EventLoop {
    ctx: Arc<Mutex<Context<EventLoopInner>>>,
}

pub const DEFAULT_ETH: &str = "/dev/class/ethernet/000";

impl EventLoop {
    /// Run a dummy event loop.
    ///
    /// This function hard-codes way too many things, and will soon be replaced
    /// with a more general-purpose mechanism.
    pub async fn run_ethernet(path: &str) -> Result<(), failure::Error> {
        // Hardcoded IPv4 address: if you use something other than a /24, update the subnet below
        // as well.
        const FIXED_IPADDR: Ipv4Addr = Ipv4Addr::new([192, 168, 1, 39]);

        let vmo = zx::Vmo::create_with_opts(
            zx::VmoOptions::NON_RESIZABLE,
            256 * eth::DEFAULT_BUFFER_SIZE as u64,
        )?;

        let dev = File::open(path)?;

        let eth_client = await!(eth::Client::from_file(
            dev,
            vmo,
            eth::DEFAULT_BUFFER_SIZE,
            "recovery-ns"
        ))?;
        let mac = await!(eth_client.info())?.mac;
        let mut state = StackState::default();
        let eth_id = state.add_ethernet_device(Mac::new(mac.octets));
        let event_loop = EventLoop {
            ctx: Arc::new(Mutex::new(Context::new(
                state,
                EventLoopInner {
                    device_id: eth_id,
                    eth_client,
                    event_loop: None,
                    timers: vec![],
                },
            ))),
        };
        event_loop.ctx.lock().unwrap().dispatcher().event_loop = Some(Arc::clone(&event_loop.ctx));

        {
            let mut ctx = event_loop.ctx.lock().unwrap();

            await!(ctx.dispatcher().eth_client.start())?;
            // Hardcoded subnet: if you update the IPADDR above to use a network that's not /24, update
            // this as well.
            let fixed_subnet = Subnet::new(Ipv4Addr::new([192, 168, 1, 0]), 24);
            set_ip_addr(&mut ctx, eth_id, FIXED_IPADDR, fixed_subnet);
            add_device_route(&mut ctx, fixed_subnet, eth_id);
        }

        let mut buf = [0; 2048];
        let mut events = event_loop
            .ctx
            .lock()
            .unwrap()
            .dispatcher()
            .eth_client
            .get_stream();
        while let Some(evt) = await!(events.try_next())? {
            match evt {
                eth::Event::StatusChanged => {
                    let mut ctx = event_loop.ctx.lock().unwrap();
                    let status = await!(ctx.dispatcher().eth_client.get_status())?;
                    println!("ethernet status: {:?}", status);
                }
                eth::Event::Receive(rx, _flags) => {
                    let len = rx.read(&mut buf);
                    receive_frame(&mut event_loop.ctx.lock().unwrap(), eth_id, &mut buf[..len]);
                }
            }
        }
        Ok(())
    }
}

struct TimerInfo {
    time: Instant,
    id: TimerId,
    abort_handle: AbortHandle,
}

struct EventLoopInner {
    device_id: DeviceId,
    eth_client: eth::Client,
    // This creates an Arc cycle.
    // This is difficult to avoid, since the event loop needs to be able to pass the timeout
    // handlers a mutable reference to the event loop itself, so that they can send packets/etc.
    // The Arc cycle shouldn't be a problem, however, since we expect the event loop to live for
    // as long as the netstack is alive.
    // The Option is just to allow a concrete instance of this type to be created - the creator is
    // expected to uphold the invariant that event_loop is not None by the time the EventLoopInner
    // is used.
    event_loop: Option<Arc<Mutex<Context<EventLoopInner>>>>,
    timers: Vec<TimerInfo>,
}

impl EventDispatcher for EventLoopInner {
    fn schedule_timeout(&mut self, duration: Duration, id: TimerId) -> Option<Instant> {
        // We need to separately keep track of the time at which the future completes (a Zircon
        // Time object) and the time at which the user expects it to complete. You cannot convert
        // between Zircon Time objects and std::time::Instant objects (since std::time::Instance is
        // opaque), so we generate two different time objects to keep track of.
        let zircon_time = fuchsia_zircon::Time::after(fuchsia_zircon::Duration::from(duration));
        let rust_time = Instant::now() + duration;

        let old_timer = self.cancel_timeout(id);

        let event_loop = Arc::clone(self.event_loop.as_ref().unwrap());
        let timeout = async move {
            await!(fasync::Timer::new(zircon_time));
            handle_timeout(&mut event_loop.lock().unwrap(), id);
            event_loop.lock().unwrap().dispatcher().cancel_timeout(id);
        };

        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        self.timers.push(TimerInfo {
            time: rust_time,
            id,
            abort_handle,
        });

        let timeout = Abortable::new(timeout, abort_registration);
        let timeout = timeout.unwrap_or_else(|_| ());

        fasync::spawn_local(timeout);
        old_timer
    }

    fn schedule_timeout_instant(&mut self, _time: Instant, _id: TimerId) -> Option<Instant> {
        // It's not possible to convert a std::time::Instant to a Zircon Time, so this API will
        // need some more thought. Punting on this until we need it.
        unimplemented!()
    }

    fn cancel_timeout(&mut self, id: TimerId) -> Option<Instant> {
        let index = self.timers.iter().enumerate().find_map(|x| {
            if x.1.id == id {
                Some(x.0)
            } else {
                None
            }
        })?;
        Some(self.timers.remove(index).time)
    }
}

impl DeviceLayerEventDispatcher for EventLoopInner {
    fn send_frame(&mut self, device: DeviceId, frame: &[u8]) {
        // TODO(joshlf): Handle more than one device
        assert_eq!(device, self.device_id);
        self.eth_client.send(&frame);
    }
}

impl UdpEventDispatcher for EventLoopInner {
    type UdpConn = ();
    type UdpListener = ();
}

impl TransportLayerEventDispatcher for EventLoopInner {}
