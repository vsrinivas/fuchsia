// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Netstack3 bindings.
//!
//! This module provides Fuchsia bindings for the [`netstack3_core`] crate.

#[macro_use]
mod macros;

#[cfg(test)]
mod integration_tests;

mod context;
mod devices;
mod ethernet_worker;
mod interfaces_admin;
mod interfaces_watcher;
mod netdevice_worker;
mod socket;
mod stack_fidl_worker;
mod timers;
mod util;

use std::convert::TryFrom as _;
use std::future::Future;
use std::num::NonZeroU16;
use std::ops::DerefMut as _;
use std::sync::Arc;
use std::time::Duration;

use fidl::endpoints::{
    ControlHandle as _, DiscoverableProtocolMarker, RequestStream, Responder as _,
};
use fidl_fuchsia_net_stack as fidl_net_stack;
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_zircon as zx;
use futures::{
    channel::mpsc, lock::Mutex, FutureExt as _, SinkExt as _, StreamExt as _, TryStreamExt as _,
};
use log::{debug, error, warn};
use packet::{BufferMut, Serializer};
use packet_formats::icmp::{IcmpEchoReply, IcmpMessage, IcmpUnusedCode};
use rand::rngs::OsRng;
use util::ConversionContext;

use context::Lockable;
use devices::{
    BindingId, CommonInfo, DeviceInfo, DeviceSpecificInfo, Devices, EthernetInfo, LoopbackInfo,
    NetdeviceInfo,
};
use interfaces_watcher::{InterfaceEventProducer, InterfaceProperties, InterfaceUpdate};
use timers::TimerDispatcher;

use net_types::ip::{AddrSubnet, AddrSubnetEither, Ip, Ipv4, Ipv6};
use netstack3_core::{
    add_ip_addr_subnet, add_route,
    context::{EventContext, InstantContext, RngContext, TimerContext},
    handle_timer, icmp, update_ipv4_configuration, update_ipv6_configuration, AddableEntryEither,
    BlanketCoreContext, BufferUdpContext, Ctx, DeviceId, DeviceLayerEventDispatcher,
    EventDispatcher, IpDeviceConfiguration, IpExt, IpSockCreationError, Ipv4DeviceConfiguration,
    Ipv6DeviceConfiguration, SlaacConfiguration, TimerId, UdpBoundId, UdpConnId, UdpContext,
    UdpListenerId,
};

/// Default MTU for loopback.
///
/// This value is also the default value used on Linux. As of writing:
///
/// ```shell
/// $ ip link show dev lo
/// 1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000
///     link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
/// ```
const DEFAULT_LOOPBACK_MTU: u32 = 65536;

pub(crate) trait LockableContext:
    for<'a> Lockable<'a, Ctx<Self::Dispatcher, Self::Context>>
{
    type Dispatcher: EventDispatcher;
    type Context: BlanketCoreContext + Send;
}

pub(crate) trait DeviceStatusNotifier {
    /// A notification that the state of the device with binding Id `id`
    /// changed.
    ///
    /// This method is called by workers that observe devices, such as
    /// [`EthernetWorker`]. This method is called after all the internal
    /// structures that cache or store device state are already up to date. The
    /// only side effect should be notifying other workers or external
    /// applications that are listening for status changes.
    fn device_status_changed(&mut self, id: u64);
}

pub(crate) trait InterfaceEventProducerFactory {
    fn create_interface_event_producer(
        &self,
        id: BindingId,
        properties: InterfaceProperties,
    ) -> InterfaceEventProducer;
}

type IcmpEchoSockets = socket::datagram::SocketCollectionPair<socket::datagram::IcmpEcho>;
type UdpSockets = socket::datagram::SocketCollectionPair<socket::datagram::Udp>;

/// `BindingsDispatcher` is the dispatcher used by [`Netstack`] and it
/// implements the regular network stack operation, sending outgoing frames to
/// the appropriate devices, and proxying calls to their appropriate submodules.
///
/// Implementation of some traits required by [`EventDispatcher`] may be in this
/// crate's submodules, closer to where the implementation logic makes more
/// sense.
#[derive(Default)]
pub(crate) struct BindingsDispatcher {
    devices: Devices,
    icmp_echo_sockets: IcmpEchoSockets,
    udp_sockets: UdpSockets,
}

impl AsRef<Devices> for BindingsDispatcher {
    fn as_ref(&self) -> &Devices {
        &self.devices
    }
}

impl AsMut<Devices> for BindingsDispatcher {
    fn as_mut(&mut self) -> &mut Devices {
        &mut self.devices
    }
}

impl DeviceStatusNotifier for BindingsDispatcher {
    fn device_status_changed(&mut self, _id: u64) {
        // NOTE(brunodalbo) we may want to do more things here in the future,
        // for now this is only intercepted for testing
    }
}

/// Provides context implementations which satisfy [`BlanketCoreContext`].
///
/// `BindingsContext` provides time, timers, and random numbers to the Core.
#[derive(Default)]
pub(crate) struct BindingsContextImpl {
    timers: timers::TimerDispatcher<TimerId>,
    rng: OsRng,
}

impl AsRef<timers::TimerDispatcher<TimerId>> for BindingsContextImpl {
    fn as_ref(&self) -> &TimerDispatcher<TimerId> {
        &self.timers
    }
}

impl AsMut<timers::TimerDispatcher<TimerId>> for BindingsContextImpl {
    fn as_mut(&mut self) -> &mut TimerDispatcher<TimerId> {
        &mut self.timers
    }
}

impl<'a> Lockable<'a, Ctx<BindingsDispatcher, BindingsContextImpl>> for Netstack {
    type Guard = futures::lock::MutexGuard<'a, Ctx<BindingsDispatcher, BindingsContextImpl>>;
    type Fut = futures::lock::MutexLockFuture<'a, Ctx<BindingsDispatcher, BindingsContextImpl>>;
    fn lock(&'a self) -> Self::Fut {
        self.ctx.lock()
    }
}

impl AsRef<IcmpEchoSockets> for BindingsDispatcher {
    fn as_ref(&self) -> &IcmpEchoSockets {
        &self.icmp_echo_sockets
    }
}

impl AsMut<IcmpEchoSockets> for BindingsDispatcher {
    fn as_mut(&mut self) -> &mut IcmpEchoSockets {
        &mut self.icmp_echo_sockets
    }
}

impl AsRef<UdpSockets> for BindingsDispatcher {
    fn as_ref(&self) -> &UdpSockets {
        &self.udp_sockets
    }
}

impl AsMut<UdpSockets> for BindingsDispatcher {
    fn as_mut(&mut self) -> &mut UdpSockets {
        &mut self.udp_sockets
    }
}

impl<D, C> timers::TimerHandler<TimerId> for Ctx<D, C>
where
    D: EventDispatcher + Send + Sync + 'static,
    C: BlanketCoreContext + AsMut<timers::TimerDispatcher<TimerId>> + Send + Sync + 'static,
{
    fn handle_expired_timer(&mut self, timer: TimerId) {
        handle_timer(self, &mut (), timer)
    }

    fn get_timer_dispatcher(&mut self) -> &mut timers::TimerDispatcher<TimerId> {
        self.ctx.as_mut()
    }
}

impl<C> timers::TimerContext<TimerId> for C
where
    C: LockableContext + Clone + Send + Sync + 'static,
    C::Dispatcher: Send + Sync + 'static,
    C::Context: AsMut<timers::TimerDispatcher<TimerId>> + Send + Sync + 'static,
{
    type Handler = Ctx<C::Dispatcher, C::Context>;
}

impl<D> ConversionContext for D
where
    D: AsRef<Devices>,
{
    fn get_core_id(&self, binding_id: u64) -> Option<DeviceId> {
        self.as_ref().get_core_id(binding_id)
    }

    fn get_binding_id(&self, core_id: DeviceId) -> Option<u64> {
        self.as_ref().get_binding_id(core_id)
    }
}

/// A thin wrapper around `fuchsia_async::Time` that implements `core::Instant`.
#[derive(PartialEq, Eq, PartialOrd, Ord, Copy, Clone, Debug)]
pub(crate) struct StackTime(fasync::Time);

impl netstack3_core::Instant for StackTime {
    fn duration_since(&self, earlier: StackTime) -> Duration {
        assert!(self.0 >= earlier.0);
        // guaranteed not to panic because the assertion ensures that the
        // difference is non-negative, and all non-negative i64 values are also
        // valid u64 values
        Duration::from_nanos(u64::try_from(self.0.into_nanos() - earlier.0.into_nanos()).unwrap())
    }

    fn checked_add(&self, duration: Duration) -> Option<StackTime> {
        Some(StackTime(fasync::Time::from_nanos(
            self.0.into_nanos().checked_add(i64::try_from(duration.as_nanos()).ok()?)?,
        )))
    }

    fn checked_sub(&self, duration: Duration) -> Option<StackTime> {
        Some(StackTime(fasync::Time::from_nanos(
            self.0.into_nanos().checked_sub(i64::try_from(duration.as_nanos()).ok()?)?,
        )))
    }
}

impl InstantContext for BindingsContextImpl {
    type Instant = StackTime;

    fn now(&self) -> StackTime {
        StackTime(fasync::Time::now())
    }
}

impl RngContext for BindingsContextImpl {
    type Rng = OsRng;

    fn rng(&self) -> &OsRng {
        &self.rng
    }

    fn rng_mut(&mut self) -> &mut OsRng {
        &mut self.rng
    }
}

impl TimerContext<TimerId> for BindingsContextImpl {
    fn schedule_timer_instant(&mut self, time: StackTime, id: TimerId) -> Option<StackTime> {
        self.timers.schedule_timer(id, time)
    }

    fn cancel_timer(&mut self, id: TimerId) -> Option<StackTime> {
        self.timers.cancel_timer(&id)
    }

    fn cancel_timers_with<F: FnMut(&TimerId) -> bool>(&mut self, f: F) {
        self.timers.cancel_timers_with(f);
    }

    fn scheduled_instant(&self, id: TimerId) -> Option<StackTime> {
        self.timers.scheduled_time(&id)
    }
}

impl<B> DeviceLayerEventDispatcher<B> for BindingsDispatcher
where
    B: BufferMut,
{
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: DeviceId,
        frame: S,
    ) -> Result<(), S> {
        // TODO(wesleyac): Error handling
        let frame = frame.serialize_vec_outer().map_err(|(_, ser)| ser)?;
        let dev = match self.devices.get_core_device_mut(device) {
            Some(dev) => dev,
            None => {
                error!("Tried to send frame on device that is not listed: {:?}", device);
                return Ok(());
            }
        };

        match dev.info_mut() {
            DeviceSpecificInfo::Ethernet(EthernetInfo {
                common_info: CommonInfo { admin_enabled, mtu: _, events: _, name: _ },
                client,
                mac: _,
                features: _,
                phy_up,
            }) => {
                if *admin_enabled && *phy_up {
                    client.send(frame.as_ref())
                }
            }
            DeviceSpecificInfo::Netdevice(NetdeviceInfo {
                common_info: CommonInfo { admin_enabled, mtu: _, events: _, name: _ },
                handler,
                mac: _,
                phy_up,
            }) => {
                if *admin_enabled && *phy_up {
                    handler.send(frame.as_ref()).unwrap_or_else(|e| {
                        log::warn!("failed to send frame to {:?}: {:?}", handler, e)
                    })
                }
            }
            DeviceSpecificInfo::Loopback(LoopbackInfo { .. }) => {
                unreachable!("loopback must not send packets out of the node")
            }
        }

        Ok(())
    }
}

impl<I> icmp::IcmpContext<I> for BindingsDispatcher
where
    I: socket::datagram::SocketCollectionIpExt<socket::datagram::IcmpEcho> + icmp::IcmpIpExt,
{
    fn receive_icmp_error(&mut self, conn: icmp::IcmpConnId<I>, seq_num: u16, err: I::ErrorCode) {
        I::get_collection_mut(self).receive_icmp_error(conn, seq_num, err)
    }

    fn close_icmp_connection(&mut self, conn: icmp::IcmpConnId<I>, err: IpSockCreationError) {
        I::get_collection_mut(self).close_icmp_connection(conn, err)
    }
}

impl<I, B: BufferMut> icmp::BufferIcmpContext<I, B> for BindingsDispatcher
where
    I: socket::datagram::SocketCollectionIpExt<socket::datagram::IcmpEcho> + icmp::IcmpIpExt,
    IcmpEchoReply: for<'a> IcmpMessage<I, &'a [u8], Code = IcmpUnusedCode>,
{
    fn receive_icmp_echo_reply(
        &mut self,
        conn: icmp::IcmpConnId<I>,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        id: u16,
        seq_num: u16,
        data: B,
    ) {
        I::get_collection_mut(self).receive_icmp_echo_reply(conn, src_ip, dst_ip, id, seq_num, data)
    }
}

impl<I> UdpContext<I> for BindingsDispatcher
where
    I: socket::datagram::SocketCollectionIpExt<socket::datagram::Udp> + icmp::IcmpIpExt,
{
    fn receive_icmp_error(&mut self, id: UdpBoundId<I>, err: I::ErrorCode) {
        I::get_collection_mut(self).receive_icmp_error(id, err)
    }
}

impl<I, B: BufferMut> BufferUdpContext<I, B> for BindingsDispatcher
where
    I: socket::datagram::SocketCollectionIpExt<socket::datagram::Udp> + IpExt,
{
    fn receive_udp_from_conn(
        &mut self,
        conn: UdpConnId<I>,
        src_ip: I::Addr,
        src_port: NonZeroU16,
        body: B,
    ) {
        I::get_collection_mut(self).receive_udp_from_conn(conn, src_ip, src_port, body)
    }

    fn receive_udp_from_listen(
        &mut self,
        listener: UdpListenerId<I>,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        src_port: Option<NonZeroU16>,
        body: B,
    ) {
        I::get_collection_mut(self)
            .receive_udp_from_listen(listener, src_ip, dst_ip, src_port, body)
    }
}

impl<I: Ip> EventContext<netstack3_core::IpDeviceEvent<DeviceId, I>> for BindingsDispatcher {
    fn on_event(&mut self, event: netstack3_core::IpDeviceEvent<DeviceId, I>) {
        let (device, event) = match event {
            netstack3_core::IpDeviceEvent::AddressAdded { device, addr, state } => (
                device,
                InterfaceUpdate::AddressAdded {
                    addr: addr.into(),
                    initial_state: interfaces_watcher::AddressState {
                        valid_until: zx::Time::INFINITE,
                        assignment_state: state,
                    },
                },
            ),
            netstack3_core::IpDeviceEvent::AddressRemoved { device, addr } => {
                (device, InterfaceUpdate::AddressRemoved(addr.into()))
            }
            netstack3_core::IpDeviceEvent::AddressStateChanged { device, addr, state } => (
                device,
                InterfaceUpdate::AddressAssignmentStateChanged {
                    addr: addr.into(),
                    new_state: state,
                },
            ),
        };
        self.notify_interface_update(device, event);
    }
}

impl<I: Ip> EventContext<netstack3_core::IpLayerEvent<DeviceId, I>> for BindingsDispatcher {
    fn on_event(&mut self, event: netstack3_core::IpLayerEvent<DeviceId, I>) {
        let (device, subnet, has_default_route) = match event {
            netstack3_core::IpLayerEvent::DeviceRouteAdded { device, subnet } => {
                (device, subnet, true)
            }
            netstack3_core::IpLayerEvent::DeviceRouteRemoved { device, subnet } => {
                (device, subnet, false)
            }
        };
        // We only care about the default route.
        if subnet.prefix() != 0 || subnet.network() != I::UNSPECIFIED_ADDRESS {
            return;
        }
        self.notify_interface_update(
            device,
            InterfaceUpdate::DefaultRouteChanged { version: I::VERSION, has_default_route },
        );
    }
}

impl EventContext<netstack3_core::DadEvent<DeviceId>> for BindingsDispatcher {
    fn on_event(&mut self, event: netstack3_core::DadEvent<DeviceId>) {
        match event {
            netstack3_core::DadEvent::AddressAssigned { device, addr } => {
                self.on_event(netstack3_core::IpDeviceEvent::<_, Ipv6>::AddressStateChanged {
                    device,
                    addr: *addr,
                    state: netstack3_core::IpAddressState::Assigned,
                })
            }
        }
    }
}

impl EventContext<netstack3_core::Ipv6RouteDiscoveryEvent<DeviceId>> for BindingsDispatcher {
    fn on_event(&mut self, _event: netstack3_core::Ipv6RouteDiscoveryEvent<DeviceId>) {
        // TODO(https://fxbug.dev/97203): Update forwarding table in response to
        // the event.
    }
}

impl BindingsDispatcher {
    fn notify_interface_update(&self, device: DeviceId, event: InterfaceUpdate) {
        self.devices
            .get_core_device(device)
            .unwrap_or_else(|| panic!("issued event {:?} for deleted device {:?}", event, device))
            .info()
            .common_info()
            .events
            .notify(event)
            .expect("interfaces worker closed");
    }
}

trait MutableDeviceState {
    /// Invoke a function on the state associated with the device `id`.
    fn update_device_state<F: FnOnce(&mut DeviceInfo)>(&mut self, id: u64, f: F);
}

impl<D, C> MutableDeviceState for Ctx<D, C>
where
    D: EventDispatcher + AsMut<Devices> + DeviceStatusNotifier,
    C: BlanketCoreContext,
{
    fn update_device_state<F: FnOnce(&mut DeviceInfo)>(&mut self, id: u64, f: F) {
        if let Some(device_info) = self.dispatcher.as_mut().get_device_mut(id) {
            f(device_info);
            self.dispatcher.device_status_changed(id)
        }
    }
}

trait InterfaceControl {
    /// Enables an interface.
    ///
    /// Both `admin_enabled` and `phy_up` must be true for the interface to be
    /// enabled.
    fn enable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error>;

    /// Disables an interface.
    ///
    /// Either an Admin (fidl) or Phy change can disable an interface.
    fn disable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error>;
}

fn set_interface_enabled<
    D: EventDispatcher + AsRef<Devices> + AsMut<Devices>,
    C: BlanketCoreContext,
>(
    ctx: &mut Ctx<D, C>,
    id: u64,
    should_enable: bool,
) -> Result<(), fidl_net_stack::Error> {
    let device =
        ctx.dispatcher.as_mut().get_device_mut(id).ok_or(fidl_net_stack::Error::NotFound)?;
    let core_id = device.core_id();

    let (dev_enabled, events) = match device.info_mut() {
        DeviceSpecificInfo::Ethernet(EthernetInfo {
            common_info: CommonInfo { admin_enabled, mtu: _, events, name: _ },
            client: _,
            mac: _,
            features: _,
            phy_up,
        })
        | DeviceSpecificInfo::Netdevice(NetdeviceInfo {
            common_info: CommonInfo { admin_enabled, mtu: _, events, name: _ },
            handler: _,
            mac: _,
            phy_up,
        }) => (*admin_enabled && *phy_up, events),
        DeviceSpecificInfo::Loopback(LoopbackInfo {
            common_info: CommonInfo { admin_enabled, mtu: _, events, name: _ },
        }) => (*admin_enabled, events),
    };

    if should_enable {
        // We want to enable the interface, but its device is considered
        // disabled so we do nothing further.
        //
        // This can happen when the interface was set to be administratively up
        // but the phy is down.
        if !dev_enabled {
            return Ok(());
        }
    } else {
        assert!(!dev_enabled, "caller attemped to disable an interface that is considered enabled");
    }

    events
        .notify(InterfaceUpdate::OnlineChanged(should_enable))
        .expect("interfaces worker not running");

    update_ipv4_configuration(ctx, core_id, |config| {
        config.ip_config.ip_enabled = should_enable;
    });
    update_ipv6_configuration(ctx, core_id, |config| {
        config.ip_config.ip_enabled = should_enable;
    });

    Ok(())
}

impl<D, C> InterfaceControl for Ctx<D, C>
where
    D: EventDispatcher + AsRef<Devices> + AsMut<Devices>,
    C: BlanketCoreContext,
{
    fn enable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        set_interface_enabled(self, id, true /* should_enable */)
    }

    fn disable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        set_interface_enabled(self, id, false /* should_enable */)
    }
}

type NetstackContext = Arc<Mutex<Ctx<BindingsDispatcher, BindingsContextImpl>>>;

/// The netstack.
///
/// Provides the entry point for creating a netstack to be served as a
/// component.
#[derive(Clone)]
pub struct Netstack {
    ctx: NetstackContext,
    interfaces_event_sink: interfaces_watcher::WorkerInterfaceSink,
}

/// Contains the information needed to start serving a network stack over FIDL.
pub struct NetstackSeed {
    netstack: Netstack,
    interfaces_worker: interfaces_watcher::Worker,
    interfaces_watcher_sink: interfaces_watcher::WorkerWatcherSink,
}

impl Default for NetstackSeed {
    fn default() -> Self {
        let (interfaces_worker, interfaces_watcher_sink, interfaces_event_sink) =
            interfaces_watcher::Worker::new();
        Self {
            netstack: Netstack { ctx: Default::default(), interfaces_event_sink },
            interfaces_worker,
            interfaces_watcher_sink,
        }
    }
}

impl LockableContext for Netstack {
    type Dispatcher = BindingsDispatcher;
    type Context = BindingsContextImpl;
}

impl InterfaceEventProducerFactory for Netstack {
    fn create_interface_event_producer(
        &self,
        id: BindingId,
        properties: InterfaceProperties,
    ) -> InterfaceEventProducer {
        self.interfaces_event_sink
            .add_interface(id, properties)
            .expect("interface worker not running")
    }
}

enum Service {
    Stack(fidl_fuchsia_net_stack::StackRequestStream),
    Socket(fidl_fuchsia_posix_socket::ProviderRequestStream),
    Interfaces(fidl_fuchsia_net_interfaces::StateRequestStream),
    InterfacesAdmin(fidl_fuchsia_net_interfaces_admin::InstallerRequestStream),
    Debug(fidl_fuchsia_net_debug::InterfacesRequestStream),
}

enum WorkItem {
    Incoming(Service),
    Task(fasync::Task<()>),
}

trait RequestStreamExt: RequestStream {
    fn serve_with<F, Fut, E>(self, f: F) -> futures::future::Map<Fut, fn(Result<(), E>) -> ()>
    where
        E: std::error::Error,
        F: FnOnce(Self) -> Fut,
        Fut: Future<Output = Result<(), E>>;
}

impl<D: DiscoverableProtocolMarker, S: RequestStream<Protocol = D>> RequestStreamExt for S {
    fn serve_with<F, Fut, E>(self, f: F) -> futures::future::Map<Fut, fn(Result<(), E>) -> ()>
    where
        E: std::error::Error,
        F: FnOnce(Self) -> Fut,
        Fut: Future<Output = Result<(), E>>,
    {
        f(self).map(|res| res.unwrap_or_else(|err| error!("{} error: {}", D::PROTOCOL_NAME, err)))
    }
}

impl NetstackSeed {
    /// Consumes the netstack and starts serving all the FIDL services it
    /// implements to the outgoing service directory.
    pub async fn serve(self) -> Result<(), anyhow::Error> {
        use anyhow::Context as _;

        debug!("Serving netstack");

        let Self { netstack, interfaces_worker, interfaces_watcher_sink } = self;

        {
            let mut ctx = netstack.lock().await;
            let ctx = ctx.deref_mut();

            // Add and initialize the loopback interface with the IPv4 and IPv6
            // loopback addresses and on-link routes to the loopback subnets.
            let loopback = ctx
                .state
                .add_loopback_device(DEFAULT_LOOPBACK_MTU)
                .expect("error adding loopback device");
            let devices: &mut Devices = ctx.dispatcher.as_mut();
            let _binding_id: u64 = devices
                .add_device(loopback, |id| {
                    const LOOPBACK_NAME: &'static str = "lo";
                    let events = netstack.create_interface_event_producer(
                        id,
                        InterfaceProperties {
                            name: LOOPBACK_NAME.to_string(),
                            device_class: fidl_fuchsia_net_interfaces::DeviceClass::Loopback(
                                fidl_fuchsia_net_interfaces::Empty {},
                            ),
                        },
                    );
                    events
                        .notify(InterfaceUpdate::OnlineChanged(true))
                        .expect("interfaces worker not running");
                    DeviceSpecificInfo::Loopback(LoopbackInfo {
                        common_info: CommonInfo {
                            mtu: DEFAULT_LOOPBACK_MTU,
                            admin_enabled: true,
                            events,
                            name: LOOPBACK_NAME.to_string(),
                        },
                    })
                })
                .expect("error adding loopback device");
            // Don't need DAD and IGMP/MLD on loopback.
            update_ipv4_configuration(ctx, loopback, |config| {
                *config = Ipv4DeviceConfiguration {
                    ip_config: IpDeviceConfiguration { ip_enabled: true, gmp_enabled: false },
                };
            });
            update_ipv6_configuration(ctx, loopback, |config| {
                *config = Ipv6DeviceConfiguration {
                    dad_transmits: None,
                    max_router_solicitations: None,
                    slaac_config: SlaacConfiguration {
                        enable_stable_addresses: true,
                        temporary_address_configuration: None,
                    },
                    ip_config: IpDeviceConfiguration { ip_enabled: true, gmp_enabled: false },
                };
            });
            add_ip_addr_subnet(
                ctx,
                loopback,
                AddrSubnetEither::V4(
                    AddrSubnet::from_witness(
                        Ipv4::LOOPBACK_ADDRESS,
                        Ipv4::LOOPBACK_SUBNET.prefix(),
                    )
                    .expect("error creating IPv4 loopback AddrSub"),
                ),
            )
            .expect("error adding IPv4 loopback address");
            add_route(
                ctx,
                AddableEntryEither::new(Ipv4::LOOPBACK_SUBNET.into(), Some(loopback), None)
                    .expect("error creating IPv4 route entry"),
            )
            .expect("error adding IPv4 loopback on-link subnet route");
            add_ip_addr_subnet(
                ctx,
                loopback,
                AddrSubnetEither::V6(
                    AddrSubnet::from_witness(
                        Ipv6::LOOPBACK_ADDRESS,
                        Ipv6::LOOPBACK_SUBNET.prefix(),
                    )
                    .expect("error creating IPv6 loopback AddrSub"),
                ),
            )
            .expect("error adding IPv6 loopback address");
            add_route(
                ctx,
                AddableEntryEither::new(Ipv6::LOOPBACK_SUBNET.into(), Some(loopback), None)
                    .expect("error creating IPv6 route entry"),
            )
            .expect("error adding IPv6 loopback on-link subnet route");

            // Start servicing timers.
            let Ctx {
                state: _,
                dispatcher: BindingsDispatcher { devices: _, icmp_echo_sockets: _, udp_sockets: _ },
                ctx: BindingsContextImpl { rng: _, timers },
            } = ctx;
            timers.spawn(netstack.clone());
        }

        let interfaces_worker_task = fuchsia_async::Task::spawn(async move {
            let result = interfaces_worker.run().await;
            // The worker is not expected to end for the lifetime of the stack.
            panic!("interfaces worker finished unexpectedly {:?}", result);
        });

        let mut fs = ServiceFs::new_local();
        let _: &mut ServiceFsDir<'_, _> = fs
            .dir("svc")
            .add_fidl_service(Service::Debug)
            .add_fidl_service(Service::Stack)
            .add_fidl_service(Service::Socket)
            .add_fidl_service(Service::Interfaces)
            .add_fidl_service(Service::InterfacesAdmin);

        let services = fs.take_and_serve_directory_handle().context("directory handle")?;

        // Buffer size doesn't matter much, we're just trying to reduce
        // allocations.
        const TASK_CHANNEL_BUFFER_SIZE: usize = 16;
        let (task_sink, task_stream) = mpsc::channel(TASK_CHANNEL_BUFFER_SIZE);
        let work_items = futures::stream::select(
            services.map(WorkItem::Incoming),
            task_stream.map(WorkItem::Task),
        );
        let work_items_fut = work_items
            .for_each_concurrent(None, |wi| async {
                match wi {
                    WorkItem::Incoming(Service::Stack(stack)) => {
                        stack
                            .serve_with(|rs| {
                                stack_fidl_worker::StackFidlWorker::serve(netstack.clone(), rs)
                            })
                            .await
                    }
                    WorkItem::Incoming(Service::Socket(socket)) => {
                        socket.serve_with(|rs| socket::serve(netstack.clone(), rs)).await
                    }
                    WorkItem::Incoming(Service::Interfaces(interfaces)) => {
                        interfaces
                            .serve_with(|rs| {
                                interfaces_watcher::serve(
                                    rs, interfaces_watcher_sink.clone()
                                )
                            })
                            .await
                    }
                    WorkItem::Incoming(Service::InterfacesAdmin(installer)) => {
                        log::debug!(
                            "serving {}",
                            fidl_fuchsia_net_interfaces_admin::InstallerMarker::PROTOCOL_NAME
                        );
                        interfaces_admin::serve(netstack.clone(), installer)
                            .map_err(anyhow::Error::from)
                            .forward(task_sink.clone().sink_map_err(anyhow::Error::from))
                            .await
                            .unwrap_or_else(|e| {
                                log::warn!(
                                    "error serving {}: {:?}",
                                    fidl_fuchsia_net_interfaces_admin::InstallerMarker::PROTOCOL_NAME,
                                    e
                                )
                            })
                    }
                    WorkItem::Incoming(Service::Debug(debug)) => {
                        // TODO(https://fxbug.dev/88797): Implement this
                        // properly. This protocol is stubbed out to allow
                        // shared integration test code with Netstack2.
                        debug
                            .serve_with(|rs| rs.try_for_each(|req| async move {
                                match req {
                                    fidl_fuchsia_net_debug::InterfacesRequest::GetAdmin {
                                        id: _,
                                        control,
                                        control_handle: _,
                                    } => {
                                        let () = control
                                            .close_with_epitaph(zx::Status::NOT_SUPPORTED)
                                            .unwrap_or_else(|e| if e.is_closed() {
                                                debug!("control handle closed before sending epitaph: {:?}", e)
                                            } else {
                                                error!("failed to send epitaph: {:?}", e)
                                            });
                                        warn!(
                                            "TODO(https://fxbug.dev/88797): fuchsia.net.debug/Interfaces not implemented"
                                        );
                                    }
                                    fidl_fuchsia_net_debug::InterfacesRequest::GetMac {
                                        id: _,
                                        responder,
                                    } => {
                                        responder.control_handle().shutdown_with_epitaph(zx::Status::NOT_SUPPORTED)
                                    }
                                }
                                Result::<(), fidl::Error>::Ok(())
                            }))
                            .await
                    }
                    WorkItem::Task(task) => task.await
                }
            });

        let ((), ()) = futures::future::join(work_items_fut, interfaces_worker_task).await;
        debug!("Services stream finished");
        Ok(())
    }
}
