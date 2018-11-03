// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The special-purpose event loop used by the recovery netstack.

use ethernet as eth;
use fuchsia_zircon as zx;

use std::fs::File;

use futures::prelude::*;

use netstack_core::{
    receive_frame, set_ip_addr, Context, DeviceId, DeviceLayerEventDispatcher, EventDispatcher,
    Ipv4Addr, Mac, StackState, Subnet, TimerId, TransportLayerEventDispatcher, UdpEventDispatcher,
};

/// The event loop.
pub struct EventLoop {
    ctx: Context<EventLoopInner>,
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
        let mut event_loop = EventLoop {
            ctx: Context::new(
                state,
                EventLoopInner {
                    device_id: eth_id,
                    eth_client,
                },
            ),
        };

        await!(event_loop.ctx.dispatcher().eth_client.start())?;
        // Hardcoded subnet: if you update the IPADDR above to use a network that's not /24, update
        // this as well.
        let fixed_subnet = Subnet::new(Ipv4Addr::new([192, 168, 1, 0]), 24);
        set_ip_addr(&mut event_loop.ctx, eth_id, FIXED_IPADDR, fixed_subnet);

        let mut buf = [0; 2048];
        let mut events = event_loop.ctx.dispatcher().eth_client.get_stream();
        while let Some(evt) = await!(events.try_next())? {
            match evt {
                eth::Event::StatusChanged => {
                    let status = await!(event_loop.ctx.dispatcher().eth_client.get_status())?;
                    println!("ethernet status: {:?}", status);
                }
                eth::Event::Receive(rx) => {
                    let len = rx.read(&mut buf);
                    receive_frame(&mut event_loop.ctx, eth_id, &mut buf[..len]);
                }
            }
        }
        Ok(())
    }
}

struct EventLoopInner {
    device_id: DeviceId,
    eth_client: eth::Client,
}

impl EventDispatcher for EventLoopInner {
    fn schedule_timeout(
        &mut self, _duration: std::time::Duration, _id: TimerId,
    ) -> Option<std::time::Instant> {
        unimplemented!()
    }

    fn schedule_timeout_instant(
        &mut self, _time: std::time::Instant, _id: TimerId,
    ) -> Option<std::time::Instant> {
        unimplemented!()
    }

    fn cancel_timeout(&mut self, id: TimerId) -> Option<std::time::Instant> {
        unimplemented!()
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
