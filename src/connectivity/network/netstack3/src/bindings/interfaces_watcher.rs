// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{HashMap, VecDeque};

use super::{devices::BindingId, util::IntoFidl};
use fidl::prelude::*;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_interfaces::{
    self as finterfaces, StateRequest, StateRequestStream, WatcherOptions, WatcherRequest,
    WatcherRequestStream, WatcherWatchResponder,
};
use fidl_fuchsia_net_interfaces_ext as finterfaces_ext;
use fuchsia_zircon as zx;
use futures::{
    channel::mpsc, ready, sink::SinkExt as _, task::Poll, Future, FutureExt as _, StreamExt as _,
    TryFutureExt as _, TryStreamExt as _,
};
use net_types::ip::{AddrSubnetEither, IpAddr, IpVersion};
use netstack3_core::ip::device::IpAddressState;

/// Possible errors when serving `fuchsia.net.interfaces/State`.
#[derive(thiserror::Error, Debug)]
pub(crate) enum Error {
    #[error("failed to send a Watcher task to parent")]
    Send(#[from] WorkerClosedError),
    #[error(transparent)]
    Fidl(#[from] fidl::Error),
}

/// Serves the `fuchsia.net.interfaces/State` protocol.
pub(crate) async fn serve(
    stream: StateRequestStream,
    sink: WorkerWatcherSink,
) -> Result<(), Error> {
    stream
        .err_into()
        .try_fold(sink, |mut sink, req| async move {
            let _ = &req;
            let StateRequest::GetWatcher {
                options: WatcherOptions { .. },
                watcher,
                control_handle: _,
            } = req;
            let watcher = watcher.into_stream()?;
            sink.add_watcher(watcher).await?;
            Ok(sink)
        })
        .map_ok(|_: WorkerWatcherSink| ())
        .await
}

/// The maximum events to buffer at server side before the client consumes them.
///
/// The value is currently kept in sync with the netstack2 implementation.
const MAX_EVENTS: usize = 128;

#[derive(Debug)]
/// A bounded queue of [`Events`] for `fuchsia.net.interfaces/Watcher` protocol.
struct EventQueue {
    events: VecDeque<finterfaces::Event>,
}

impl EventQueue {
    /// Creates a new event queue containing all the interfaces in `state`
    /// wrapped in a [`finterfaces::Event::Existing`] followed by a
    /// [`finterfaces::Event::Idle`].
    fn from_state(state: &HashMap<BindingId, InterfaceState>) -> Result<Self, zx::Status> {
        // NB: Leave room for idle event.
        if state.len() >= MAX_EVENTS {
            return Err(zx::Status::BUFFER_TOO_SMALL);
        }
        // NB: Compiler can't infer the parameter types.
        let state_to_event = |(id, state): (&u64, &InterfaceState)| {
            let InterfaceState {
                properties: InterfaceProperties { name, device_class },
                addresses,
                has_default_ipv4_route,
                has_default_ipv6_route,
                online,
            } = state;
            finterfaces::Event::Existing(
                finterfaces_ext::Properties {
                    id: *id,
                    name: name.clone(),
                    device_class: device_class.clone(),
                    online: *online,
                    addresses: Worker::collect_addresses(addresses),
                    has_default_ipv4_route: *has_default_ipv4_route,
                    has_default_ipv6_route: *has_default_ipv6_route,
                }
                .into(),
            )
        };
        Ok(Self {
            events: state
                .iter()
                .map(state_to_event)
                .chain(std::iter::once(finterfaces::Event::Idle(finterfaces::Empty {})))
                .collect(),
        })
    }

    /// Adds a [`finterfaces::Event`] to the back of the queue.
    fn push(&mut self, event: finterfaces::Event) -> Result<(), finterfaces::Event> {
        let Self { events } = self;
        if events.len() >= MAX_EVENTS {
            return Err(event);
        }
        // NB: We could perform event consolidation here, but that's not
        // implemented in NS2 at the moment of writing so it's easier to match
        // behavior.
        events.push_back(event);
        Ok(())
    }

    /// Removes an [`Event`] from the front of the queue.
    fn pop_front(&mut self) -> Option<finterfaces::Event> {
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
                    Some(mut e) => {
                        responder_send!(responder, &mut e)
                    }
                    None => match &self.responder {
                        Some(existing) => {
                            existing
                                .control_handle()
                                .shutdown_with_epitaph(zx::Status::ALREADY_EXISTS);
                            return Poll::Ready(Ok(()));
                        }
                        None => {
                            self.responder = Some(responder);
                        }
                    },
                },
                None => return Poll::Ready(Ok(())),
            }
        }
    }
}

impl Watcher {
    fn push(&mut self, mut event: finterfaces::Event) {
        let Self { stream, events, responder } = self;
        if let Some(responder) = responder.take() {
            match responder.send(&mut event) {
                Ok(()) => (),
                Err(e) if e.is_closed() => (),
                Err(e) => log::error!("error sending event {:?} to watcher: {:?}", event, e),
            }
            return;
        }
        match events.push(event) {
            Ok(()) => (),
            Err(event) => {
                log::warn!("failed to enqueue event {:?} on watcher, closing channel", event);
                stream.control_handle().shutdown();
            }
        }
    }
}

/// Interface specific events.
#[derive(Debug)]
#[cfg_attr(test, derive(Clone, Eq, PartialEq))]
pub enum InterfaceUpdate {
    AddressAdded { addr: AddrSubnetEither, assignment_state: IpAddressState, valid_until: zx::Time },
    AddressAssignmentStateChanged { addr: IpAddr, new_state: IpAddressState },
    AddressRemoved(IpAddr),
    DefaultRouteChanged { version: IpVersion, has_default_route: bool },
    OnlineChanged(bool),
}

/// Immutable interface properties.
#[derive(Debug)]
#[cfg_attr(test, derive(Clone, Eq, PartialEq))]
pub struct InterfaceProperties {
    pub name: String,
    pub device_class: finterfaces::DeviceClass,
}

/// Cached interface state by the worker.
#[derive(Debug)]
#[cfg_attr(test, derive(Clone, Eq, PartialEq))]
pub struct InterfaceState {
    properties: InterfaceProperties,
    online: bool,
    addresses: HashMap<IpAddr, AddressProperties>,
    has_default_ipv4_route: bool,
    has_default_ipv6_route: bool,
}

#[derive(Debug)]
#[cfg_attr(test, derive(Clone, Eq, PartialEq))]
struct AddressProperties {
    prefix_len: u8,
    state: AddressState,
}

/// Cached address state by the worker.
#[derive(Debug)]
#[cfg_attr(test, derive(Clone, Eq, PartialEq))]
pub struct AddressState {
    pub valid_until: zx::Time,
    // Whether or not the Address should be published to clients. This is a
    // rough proxy of the address's [`IpAddressState`]: `Assigned` addresses are
    // visible whereas `Tentative` addresses are not. Note that when addresses
    // become `Unavailable` they should retain their previous visibility:
    // visible if the address was previously `Assigned` and invisible if
    // previously `Tentative`.
    pub visible: bool,
}

#[derive(Debug)]
#[cfg_attr(test, derive(Clone))]
pub(crate) enum InterfaceEvent {
    Added { id: BindingId, properties: InterfaceProperties },
    Changed { id: BindingId, event: InterfaceUpdate },
    Removed(BindingId),
}

#[derive(Debug)]
pub struct InterfaceEventProducer {
    id: BindingId,
    channel: mpsc::UnboundedSender<InterfaceEvent>,
}

impl InterfaceEventProducer {
    /// Notifies the interface state [`Worker`] of [`event`] on this
    /// [`InterfaceEventProducer`]'s interface.
    pub fn notify(&self, event: InterfaceUpdate) -> Result<(), InterfaceUpdate> {
        let Self { id, channel } = self;
        channel.unbounded_send(InterfaceEvent::Changed { id: *id, event }).map_err(|e| {
            match e.into_inner() {
                InterfaceEvent::Changed { id: _, event } => event,
                // All other patterns are unreachable, this is only so we can get
                // back the event we just created above.
                e => unreachable!("{:?}", e),
            }
        })
    }

    #[cfg(test)]
    pub(crate) fn new(
        id: BindingId,
        channel: mpsc::UnboundedSender<InterfaceEvent>,
    ) -> InterfaceEventProducer {
        InterfaceEventProducer { id, channel }
    }
}

impl Drop for InterfaceEventProducer {
    fn drop(&mut self) {
        let Self { id, channel } = self;
        channel.unbounded_send(InterfaceEvent::Removed(*id)).unwrap_or_else(
            |_: mpsc::TrySendError<_>| {
                // If the worker was closed before its producers we can assume
                // it's no longer interested in events, so we simply drop these
                // errors.
            },
        )
    }
}

#[derive(thiserror::Error, Debug)]
#[cfg_attr(test, derive(Eq, PartialEq))]
pub enum WorkerError {
    #[error("attempted to reinsert interface {interface} over old {old:?}")]
    AddedDuplicateInterface { interface: BindingId, old: InterfaceState },
    #[error("attempted to remove nonexisting interface with id {0}")]
    RemoveNonexistentInterface(BindingId),
    #[error("attempted to update nonexisting interface with id {0}")]
    UpdateNonexistentInterface(BindingId),
    #[error("attempted to assign already assigned address {addr} on interface {interface}")]
    AssignExistingAddr { interface: BindingId, addr: IpAddr },
    #[error("attempted to unassign nonexisting interface address {addr} on interface {interface}")]
    UnassignNonexistentAddr { interface: BindingId, addr: IpAddr },
    #[error("attempted to update assignment state to {state:?} on non existing interface address {addr} on interface {interface}")]
    UpdateStateOnNonexistentAddr { interface: BindingId, addr: IpAddr, state: IpAddressState },
}

pub struct Worker {
    events: mpsc::UnboundedReceiver<InterfaceEvent>,
    watchers: mpsc::Receiver<finterfaces::WatcherRequestStream>,
}
/// Arbitrarily picked constant to force backpressure on FIDL requests.
const WATCHER_CHANNEL_CAPACITY: usize = 32;

impl Worker {
    pub fn new() -> (Worker, WorkerWatcherSink, WorkerInterfaceSink) {
        let (events_sender, events_receiver) = mpsc::unbounded();
        let (watchers_sender, watchers_receiver) = mpsc::channel(WATCHER_CHANNEL_CAPACITY);
        (
            Worker { events: events_receiver, watchers: watchers_receiver },
            WorkerWatcherSink { sender: watchers_sender },
            WorkerInterfaceSink { sender: events_sender },
        )
    }

    /// Runs the worker until all [`WorkerWatcherSink`]s and
    /// [`WorkerInterfaceSink`]s are closed.
    ///
    /// On success, returns the set of currently opened [`Watcher`]s that the
    /// `Worker` was polling on when all its sinks were closed.
    pub(crate) async fn run(
        self,
    ) -> Result<futures::stream::FuturesUnordered<Watcher>, WorkerError> {
        let Self { events, watchers: watchers_stream } = self;
        let mut current_watchers = futures::stream::FuturesUnordered::<Watcher>::new();
        let mut interface_state = HashMap::new();

        enum SinkAction {
            NewWatcher(finterfaces::WatcherRequestStream),
            Event(InterfaceEvent),
        }
        let mut sink_actions = futures::stream::select_with_strategy(
            watchers_stream.map(SinkAction::NewWatcher),
            events.map(SinkAction::Event),
            // Always consume events before watchers. That allows external
            // observers to assume all side effects of a call are already
            // applied before a watcher observes its initial existing set of
            // properties.
            |_: &mut ()| futures::stream::PollNext::Right,
        );

        loop {
            let mut poll_watchers = if current_watchers.is_empty() {
                futures::future::pending().left_future()
            } else {
                current_watchers.by_ref().next().right_future()
            };

            // NB: Declare an enumeration with actions to prevent too much logic
            // in select macro.
            enum Action {
                WatcherEnded(Option<Result<(), fidl::Error>>),
                Sink(Option<SinkAction>),
            }
            let action = futures::select! {
                r = poll_watchers => Action::WatcherEnded(r),
                a = sink_actions.next() => Action::Sink(a),
            };

            match action {
                Action::WatcherEnded(r) => match r {
                    Some(Ok(())) => {}
                    Some(Err(e)) => {
                        if !e.is_closed() {
                            log::error!("error operating interface watcher {:?}", e);
                        }
                    }
                    // This should not be observable since we check if our
                    // watcher collection is empty above and replace it with a
                    // pending future.
                    None => unreachable!("should not observe end of FuturesUnordered"),
                },
                Action::Sink(Some(SinkAction::NewWatcher(stream))) => {
                    match EventQueue::from_state(&interface_state) {
                        Ok(events) => {
                            current_watchers.push(Watcher { stream, events, responder: None })
                        }
                        Err(status) => {
                            log::warn!("failed to construct events for watcher: {}", status);
                            stream.control_handle().shutdown_with_epitaph(status);
                        }
                    }
                }
                Action::Sink(Some(SinkAction::Event(e))) => {
                    log::debug!("consuming event {:?}", e);
                    if let Some(event) = Self::consume_event(&mut interface_state, e)? {
                        current_watchers.iter_mut().for_each(|watcher| watcher.push(event.clone()));
                    }
                }
                // If all of the sinks close, shutdown the worker.
                Action::Sink(None) => {
                    return Ok(current_watchers);
                }
            }
        }
    }

    /// Consumes a single worker event, mutating state.
    ///
    /// On `Err`, the worker must be stopped and `state` can't be considered
    /// valid anymore.
    fn consume_event(
        state: &mut HashMap<BindingId, InterfaceState>,
        event: InterfaceEvent,
    ) -> Result<Option<finterfaces::Event>, WorkerError> {
        match event {
            InterfaceEvent::Added {
                id,
                properties: InterfaceProperties { name, device_class },
            } => {
                let online = false;
                let has_default_ipv4_route = false;
                let has_default_ipv6_route = false;
                match state.insert(
                    id,
                    InterfaceState {
                        properties: InterfaceProperties { name: name.clone(), device_class },
                        online,
                        addresses: HashMap::new(),
                        has_default_ipv4_route,
                        has_default_ipv6_route,
                    },
                ) {
                    Some(old) => Err(WorkerError::AddedDuplicateInterface { interface: id, old }),
                    None => Ok(Some(finterfaces::Event::Added(
                        finterfaces_ext::Properties {
                            id,
                            name,
                            device_class,
                            online,
                            addresses: Vec::new(),
                            has_default_ipv4_route,
                            has_default_ipv6_route,
                        }
                        .into(),
                    ))),
                }
            }
            InterfaceEvent::Removed(rm) => match state.remove(&rm) {
                Some(InterfaceState { .. }) => Ok(Some(finterfaces::Event::Removed(rm))),
                None => Err(WorkerError::RemoveNonexistentInterface(rm)),
            },
            InterfaceEvent::Changed { id, event } => {
                let InterfaceState {
                    properties: _,
                    online,
                    addresses,
                    has_default_ipv4_route,
                    has_default_ipv6_route,
                } = state
                    .get_mut(&id)
                    .ok_or_else(|| WorkerError::UpdateNonexistentInterface(id))?;
                match event {
                    InterfaceUpdate::AddressAdded { addr, assignment_state, valid_until } => {
                        let (addr, prefix_len) = addr.addr_prefix();
                        let addr = *addr;
                        let visible = match assignment_state {
                            IpAddressState::Assigned | IpAddressState::Unavailable => true,
                            IpAddressState::Tentative => false,
                        };
                        match addresses.insert(
                            addr,
                            AddressProperties {
                                prefix_len,
                                state: AddressState { visible, valid_until },
                            },
                        ) {
                            Some(AddressProperties { .. }) => {
                                Err(WorkerError::AssignExistingAddr { interface: id, addr })
                            }
                            None => {
                                if visible {
                                    Ok(Some(finterfaces::Event::Changed(finterfaces::Properties {
                                        id: Some(id),
                                        addresses: Some(Self::collect_addresses(addresses)),
                                        ..finterfaces::Properties::EMPTY
                                    })))
                                } else {
                                    Ok(None)
                                }
                            }
                        }
                    }
                    InterfaceUpdate::AddressAssignmentStateChanged { addr, new_state } => {
                        let AddressProperties {
                            prefix_len: _,
                            state: AddressState { visible, valid_until: _ },
                        } = addresses.get_mut(&addr).ok_or_else(|| {
                            WorkerError::UpdateStateOnNonexistentAddr {
                                interface: id,
                                addr,
                                state: new_state,
                            }
                        })?;
                        let new_visibility = match new_state {
                            IpAddressState::Assigned => true,
                            IpAddressState::Tentative => false,
                            // Keep the old visibility when addresses become
                            // `Unavailable`. If the address was `Assigned`, it
                            // will remain visible, and if the address was
                            // `Tentative` it will remain invisible.
                            IpAddressState::Unavailable => *visible,
                        };

                        if *visible == new_visibility {
                            return Ok(None);
                        }
                        *visible = new_visibility;
                        Ok(Some(finterfaces::Event::Changed(finterfaces::Properties {
                            id: Some(id),
                            addresses: Some(Self::collect_addresses(addresses)),
                            ..finterfaces::Properties::EMPTY
                        })))
                    }
                    InterfaceUpdate::AddressRemoved(addr) => match addresses.remove(&addr) {
                        Some(AddressProperties {
                            prefix_len: _,
                            state: AddressState { visible, valid_until: _ },
                        }) => {
                            if visible {
                                Ok(Some(finterfaces::Event::Changed(finterfaces::Properties {
                                    id: Some(id),
                                    addresses: (Some(Self::collect_addresses(addresses))),
                                    ..finterfaces::Properties::EMPTY
                                })))
                            } else {
                                Ok(None)
                            }
                        }
                        None => Err(WorkerError::UnassignNonexistentAddr { interface: id, addr }),
                    },
                    InterfaceUpdate::DefaultRouteChanged {
                        version,
                        has_default_route: new_value,
                    } => {
                        let mut table = finterfaces::Properties {
                            id: Some(id),
                            ..finterfaces::Properties::EMPTY
                        };
                        let (state, prop) = match version {
                            IpVersion::V4 => {
                                (has_default_ipv4_route, &mut table.has_default_ipv4_route)
                            }
                            IpVersion::V6 => {
                                (has_default_ipv6_route, &mut table.has_default_ipv6_route)
                            }
                        };
                        Ok((*state != new_value)
                            .then(|| {
                                *state = new_value;
                                *prop = Some(new_value);
                            })
                            .map(move |()| finterfaces::Event::Changed(table)))
                    }
                    InterfaceUpdate::OnlineChanged(new_online) => {
                        Ok((*online != new_online).then(|| {
                            *online = new_online;
                            finterfaces::Event::Changed(finterfaces::Properties {
                                id: Some(id),
                                online: Some(new_online),
                                ..finterfaces::Properties::EMPTY
                            })
                        }))
                    }
                }
            }
        }
    }

    fn collect_addresses<T: SortableInterfaceAddress>(
        addrs: &HashMap<IpAddr, AddressProperties>,
    ) -> Vec<T> {
        let mut addrs = addrs
            .iter()
            .filter_map(
                |(
                    addr,
                    AddressProperties { prefix_len, state: AddressState { valid_until, visible } },
                )| {
                    if *visible {
                        Some(
                            finterfaces_ext::Address {
                                addr: fnet::Subnet {
                                    addr: addr.into_fidl(),
                                    prefix_len: *prefix_len,
                                },
                                valid_until: valid_until.into_nanos(),
                            }
                            .into(),
                        )
                    } else {
                        None
                    }
                },
            )
            .collect::<Vec<T>>();
        // Provide a stably ordered vector of addresses.
        addrs.sort_by_key(|addr| addr.get_sort_key());
        addrs
    }
}

/// This trait enables the implementation of [`Worker::collect_addresses`] to be
/// agnostic to extension and pure FIDL types, it is not meant to be used in
/// other contexts.
trait SortableInterfaceAddress: From<finterfaces_ext::Address> {
    type Key: Ord;
    fn get_sort_key(&self) -> Self::Key;
}

impl SortableInterfaceAddress for finterfaces_ext::Address {
    type Key = fnet::Subnet;
    fn get_sort_key(&self) -> fnet::Subnet {
        self.addr.clone()
    }
}

impl SortableInterfaceAddress for finterfaces::Address {
    type Key = Option<fnet::Subnet>;
    fn get_sort_key(&self) -> Option<fnet::Subnet> {
        self.addr.clone()
    }
}

#[derive(thiserror::Error, Debug)]
#[error("Connection to interfaces worker closed")]
pub struct WorkerClosedError {}

#[derive(Clone)]
pub struct WorkerWatcherSink {
    sender: mpsc::Sender<finterfaces::WatcherRequestStream>,
}

impl WorkerWatcherSink {
    /// Adds a new interface watcher to be operated on by [`Worker`].
    pub async fn add_watcher(
        &mut self,
        watcher: finterfaces::WatcherRequestStream,
    ) -> Result<(), WorkerClosedError> {
        self.sender.send(watcher).await.map_err(|_: mpsc::SendError| WorkerClosedError {})
    }
}

#[derive(Clone)]
pub struct WorkerInterfaceSink {
    sender: mpsc::UnboundedSender<InterfaceEvent>,
}

impl WorkerInterfaceSink {
    /// Adds a new interface `id` with fixed properties `properties`.
    ///
    /// Added interfaces are always assumed to be offline and have no assigned
    /// address or default routes.
    ///
    /// The returned [`InterfaceEventProducer`] can be used to feed interface
    /// changes to be notified to FIDL watchers. On drop,
    /// `InterfaceEventProducer` notifies the [`Worker`] that the interface was
    /// removed.
    ///
    /// Note that the [`Worker`] will exit with an error if two interfaces with
    /// the same identifier are created at the same time, but that is not
    /// observable from `add_interface`. It does not provide guardrails to
    /// prevent identifier reuse, however.
    pub fn add_interface(
        &self,
        id: BindingId,
        properties: InterfaceProperties,
    ) -> Result<InterfaceEventProducer, WorkerClosedError> {
        self.sender
            .unbounded_send(InterfaceEvent::Added { id, properties })
            .map_err(|_: mpsc::TrySendError<_>| WorkerClosedError {})?;
        Ok(InterfaceEventProducer { id, channel: self.sender.clone() })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bindings::util::TryIntoCore as _;
    use assert_matches::assert_matches;
    use fidl_fuchsia_hardware_network as fhardware_network;
    use fixture::fixture;
    use futures::{Future, Stream};
    use itertools::Itertools as _;
    use net_types::ip::{AddrSubnet, IpAddress as _, Ipv6, Ipv6Addr};
    use std::convert::{TryFrom as _, TryInto as _};
    use test_case::test_case;

    impl WorkerWatcherSink {
        fn create_watcher(&mut self) -> finterfaces::WatcherProxy {
            let (watcher, stream) =
                fidl::endpoints::create_proxy_and_stream::<finterfaces::WatcherMarker>()
                    .expect("create proxy");
            self.add_watcher(stream)
                .now_or_never()
                .expect("unexpected backpressure on sink")
                .expect("failed to send watcher to worker");
            watcher
        }

        fn create_watcher_event_stream(
            &mut self,
        ) -> impl Stream<Item = finterfaces::Event> + Unpin {
            futures::stream::unfold(self.create_watcher(), |watcher| {
                watcher.watch().map(move |e| match e {
                    Ok(event) => Some((event, watcher)),
                    Err(e) => {
                        if e.is_closed() {
                            None
                        } else {
                            panic!("error fetching next event on watcher {:?}", e);
                        }
                    }
                })
            })
        }
    }

    async fn with_worker<
        Fut: Future<Output = ()>,
        F: FnOnce(WorkerWatcherSink, WorkerInterfaceSink) -> Fut,
    >(
        _name: &str,
        f: F,
    ) {
        let (worker, watcher_sink, interface_sink) = Worker::new();
        let (r, ()) = futures::future::join(worker.run(), f(watcher_sink, interface_sink)).await;
        let watchers = r.expect("worker failed");
        let () = watchers.try_collect().await.expect("watchers error");
    }

    const IFACE1_ID: BindingId = 111;
    const IFACE1_NAME: &str = "iface1";
    const IFACE1_CLASS: finterfaces::DeviceClass =
        finterfaces::DeviceClass::Device(fhardware_network::DeviceClass::Ethernet);

    const IFACE2_ID: BindingId = 222;
    const IFACE2_NAME: &str = "iface2";
    const IFACE2_CLASS: finterfaces::DeviceClass =
        finterfaces::DeviceClass::Loopback(finterfaces::Empty {});

    /// Tests full integration between [`Worker`] and [`Watcher`]s through basic
    /// state updates.
    #[fixture(with_worker)]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn basic_state_updates(
        mut watcher_sink: WorkerWatcherSink,
        interface_sink: WorkerInterfaceSink,
    ) {
        let mut watcher = watcher_sink.create_watcher_event_stream();
        assert_eq!(watcher.next().await, Some(finterfaces::Event::Idle(finterfaces::Empty {})));

        let producer = interface_sink
            .add_interface(
                IFACE1_ID,
                InterfaceProperties { name: IFACE1_NAME.to_string(), device_class: IFACE1_CLASS },
            )
            .expect("add interface");

        assert_eq!(
            watcher.next().await,
            Some(finterfaces::Event::Added(
                finterfaces_ext::Properties {
                    id: IFACE1_ID,
                    addresses: Vec::new(),
                    online: false,
                    device_class: IFACE1_CLASS,
                    has_default_ipv4_route: false,
                    has_default_ipv6_route: false,
                    name: IFACE1_NAME.to_string(),
                }
                .into()
            ))
        );

        let addr1 = AddrSubnetEither::V6(
            AddrSubnet::new(*Ipv6::LOOPBACK_IPV6_ADDRESS, Ipv6Addr::BYTES * 8).unwrap(),
        );
        const ADDR_VALID_UNTIL: zx::Time = zx::Time::from_nanos(12345);
        const BASE_PROPERTIES: finterfaces::Properties =
            finterfaces::Properties { id: Some(IFACE1_ID), ..finterfaces::Properties::EMPTY };

        for (event, expect) in [
            (
                InterfaceUpdate::AddressAdded {
                    addr: addr1.clone(),
                    assignment_state: IpAddressState::Assigned,
                    valid_until: ADDR_VALID_UNTIL,
                },
                finterfaces::Event::Changed(finterfaces::Properties {
                    addresses: Some(vec![finterfaces_ext::Address {
                        addr: addr1.clone().into_fidl(),
                        valid_until: ADDR_VALID_UNTIL.into_nanos(),
                    }
                    .into()]),
                    ..BASE_PROPERTIES
                }),
            ),
            (
                InterfaceUpdate::DefaultRouteChanged {
                    version: IpVersion::V4,
                    has_default_route: true,
                },
                finterfaces::Event::Changed(finterfaces::Properties {
                    has_default_ipv4_route: Some(true),
                    ..BASE_PROPERTIES
                }),
            ),
            (
                InterfaceUpdate::DefaultRouteChanged {
                    version: IpVersion::V6,
                    has_default_route: true,
                },
                finterfaces::Event::Changed(finterfaces::Properties {
                    has_default_ipv6_route: Some(true),
                    ..BASE_PROPERTIES
                }),
            ),
            (
                InterfaceUpdate::DefaultRouteChanged {
                    version: IpVersion::V6,
                    has_default_route: false,
                },
                finterfaces::Event::Changed(finterfaces::Properties {
                    has_default_ipv6_route: Some(false),
                    ..BASE_PROPERTIES
                }),
            ),
            (
                InterfaceUpdate::OnlineChanged(true),
                finterfaces::Event::Changed(finterfaces::Properties {
                    online: Some(true),
                    ..BASE_PROPERTIES
                }),
            ),
        ] {
            producer.notify(event).expect("notify event");
            assert_eq!(watcher.next().await, Some(expect));
        }

        // Install a new watcher and observe accumulated interface state.
        let mut new_watcher = watcher_sink.create_watcher_event_stream();
        assert_eq!(
            new_watcher.next().await,
            Some(finterfaces::Event::Existing(
                finterfaces_ext::Properties {
                    id: IFACE1_ID,
                    name: IFACE1_NAME.to_string(),
                    device_class: IFACE1_CLASS,
                    online: true,
                    addresses: vec![finterfaces_ext::Address {
                        addr: addr1.into_fidl(),
                        valid_until: ADDR_VALID_UNTIL.into_nanos()
                    }
                    .into()],
                    has_default_ipv4_route: true,
                    has_default_ipv6_route: false,
                }
                .into()
            ))
        );
        assert_eq!(new_watcher.next().await, Some(finterfaces::Event::Idle(finterfaces::Empty {})));
    }

    /// Tests [`Drop`] implementation for [`InterfaceEventProducer`].
    #[fixture(with_worker)]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn drop_producer_removes_interface(
        mut watcher_sink: WorkerWatcherSink,
        interface_sink: WorkerInterfaceSink,
    ) {
        let mut watcher = watcher_sink.create_watcher_event_stream();
        assert_eq!(watcher.next().await, Some(finterfaces::Event::Idle(finterfaces::Empty {})));
        let producer1 = interface_sink
            .add_interface(
                IFACE1_ID,
                InterfaceProperties { name: IFACE1_NAME.to_string(), device_class: IFACE1_CLASS },
            )
            .expect(" add interface");
        let _producer2 = interface_sink.add_interface(
            IFACE2_ID,
            InterfaceProperties { name: IFACE2_NAME.to_string(), device_class: IFACE2_CLASS },
        );
        assert_matches!(
            watcher.next().await,
            Some(finterfaces::Event::Added(
                finterfaces::Properties {
                    id: Some(id),
                    ..
                })) if id == IFACE1_ID
        );
        assert_matches!(
            watcher.next().await,
            Some(finterfaces::Event::Added(
                finterfaces::Properties {
                    id: Some(id),
                    ..
                })) if id == IFACE2_ID
        );
        std::mem::drop(producer1);
        assert_eq!(watcher.next().await, Some(finterfaces::Event::Removed(IFACE1_ID)));

        // Create new watcher and enumerate, only interface 2 should be
        // around now.
        let mut new_watcher = watcher_sink.create_watcher_event_stream();
        assert_matches!(
            new_watcher.next().await,
            Some(finterfaces::Event::Existing(finterfaces::Properties {
                id: Some(id),
                ..
            })) if id == IFACE2_ID
        );
        assert_eq!(new_watcher.next().await, Some(finterfaces::Event::Idle(finterfaces::Empty {})));
    }

    fn iface1_initial_state() -> (BindingId, InterfaceState) {
        (
            IFACE1_ID,
            InterfaceState {
                properties: InterfaceProperties {
                    name: IFACE1_NAME.to_string(),
                    device_class: IFACE1_CLASS,
                },
                online: false,
                addresses: Default::default(),
                has_default_ipv4_route: false,
                has_default_ipv6_route: false,
            },
        )
    }

    #[test]
    fn consume_interface_added() {
        let mut state = HashMap::new();
        let (id, initial_state) = iface1_initial_state();

        let event = InterfaceEvent::Added { id, properties: initial_state.properties.clone() };

        // Add interface.
        assert_eq!(
            Worker::consume_event(&mut state, event.clone()),
            Ok(Some(finterfaces::Event::Added(
                finterfaces_ext::Properties {
                    id,
                    name: initial_state.properties.name.clone(),
                    device_class: initial_state.properties.device_class.clone(),
                    online: false,
                    addresses: Vec::new(),
                    has_default_ipv4_route: false,
                    has_default_ipv6_route: false,
                }
                .into()
            )))
        );

        // Verify state has been updated.
        assert_eq!(state.get(&id), Some(&initial_state));

        // Adding again causes error.
        assert_eq!(
            Worker::consume_event(&mut state, event),
            Err(WorkerError::AddedDuplicateInterface { interface: id, old: initial_state })
        );
    }

    #[test]
    fn consume_interface_removed() {
        let (id, initial_state) = iface1_initial_state();
        let mut state = HashMap::from([(id, initial_state)]);

        // Remove interface.
        assert_eq!(
            Worker::consume_event(&mut state, InterfaceEvent::Removed(id)),
            Ok(Some(finterfaces::Event::Removed(id)))
        );
        // State is updated.
        assert_eq!(state.get(&id), None);
        // Can't remove again.
        assert_eq!(
            Worker::consume_event(&mut state, InterfaceEvent::Removed(id)),
            Err(WorkerError::RemoveNonexistentInterface(id))
        );
    }

    #[test]
    fn consume_changed_bad_id() {
        let mut state = HashMap::new();
        assert_eq!(
            Worker::consume_event(
                &mut state,
                InterfaceEvent::Changed {
                    id: IFACE1_ID,
                    event: InterfaceUpdate::OnlineChanged(true)
                }
            ),
            Err(WorkerError::UpdateNonexistentInterface(IFACE1_ID))
        );
    }

    #[test_case(IpAddressState::Assigned; "assigned")]
    #[test_case(IpAddressState::Unavailable; "unavailable")]
    fn consume_changed_address_added(assignment_state: IpAddressState) {
        let addr = AddrSubnetEither::V6(
            AddrSubnet::new(*Ipv6::LOOPBACK_IPV6_ADDRESS, Ipv6Addr::BYTES * 8).unwrap(),
        );
        let valid_until = zx::Time::from_nanos(1234);
        let (id, initial_state) = iface1_initial_state();

        let mut state = HashMap::from([(id, initial_state)]);

        let event = InterfaceEvent::Changed {
            id,
            event: InterfaceUpdate::AddressAdded {
                addr: addr.clone(),
                assignment_state,
                valid_until,
            },
        };

        // Add address.
        assert_eq!(
            Worker::consume_event(&mut state, event.clone()),
            Ok(Some(finterfaces::Event::Changed(finterfaces::Properties {
                id: Some(id),
                addresses: Some(vec![finterfaces_ext::Address {
                    addr: addr.clone().into_fidl(),
                    valid_until: valid_until.into_nanos()
                }
                .into()]),
                ..finterfaces::Properties::EMPTY
            })))
        );

        let (ip_addr, prefix_len) = addr.addr_prefix();

        // Check state is updated.
        assert_eq!(
            state.get(&id).expect("missing interface entry").addresses.get(&*ip_addr),
            Some(&AddressProperties {
                prefix_len,
                state: AddressState { valid_until: valid_until, visible: true }
            })
        );
        // Can't add again.
        assert_eq!(
            Worker::consume_event(&mut state, event),
            Err(WorkerError::AssignExistingAddr { addr: *ip_addr, interface: id })
        );
    }

    #[test]
    fn adding_and_removing_tentative_addresses_do_not_trigger_events() {
        let addr = AddrSubnetEither::V6(
            AddrSubnet::new(*Ipv6::LOOPBACK_IPV6_ADDRESS, Ipv6Addr::BYTES * 8).unwrap(),
        );
        let valid_until = zx::Time::from_nanos(1234);
        let (id, initial_state) = iface1_initial_state();

        let mut state = HashMap::from([(id, initial_state)]);

        let event = InterfaceEvent::Changed {
            id,
            event: InterfaceUpdate::AddressAdded {
                addr: addr.clone(),
                assignment_state: IpAddressState::Tentative,
                valid_until: valid_until,
            },
        };

        // Add address, no event should be generated.
        assert_eq!(Worker::consume_event(&mut state, event.clone()), Ok(None));
        let (ip_addr, prefix_len) = addr.addr_prefix();
        // Check state is updated.
        assert_eq!(
            state.get(&id).expect("missing interface entry").addresses.get(&*ip_addr),
            Some(&AddressProperties {
                prefix_len,
                state: AddressState { valid_until, visible: false }
            })
        );

        let event =
            InterfaceEvent::Changed { id, event: InterfaceUpdate::AddressRemoved(*ip_addr) };
        // Remove address, no event should be generated.
        assert_eq!(Worker::consume_event(&mut state, event), Ok(None));

        // Check state is updated.
        assert_eq!(state.get(&id).expect("missing interface entry").addresses.get(&*ip_addr), None);
    }

    #[test_case(false; "no_changes_to_unavailable")]
    #[test_case(true; "intersperse_changes_to_unavailable")]
    fn consume_changed_address_state_change(intersperse_unavailable: bool) {
        let subnet = AddrSubnetEither::<net_types::SpecifiedAddr<_>>::V6(
            AddrSubnet::new(*Ipv6::LOOPBACK_IPV6_ADDRESS, Ipv6Addr::BYTES * 8).unwrap(),
        );
        let (addr, prefix_len) = subnet.addr_prefix();
        let addr = *addr;
        let valid_until = zx::Time::from_nanos(1234);
        let address_properties =
            AddressProperties { prefix_len, state: AddressState { valid_until, visible: false } };
        let (id, initial_state) = iface1_initial_state();
        let initial_state = InterfaceState {
            addresses: HashMap::from([(addr, address_properties)]),
            ..initial_state
        };
        // When `intersperse_unavailable` is `true` a state change to
        // Unavailable is injected between all state changes: e.g.
        // Tentative -> Assigned becomes Tentative -> Unavailable -> Assigned.
        // This allows us to verify that changes to Unavailable do not influence
        // the events observed by the client.
        let maybe_change_state_to_unavailable = |state: &mut HashMap<u64, InterfaceState>| {
            if !intersperse_unavailable {
                return;
            }
            let event = InterfaceEvent::Changed {
                id,
                event: InterfaceUpdate::AddressAssignmentStateChanged {
                    addr,
                    new_state: IpAddressState::Unavailable,
                },
            };
            let old_state = state
                .get(&id)
                .expect("missing_interface entry")
                .addresses
                .get(&addr)
                .expect("missing address entry")
                .clone();
            // Changing state to Unavailable should never generate an event.
            assert_eq!(Worker::consume_event(state, event), Ok(None));
            // Check state is not updated.
            assert_eq!(
                state.get(&id).expect("missing interface entry").addresses.get(&addr),
                Some(&old_state)
            );
        };

        // Invisible address does not show up initially.
        assert_eq!(
            Worker::collect_addresses::<finterfaces::Address>(&initial_state.addresses),
            vec![]
        );

        let mut state = HashMap::from([(id, initial_state)]);

        maybe_change_state_to_unavailable(&mut state);

        let event = InterfaceEvent::Changed {
            id,
            event: InterfaceUpdate::AddressAssignmentStateChanged {
                addr,
                new_state: IpAddressState::Assigned,
            },
        };

        // State switch causes event.
        assert_eq!(
            Worker::consume_event(&mut state, event.clone()),
            Ok(Some(finterfaces::Event::Changed(finterfaces::Properties {
                id: Some(id),
                addresses: Some(vec![finterfaces_ext::Address {
                    addr: subnet.into_fidl(),
                    valid_until: valid_until.into_nanos()
                }
                .into()]),
                ..finterfaces::Properties::EMPTY
            })))
        );

        // Check state is updated.
        assert_eq!(
            state.get(&id).expect("missing interface entry").addresses.get(&addr),
            Some(&AddressProperties {
                prefix_len,
                state: AddressState { valid_until, visible: true },
            })
        );

        maybe_change_state_to_unavailable(&mut state);

        // Sending again will not produce an event, because no change.
        assert_eq!(Worker::consume_event(&mut state, event), Ok(None));

        maybe_change_state_to_unavailable(&mut state);

        // Switch the state back to tentative, which will trigger removal.
        let event = InterfaceEvent::Changed {
            id,
            event: InterfaceUpdate::AddressAssignmentStateChanged {
                addr,
                new_state: IpAddressState::Tentative,
            },
        };
        assert_eq!(
            Worker::consume_event(&mut state, event.clone()),
            Ok(Some(finterfaces::Event::Changed(finterfaces::Properties {
                id: Some(id),
                addresses: Some(Vec::new()),
                ..finterfaces::Properties::EMPTY
            })))
        );

        // Check state is updated.
        assert_eq!(
            state.get(&id).expect("missing interface entry").addresses.get(&addr),
            Some(&AddressProperties {
                prefix_len,
                state: AddressState { valid_until, visible: false },
            })
        );

        maybe_change_state_to_unavailable(&mut state);

        // Sending again will not produce an event, because no change.
        assert_eq!(Worker::consume_event(&mut state, event), Ok(None));

        maybe_change_state_to_unavailable(&mut state);
    }

    #[test_case(IpAddressState::Assigned; "assigned")]
    #[test_case(IpAddressState::Unavailable; "unavailable")]
    fn consume_changed_start_unavailable(new_state: IpAddressState) {
        let addr = AddrSubnetEither::V6(
            AddrSubnet::new(*Ipv6::LOOPBACK_IPV6_ADDRESS, Ipv6Addr::BYTES * 8).unwrap(),
        );
        let valid_until = zx::Time::from_nanos(1234);
        let (id, initial_state) = iface1_initial_state();

        let mut state = HashMap::from([(id, initial_state)]);

        let event = InterfaceEvent::Changed {
            id,
            event: InterfaceUpdate::AddressAdded {
                addr: addr.clone(),
                assignment_state: IpAddressState::Unavailable,
                valid_until,
            },
        };

        // Add address.
        assert_eq!(
            Worker::consume_event(&mut state, event.clone()),
            Ok(Some(finterfaces::Event::Changed(finterfaces::Properties {
                id: Some(id),
                addresses: Some(vec![finterfaces_ext::Address {
                    addr: addr.clone().into_fidl(),
                    valid_until: valid_until.into_nanos()
                }
                .into()]),
                ..finterfaces::Properties::EMPTY
            })))
        );

        let (ip_addr, prefix_len) = addr.addr_prefix();

        // Check state is updated.
        assert_eq!(
            state.get(&id).expect("missing interface entry").addresses.get(&*ip_addr),
            Some(&AddressProperties {
                prefix_len,
                state: AddressState { valid_until: valid_until, visible: true }
            })
        );

        // Send an event to change the state.
        let event = InterfaceEvent::Changed {
            id,
            event: InterfaceUpdate::AddressAssignmentStateChanged { addr: *addr.addr(), new_state },
        };

        let (expected_visibility, expected_event) = match new_state {
            // Changing from `Unavailable` to `Unavailable` is a No-Op.
            IpAddressState::Unavailable |
            // Changing from `Unavailable` to `Assigned` does not change the
            // address visibility and should not generate events.
            IpAddressState::Assigned => (true, None),
            // Changing from `Unavailable` to `Tentative` makes the address
            // invisible and should generate an event with the address missing.
            IpAddressState::Tentative => (false, Some(finterfaces::Event::Changed(finterfaces::Properties {
                id: Some(id),
                addresses: Some(Vec::new()),
                ..finterfaces::Properties::EMPTY
            }))),
        };
        assert_eq!(Worker::consume_event(&mut state, event), Ok(expected_event));
        assert_eq!(
            state.get(&id).expect("missing interface entry").addresses.get(&addr.addr()),
            Some(&AddressProperties {
                prefix_len,
                state: AddressState { valid_until, visible: expected_visibility },
            })
        );
    }

    #[test]
    fn consume_changed_address_removed() {
        let addr = (*Ipv6::LOOPBACK_IPV6_ADDRESS).into();
        let address_properties = AddressProperties {
            prefix_len: Ipv6Addr::BYTES * 8,
            state: AddressState { valid_until: zx::Time::INFINITE, visible: true },
        };
        let (id, initial_state) = iface1_initial_state();
        let initial_state = InterfaceState {
            addresses: HashMap::from([(addr, address_properties)]),
            ..initial_state
        };
        let mut state = HashMap::from([(id, initial_state)]);

        let event = InterfaceEvent::Changed { id, event: InterfaceUpdate::AddressRemoved(addr) };

        // Remove address.
        assert_eq!(
            Worker::consume_event(&mut state, event.clone()),
            Ok(Some(finterfaces::Event::Changed(finterfaces::Properties {
                id: Some(id),
                addresses: Some(Vec::new()),
                ..finterfaces::Properties::EMPTY
            })))
        );
        // Check state is updated.
        assert_eq!(state.get(&id).expect("missing interface entry").addresses.get(&addr), None);
        // Can't remove again.
        assert_eq!(
            Worker::consume_event(&mut state, event),
            Err(WorkerError::UnassignNonexistentAddr { interface: id, addr })
        );
    }

    #[test]
    fn consume_changed_online() {
        let (id, initial_state) = iface1_initial_state();
        let mut state = HashMap::from([(id, initial_state)]);

        // Change to online.
        assert_eq!(
            Worker::consume_event(
                &mut state,
                InterfaceEvent::Changed { id, event: InterfaceUpdate::OnlineChanged(true) }
            ),
            Ok(Some(finterfaces::Event::Changed(finterfaces::Properties {
                id: Some(id),
                online: Some(true),
                ..finterfaces::Properties::EMPTY
            })))
        );
        // Check state is updated.
        assert_eq!(state.get(&id).expect("missing interface entry").online, true);
        // Change again produces no update.
        assert_eq!(
            Worker::consume_event(
                &mut state,
                InterfaceEvent::Changed { id, event: InterfaceUpdate::OnlineChanged(true) }
            ),
            Ok(None)
        );
    }

    #[test_case(IpVersion::V4; "ipv4")]
    #[test_case(IpVersion::V6; "ipv6")]
    fn consume_changed_default_route(version: IpVersion) {
        let (id, initial_state) = iface1_initial_state();
        let mut state = HashMap::from([(id, initial_state)]);

        let expect_set_props = match version {
            IpVersion::V4 => finterfaces::Properties {
                has_default_ipv4_route: Some(true),
                ..finterfaces::Properties::EMPTY
            },
            IpVersion::V6 => finterfaces::Properties {
                has_default_ipv6_route: Some(true),
                ..finterfaces::Properties::EMPTY
            },
        };

        // Update default route.
        assert_eq!(
            Worker::consume_event(
                &mut state,
                InterfaceEvent::Changed {
                    id,
                    event: InterfaceUpdate::DefaultRouteChanged {
                        version,
                        has_default_route: true
                    }
                }
            ),
            Ok(Some(finterfaces::Event::Changed(finterfaces::Properties {
                id: Some(id),
                ..expect_set_props
            })))
        );
        // Check only the proper state is updated.
        let InterfaceState { has_default_ipv4_route, has_default_ipv6_route, .. } =
            state.get(&id).expect("missing interface entry");
        assert_eq!(*has_default_ipv4_route, version == IpVersion::V4);
        assert_eq!(*has_default_ipv6_route, version == IpVersion::V6);
        // Change again produces no update.
        assert_eq!(
            Worker::consume_event(
                &mut state,
                InterfaceEvent::Changed {
                    id,
                    event: InterfaceUpdate::DefaultRouteChanged {
                        version,
                        has_default_route: true
                    }
                }
            ),
            Ok(None)
        );
    }

    #[fixture(with_worker)]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn watcher_enqueues_events(
        mut watcher_sink: WorkerWatcherSink,
        interface_sink: WorkerInterfaceSink,
    ) {
        let mut create_watcher = || {
            let mut watcher = watcher_sink.create_watcher_event_stream();
            async move {
                assert_eq!(
                    watcher.next().await,
                    Some(finterfaces::Event::Idle(finterfaces::Empty {}))
                );
                watcher
            }
        };

        let watcher1 = create_watcher().await;
        let watcher2 = create_watcher().await;

        let range = 1..=10;
        let producers = watcher1
            .zip(futures::stream::iter(range.clone().map(|i| {
                let producer = interface_sink
                    .add_interface(
                        i,
                        InterfaceProperties {
                            name: format!("if{}", i),
                            device_class: IFACE1_CLASS,
                        },
                    )
                    .expect("failed to add interface");
                (producer, i)
            })))
            .map(|(event, (producer, i))| {
                assert_matches!(
                    event,
                    finterfaces::Event::Added(finterfaces::Properties {
                        id: Some(id),
                        ..
                    }) if id == i
                );
                producer
            })
            .collect::<Vec<_>>()
            .await;
        assert_eq!(producers.len(), usize::try_from(*range.end()).unwrap());

        let last = watcher2
            .zip(futures::stream::iter(range.clone()))
            .fold(None, |_, (event, i)| {
                assert_matches!(
                    event,
                    finterfaces::Event::Added(finterfaces::Properties {
                        id: Some(id),
                        ..
                    }) if id == i
                );
                futures::future::ready(Some(i))
            })
            .await;
        assert_eq!(last, Some(*range.end()));
    }

    #[fixture(with_worker)]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn idle_watcher_gets_closed(
        mut watcher_sink: WorkerWatcherSink,
        interface_sink: WorkerInterfaceSink,
    ) {
        let watcher = watcher_sink.create_watcher();
        // Get the idle event to make sure the worker sees the watcher.
        assert_matches!(watcher.watch().await, Ok(finterfaces::Event::Idle(finterfaces::Empty {})));

        // NB: Every round generates two events, addition and removal because we
        // drop the producer.
        for i in 1..=(MAX_EVENTS / 2 + 1) {
            let _: InterfaceEventProducer = interface_sink
                .add_interface(
                    i.try_into().unwrap(),
                    InterfaceProperties { name: format!("if{}", i), device_class: IFACE1_CLASS },
                )
                .expect("failed to add interface");
        }
        // Watcher gets closed.
        assert_eq!(watcher.on_closed().await, Ok(zx::Signals::CHANNEL_PEER_CLOSED));
    }

    /// Tests that the worker can handle watchers coming and going.
    #[test]
    fn watcher_turnaround() {
        let mut executor =
            fuchsia_async::TestExecutor::new_with_fake_time().expect("failed to create executor");
        let (worker, mut watcher_sink, interface_sink) = Worker::new();
        let sink_keep = watcher_sink.clone();
        let create_watchers = fuchsia_async::Task::spawn(async move {
            let mut watcher = watcher_sink.create_watcher_event_stream();
            assert_eq!(watcher.next().await, Some(finterfaces::Event::Idle(finterfaces::Empty {})));
        });

        // NB: Map the output of the worker future so we can assert equality on
        // the return, since its return is not Debug otherwise.
        let worker_fut = worker.run().map(|result| {
            let pending_watchers = result.expect("worker finished with error");
            pending_watchers.len()
        });
        futures::pin_mut!(worker_fut);
        assert_eq!(executor.run_until_stalled(&mut worker_fut), std::task::Poll::Pending);
        // If executor stalled then the task must've finished.
        assert_eq!(create_watchers.now_or_never(), Some(()));

        // Drop the sinks, should cause the worker to return.
        std::mem::drop((sink_keep, interface_sink));
        assert_eq!(executor.run_until_stalled(&mut worker_fut), std::task::Poll::Ready(0));
    }

    #[fixture(with_worker)]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn address_sorting(
        mut watcher_sink: WorkerWatcherSink,
        interface_sink: WorkerInterfaceSink,
    ) {
        let mut watcher = watcher_sink.create_watcher_event_stream();
        assert_eq!(watcher.next().await, Some(finterfaces::Event::Idle(finterfaces::Empty {})));

        const ADDR1: fnet::Subnet = net_declare::fidl_subnet!("2000::1/64");
        const ADDR2: fnet::Subnet = net_declare::fidl_subnet!("192.168.1.1/24");
        const ADDR3: fnet::Subnet = net_declare::fidl_subnet!("192.168.1.2/24");

        for addrs in [ADDR1, ADDR2, ADDR3].into_iter().permutations(3) {
            let producer = interface_sink
                .add_interface(
                    IFACE1_ID,
                    InterfaceProperties {
                        name: IFACE1_NAME.to_string(),
                        device_class: IFACE1_CLASS,
                    },
                )
                .expect("failed to add interface");
            assert_matches!(
                watcher.next().await,
                Some(finterfaces::Event::Added(finterfaces::Properties {
                    id: Some(id), .. }
                )) if id == IFACE1_ID
            );

            let mut expect = vec![];
            for addr in addrs {
                producer
                    .notify(InterfaceUpdate::AddressAdded {
                        addr: addr.try_into_core().expect("invalid address"),
                        assignment_state: IpAddressState::Assigned,
                        valid_until: zx::Time::INFINITE,
                    })
                    .expect("failed to notify");
                expect.push(addr);
                expect.sort();

                let addresses = assert_matches!(
                    watcher.next().await,
                    Some(finterfaces::Event::Changed(finterfaces::Properties{
                        id: Some(IFACE1_ID),
                        addresses: Some(addresses),
                        ..
                    })) => addresses
                );
                let addresses = addresses
                    .into_iter()
                    .map(|finterfaces::Address { addr, .. }| addr.expect("missing address"))
                    .collect::<Vec<_>>();
                assert_eq!(addresses, expect);
            }
            std::mem::drop(producer);
            assert_eq!(watcher.next().await, Some(finterfaces::Event::Removed(IFACE1_ID)));
        }
    }

    #[fixture(with_worker)]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn watcher_disallows_double_get(
        mut watcher_sink: WorkerWatcherSink,
        _interface_sink: WorkerInterfaceSink,
    ) {
        let watcher = watcher_sink.create_watcher();
        assert_matches!(watcher.watch().await, Ok(finterfaces::Event::Idle(finterfaces::Empty {})));

        let (r1, r2) = futures::future::join(watcher.watch(), watcher.watch()).await;
        for r in [r1, r2] {
            assert_matches!(
                r,
                Err(fidl::Error::ClientChannelClosed { status: zx::Status::ALREADY_EXISTS, .. })
            );
        }
    }

    #[test]
    fn watcher_blocking_push() {
        let mut executor =
            fuchsia_async::TestExecutor::new_with_fake_time().expect("failed to create executor");
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<finterfaces::WatcherMarker>()
                .expect("failed to create watcher");
        let mut watcher =
            Watcher { stream, events: EventQueue { events: Default::default() }, responder: None };
        let mut watch_fut = proxy.watch();
        assert_matches!(executor.run_until_stalled(&mut watch_fut), std::task::Poll::Pending);
        assert_matches!(executor.run_until_stalled(&mut watcher), std::task::Poll::Pending);
        // Got a responder, we're pending.
        assert_matches!(watcher.responder, Some(_));
        watcher.push(finterfaces::Event::Idle(finterfaces::Empty {}));
        // Responder is executed.
        assert_matches!(watcher.responder, None);
        assert_matches!(
            executor.run_until_stalled(&mut watch_fut),
            std::task::Poll::Ready(Ok(finterfaces::Event::Idle(finterfaces::Empty {})))
        );
    }
}
