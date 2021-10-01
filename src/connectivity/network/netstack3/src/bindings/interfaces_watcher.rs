// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::VecDeque;

use fidl::prelude::*;
use fidl_fuchsia_hardware_ethernet as fidl_ethernet;
use fidl_fuchsia_hardware_network as fidl_netdev;
use fidl_fuchsia_net_interfaces::{
    self as fidl_interfaces, StateRequest, StateRequestStream, WatcherOptions, WatcherRequest,
    WatcherRequestStream, WatcherWatchResponder,
};
use fidl_fuchsia_net_interfaces_ext as fidl_interfaces_ext;
use fuchsia_zircon as zx;
use futures::{
    ready, sink::Sink, sink::SinkExt as _, task::Poll, Future, StreamExt as _, TryFutureExt as _,
    TryStreamExt as _,
};
use net_types::ip::{Ip as _, Ipv4, Ipv6, Subnet, SubnetEither};
use netstack3_core::{get_all_ip_addr_subnets, get_all_routes, EntryDest};

use super::{util::IntoFidl as _, Devices, StackContext};

/// Possible errors when serving `fuchsia.net.interfaces/State`.
#[derive(thiserror::Error, Debug)]
pub(crate) enum Error {
    #[error("failed to send a Watcher task to parent")]
    Send(#[from] futures::channel::mpsc::SendError),
    #[error(transparent)]
    Fidl(#[from] fidl::Error),
}

/// Serves the `fuchsia.net.interfaces/State` protocol.
pub(crate) async fn serve<C, S>(ctx: C, stream: StateRequestStream, sink: S) -> Result<(), Error>
where
    C: StackContext,
    S: Sink<Watcher> + std::marker::Unpin,
    Error: From<S::Error>,
{
    stream
        .err_into()
        .try_fold((ctx, sink), |(ctx, mut sink), req| async {
            let StateRequest::GetWatcher {
                options: WatcherOptions { .. },
                watcher,
                control_handle: _,
            } = req;
            let (stream, control_handle) = watcher.into_stream_and_control_handle()?;
            match existing_properties(&ctx).await {
                Ok(events) => sink.send(Watcher { events, stream, responder: None }).await?,
                Err(err) => control_handle.shutdown_with_epitaph(err),
            }
            Ok((ctx, sink))
        })
        .map_ok(|_: (C, S)| ())
        .await
}

async fn existing_properties(ctx: &impl StackContext) -> Result<EventQueue, zx::Status> {
    let ctx = ctx.lock().await;
    let existing = AsRef::<Devices>::as_ref(ctx.dispatcher()).iter_devices().map(|info| {
        let features = info.features();
        // TODO(https://fxbug.dev/84863): rewrite features in terms of fuchsia.hardware.network.
        let device_class = if features.contains(fidl_ethernet::Features::Loopback) {
            fidl_interfaces::DeviceClass::Loopback(fidl_interfaces::Empty)
        } else if features.contains(fidl_ethernet::Features::Wlan) {
            fidl_interfaces::DeviceClass::Device(fidl_netdev::DeviceClass::Wlan)
        } else {
            fidl_interfaces::DeviceClass::Device(fidl_netdev::DeviceClass::Ethernet)
        };
        let addrs = info
            .core_id()
            .map(|id| {
                get_all_ip_addr_subnets(&ctx, id)
                    .map(|subnet| fidl_interfaces_ext::Address {
                        addr: subnet.into_fidl(),
                        valid_until: zx::sys::ZX_TIME_INFINITE,
                    })
                    .collect::<Vec<fidl_interfaces_ext::Address>>()
            })
            .unwrap_or(Vec::new());

        let (has_default_ipv4_route, has_default_ipv6_route) = {
            let mut has_default_ipv4_route = false;
            let mut has_default_ipv6_route = false;
            if let Some(id) = info.core_id() {
                let default_ipv4_dest = Subnet::new(Ipv4::UNSPECIFIED_ADDRESS, 0).unwrap();
                let default_ipv6_dest = Subnet::new(Ipv6::UNSPECIFIED_ADDRESS, 0).unwrap();
                for r in get_all_routes(&ctx) {
                    let (subnet, dest) = r.into_subnet_dest();
                    match dest {
                        EntryDest::Remote { next_hop: _ } => {}
                        EntryDest::Local { device } => {
                            if device != id {
                                continue;
                            }
                            match subnet {
                                SubnetEither::V4(subnet) => {
                                    if subnet == default_ipv4_dest {
                                        has_default_ipv4_route = true;
                                    }
                                }
                                SubnetEither::V6(subnet) => {
                                    if subnet == default_ipv6_dest {
                                        has_default_ipv6_route = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            (has_default_ipv4_route, has_default_ipv6_route)
        };

        fidl_interfaces_ext::Properties {
            id: info.id(),
            addresses: addrs,
            online: info.admin_enabled() && info.phy_up(),
            device_class,
            has_default_ipv4_route,
            has_default_ipv6_route,
            // TODO(https://fxbug.dev/84516): populate interface name.
            name: format!("[TBD]-{}", info.id()),
        }
    });
    EventQueue::existing(existing)
}

/// The maximum events to buffer at server side before the client consumes them.
///
/// The value is currently kept in sync with the netstack2 implementation.
const MAX_EVENTS: usize = 128;

/// A bounded queue of [`Events`] for `fuchsia.net.interfaces/Watcher` protocol.
struct EventQueue {
    events: VecDeque<fidl_interfaces::Event>,
}

impl EventQueue {
    /// Creates a queue with all the existing [`Properties`] and an [`Event::Idle`].
    // TODO(https://fxbug.dev/84866): Make `existing` an `ExactSizeIterator`.
    fn existing(
        existing: impl IntoIterator<Item = fidl_interfaces_ext::Properties>,
    ) -> Result<Self, zx::Status> {
        let events = existing
            .into_iter()
            .map(|p| fidl_interfaces::Event::Existing(p.into()))
            .chain(std::iter::once(fidl_interfaces::Event::Idle(fidl_interfaces::Empty)))
            .collect::<VecDeque<fidl_interfaces::Event>>();
        if events.len() > MAX_EVENTS {
            return Err(zx::Status::BUFFER_TOO_SMALL);
        }
        Ok(EventQueue { events })
    }

    /// Removes an [`Event`] from the front of the queue.
    fn pop_front(&mut self) -> Option<fidl_interfaces::Event> {
        let Self { events } = self;
        events.pop_front()
    }
}

/// The task that serves `fuchsia.net.interfaces/Watcher`.
#[must_use = "futures do nothing unless you `.await` or poll them"]
pub(crate) struct Watcher {
    stream: WatcherRequestStream,
    events: EventQueue,
    responder: Option<WatcherWatchResponder>,
}

impl Future for Watcher {
    type Output = Result<(), fidl::Error>;

    fn poll(
        mut self: std::pin::Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
    ) -> std::task::Poll<Self::Output> {
        loop {
            let next_request = self.as_mut().stream.poll_next_unpin(cx)?;
            match ready!(next_request) {
                Some(WatcherRequest::Watch { responder }) => match self.events.pop_front() {
                    Some(mut e) => responder_send!(responder, &mut e),
                    None => match &self.responder {
                        Some(existing) => {
                            existing
                                .control_handle()
                                .shutdown_with_epitaph(zx::Status::ALREADY_EXISTS);
                            return Poll::Ready(Ok(()));
                        }
                        None => {
                            // TODO(https://fxbug.dev/75553): Support events other
                            // than Existing and Idle.
                            responder
                                .control_handle()
                                .shutdown_with_epitaph(zx::Status::NOT_SUPPORTED);
                            log::error!("The client tried to hanging-get new events while it is not supported (https://fxbug.dev/75553)");
                            self.responder = Some(responder);
                        }
                    },
                },
                None => return Poll::Ready(Ok(())),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_events_idle() {
        let mut events = EventQueue::existing(std::iter::empty()).expect("failed to create Events");
        assert_eq!(events.pop_front(), Some(fidl_interfaces::Event::Idle(fidl_interfaces::Empty)));
        assert_eq!(events.pop_front(), None);
    }
}
