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
mod debug_fidl_worker;
mod devices;
mod ethernet_worker;
mod filter_worker;
mod interfaces_admin;
mod interfaces_watcher;
mod netdevice_worker;
mod socket;
mod stack_fidl_worker;
mod timers;
mod util;

use std::collections::HashMap;
use std::convert::TryFrom as _;
use std::future::Future;
use std::num::NonZeroU16;
use std::ops::DerefMut as _;
use std::sync::Arc;
use std::time::Duration;

use fidl::endpoints::{DiscoverableProtocolMarker, RequestStream};
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fidl_fuchsia_net_stack as fidl_net_stack;
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_zircon as zx;
use futures::{
    channel::mpsc, lock::Mutex, FutureExt as _, SinkExt as _, StreamExt as _, TryStreamExt as _,
};
use log::{debug, error};
use packet::{BufferMut, Serializer};
use packet_formats::icmp::{IcmpEchoReply, IcmpMessage, IcmpUnusedCode};
use rand::rngs::OsRng;
use util::{ConversionContext, IntoFidl as _};

use context::Lockable;
use devices::{
    BindingId, CommonInfo, DeviceInfo, DeviceSpecificInfo, Devices, EthernetInfo, LoopbackInfo,
    NetdeviceInfo,
};
use interfaces_watcher::{InterfaceEventProducer, InterfaceProperties, InterfaceUpdate};
use timers::TimerDispatcher;

use net_types::{
    ip::{AddrSubnet, AddrSubnetEither, Ip, IpAddr, IpAddress, Ipv4, Ipv6},
    SpecifiedAddr,
};
use netstack3_core::{
    add_ip_addr_subnet,
    context::{CounterContext, EventContext, InstantContext, RngContext, TimerContext},
    data_structures::id_map::IdMap,
    device::{BufferDeviceLayerEventDispatcher, DeviceId, DeviceLayerEventDispatcher},
    error::NetstackError,
    handle_timer,
    ip::{
        device::{
            slaac::SlaacConfiguration,
            state::{IpDeviceConfiguration, Ipv4DeviceConfiguration, Ipv6DeviceConfiguration},
            IpDeviceEvent,
        },
        icmp, IpExt,
    },
    transport::udp::{BufferUdpContext, UdpBoundId, UdpConnId, UdpContext, UdpListenerId},
    Ctx, NonSyncContext, SyncCtx, TimerId,
};

const LOOPBACK_NAME: &'static str = "lo";

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

pub(crate) trait LockableContext: for<'a> Lockable<'a, Ctx<Self::NonSyncCtx>> {
    type NonSyncCtx: NonSyncContext<Instant = StackTime> + Send;
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

impl DeviceStatusNotifier for BindingsNonSyncCtxImpl {
    fn device_status_changed(&mut self, _id: u64) {
        // NOTE(brunodalbo) we may want to do more things here in the future,
        // for now this is only intercepted for testing
    }
}

/// Provides an implementation of [`NonSyncContext`].
#[derive(Default)]
pub(crate) struct BindingsNonSyncCtxImpl {
    rng: OsRng,
    timers: timers::TimerDispatcher<TimerId<StackTime>>,
    devices: Devices<DeviceId<StackTime>>,
    icmp_echo_sockets: IcmpEchoSockets,
    udp_sockets: UdpSockets,
    tcp_listeners: IdMap<zx::Socket>,
}

impl AsRef<timers::TimerDispatcher<TimerId<StackTime>>> for BindingsNonSyncCtxImpl {
    fn as_ref(&self) -> &TimerDispatcher<TimerId<StackTime>> {
        &self.timers
    }
}

impl AsMut<timers::TimerDispatcher<TimerId<StackTime>>> for BindingsNonSyncCtxImpl {
    fn as_mut(&mut self) -> &mut TimerDispatcher<TimerId<StackTime>> {
        &mut self.timers
    }
}

impl AsRef<Devices<DeviceId<StackTime>>> for BindingsNonSyncCtxImpl {
    fn as_ref(&self) -> &Devices<DeviceId<StackTime>> {
        &self.devices
    }
}

impl AsMut<Devices<DeviceId<StackTime>>> for BindingsNonSyncCtxImpl {
    fn as_mut(&mut self) -> &mut Devices<DeviceId<StackTime>> {
        &mut self.devices
    }
}

impl<'a> Lockable<'a, Ctx<BindingsNonSyncCtxImpl>> for Netstack {
    type Guard = futures::lock::MutexGuard<'a, Ctx<BindingsNonSyncCtxImpl>>;
    type Fut = futures::lock::MutexLockFuture<'a, Ctx<BindingsNonSyncCtxImpl>>;
    fn lock(&'a self) -> Self::Fut {
        self.ctx.lock()
    }
}

impl AsRef<IcmpEchoSockets> for BindingsNonSyncCtxImpl {
    fn as_ref(&self) -> &IcmpEchoSockets {
        &self.icmp_echo_sockets
    }
}

impl AsMut<IcmpEchoSockets> for BindingsNonSyncCtxImpl {
    fn as_mut(&mut self) -> &mut IcmpEchoSockets {
        &mut self.icmp_echo_sockets
    }
}

impl AsRef<UdpSockets> for BindingsNonSyncCtxImpl {
    fn as_ref(&self) -> &UdpSockets {
        &self.udp_sockets
    }
}

impl AsMut<UdpSockets> for BindingsNonSyncCtxImpl {
    fn as_mut(&mut self) -> &mut UdpSockets {
        &mut self.udp_sockets
    }
}

impl<NonSyncCtx> timers::TimerHandler<TimerId<NonSyncCtx::Instant>> for Ctx<NonSyncCtx>
where
    NonSyncCtx: NonSyncContext
        + AsMut<timers::TimerDispatcher<TimerId<NonSyncCtx::Instant>>>
        + Send
        + Sync
        + 'static,
{
    fn handle_expired_timer(&mut self, timer: TimerId<NonSyncCtx::Instant>) {
        let Ctx { sync_ctx, non_sync_ctx } = self;
        handle_timer(sync_ctx, non_sync_ctx, timer)
    }

    fn get_timer_dispatcher(
        &mut self,
    ) -> &mut timers::TimerDispatcher<TimerId<NonSyncCtx::Instant>> {
        self.non_sync_ctx.as_mut()
    }
}

impl<C> timers::TimerContext<TimerId<StackTime>> for C
where
    C: LockableContext + Clone + Send + Sync + 'static,
    C::NonSyncCtx: AsMut<timers::TimerDispatcher<TimerId<StackTime>>> + Send + Sync + 'static,
{
    type Handler = Ctx<C::NonSyncCtx>;
}

impl<D> ConversionContext for D
where
    D: AsRef<Devices<DeviceId<StackTime>>>,
{
    fn get_core_id(&self, binding_id: u64) -> Option<DeviceId<StackTime>> {
        self.as_ref().get_core_id(binding_id)
    }

    fn get_binding_id(&self, core_id: DeviceId<StackTime>) -> Option<u64> {
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

impl InstantContext for BindingsNonSyncCtxImpl {
    type Instant = StackTime;

    fn now(&self) -> StackTime {
        StackTime(fasync::Time::now())
    }
}

impl CounterContext for BindingsNonSyncCtxImpl {
    fn increment_counter(&mut self, _key: &'static str) {}
}

impl RngContext for BindingsNonSyncCtxImpl {
    type Rng = OsRng;

    fn rng(&self) -> &OsRng {
        &self.rng
    }

    fn rng_mut(&mut self) -> &mut OsRng {
        &mut self.rng
    }
}

impl TimerContext<TimerId<StackTime>> for BindingsNonSyncCtxImpl {
    fn schedule_timer_instant(
        &mut self,
        time: StackTime,
        id: TimerId<StackTime>,
    ) -> Option<StackTime> {
        self.timers.schedule_timer(id, time)
    }

    fn cancel_timer(&mut self, id: TimerId<StackTime>) -> Option<StackTime> {
        self.timers.cancel_timer(&id)
    }

    fn cancel_timers_with<F: FnMut(&TimerId<StackTime>) -> bool>(&mut self, f: F) {
        self.timers.cancel_timers_with(f);
    }

    fn scheduled_instant(&self, id: TimerId<StackTime>) -> Option<StackTime> {
        self.timers.scheduled_time(&id)
    }
}

impl DeviceLayerEventDispatcher for BindingsNonSyncCtxImpl {
    fn wake_rx_task(&mut self, device: &DeviceId<StackTime>) {
        match self.devices.get_core_device_mut(device) {
            Some(dev) => match dev.info_mut() {
                DeviceSpecificInfo::Ethernet(_) | DeviceSpecificInfo::Netdevice(_) => {
                    unreachable!("only loopback supports RX queues")
                }
                DeviceSpecificInfo::Loopback(LoopbackInfo { common_info: _, rx_notifier }) => {
                    rx_notifier.schedule()
                }
            },
            None => {
                panic!("Tried to receive frame on device that is not listed: {:?}", device);
            }
        }
    }
}

impl<B> BufferDeviceLayerEventDispatcher<B> for BindingsNonSyncCtxImpl
where
    B: BufferMut,
{
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: &DeviceId<StackTime>,
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
                common_info:
                    CommonInfo {
                        admin_enabled,
                        mtu: _,
                        events: _,
                        name: _,
                        control_hook: _,
                        addresses: _,
                    },
                client,
                mac: _,
                features: _,
                phy_up,
                interface_control: _,
            }) => {
                if *admin_enabled && *phy_up {
                    client.send(frame.as_ref())
                }
            }
            DeviceSpecificInfo::Netdevice(NetdeviceInfo {
                common_info:
                    CommonInfo {
                        admin_enabled,
                        mtu: _,
                        events: _,
                        name: _,
                        control_hook: _,
                        addresses: _,
                    },
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

impl<I> icmp::IcmpContext<I> for BindingsNonSyncCtxImpl
where
    I: socket::datagram::SocketCollectionIpExt<socket::datagram::IcmpEcho> + icmp::IcmpIpExt,
{
    fn receive_icmp_error(&mut self, conn: icmp::IcmpConnId<I>, seq_num: u16, err: I::ErrorCode) {
        I::get_collection_mut(self).receive_icmp_error(conn, seq_num, err)
    }
}

impl<I, B: BufferMut> icmp::BufferIcmpContext<I, B> for BindingsNonSyncCtxImpl
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

impl<I> UdpContext<I> for BindingsNonSyncCtxImpl
where
    I: socket::datagram::SocketCollectionIpExt<socket::datagram::Udp> + icmp::IcmpIpExt,
{
    fn receive_icmp_error(&mut self, id: UdpBoundId<I>, err: I::ErrorCode) {
        I::get_collection_mut(self).receive_icmp_error(id, err)
    }
}

impl<I, B: BufferMut> BufferUdpContext<I, B> for BindingsNonSyncCtxImpl
where
    I: socket::datagram::SocketCollectionIpExt<socket::datagram::Udp> + IpExt,
{
    fn receive_udp_from_conn(
        &mut self,
        conn: UdpConnId<I>,
        src_ip: I::Addr,
        src_port: NonZeroU16,
        body: &B,
    ) {
        I::get_collection_mut(self).receive_udp_from_conn(conn, src_ip, src_port, body)
    }

    fn receive_udp_from_listen(
        &mut self,
        listener: UdpListenerId<I>,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        src_port: Option<NonZeroU16>,
        body: &B,
    ) {
        I::get_collection_mut(self)
            .receive_udp_from_listen(listener, src_ip, dst_ip, src_port, body)
    }
}

impl<I: Ip> EventContext<IpDeviceEvent<DeviceId<StackTime>, I>> for BindingsNonSyncCtxImpl {
    fn on_event(&mut self, event: IpDeviceEvent<DeviceId<StackTime>, I>) {
        match event {
            IpDeviceEvent::AddressAdded { device, addr, state } => {
                self.notify_interface_update(
                    &device,
                    InterfaceUpdate::AddressAdded {
                        addr: addr.into(),
                        assignment_state: state,
                        valid_until: zx::Time::INFINITE,
                    },
                );
                self.notify_address_update(&device, addr.addr().into(), state);
            }
            IpDeviceEvent::AddressRemoved { device, addr } => self.notify_interface_update(
                &device,
                InterfaceUpdate::AddressRemoved(addr.to_ip_addr()),
            ),
            IpDeviceEvent::AddressStateChanged { device, addr, state } => {
                self.notify_interface_update(
                    &device,
                    InterfaceUpdate::AddressAssignmentStateChanged {
                        addr: addr.to_ip_addr(),
                        new_state: state,
                    },
                );
                self.notify_address_update(&device, addr.into(), state);
            }
            IpDeviceEvent::EnabledChanged { device, ip_enabled } => {
                self.notify_interface_update(&device, InterfaceUpdate::OnlineChanged(ip_enabled))
            }
        };
    }
}

impl<I: Ip> EventContext<netstack3_core::ip::IpLayerEvent<DeviceId<StackTime>, I>>
    for BindingsNonSyncCtxImpl
{
    fn on_event(&mut self, event: netstack3_core::ip::IpLayerEvent<DeviceId<StackTime>, I>) {
        let (device, subnet, has_default_route) = match event {
            netstack3_core::ip::IpLayerEvent::DeviceRouteAdded { device, subnet } => {
                (device, subnet, true)
            }
            netstack3_core::ip::IpLayerEvent::DeviceRouteRemoved { device, subnet } => {
                (device, subnet, false)
            }
        };
        // We only care about the default route.
        if subnet.prefix() != 0 || subnet.network() != I::UNSPECIFIED_ADDRESS {
            return;
        }
        self.notify_interface_update(
            &device,
            InterfaceUpdate::DefaultRouteChanged { version: I::VERSION, has_default_route },
        );
    }
}

impl EventContext<netstack3_core::ip::device::dad::DadEvent<DeviceId<StackTime>>>
    for BindingsNonSyncCtxImpl
{
    fn on_event(&mut self, event: netstack3_core::ip::device::dad::DadEvent<DeviceId<StackTime>>) {
        match event {
            netstack3_core::ip::device::dad::DadEvent::AddressAssigned { device, addr } => self
                .on_event(
                    netstack3_core::ip::device::IpDeviceEvent::<_, Ipv6>::AddressStateChanged {
                        device,
                        addr: addr.into_specified(),
                        state: netstack3_core::ip::device::IpAddressState::Assigned,
                    },
                ),
        }
    }
}

impl
    EventContext<
        netstack3_core::ip::device::route_discovery::Ipv6RouteDiscoveryEvent<DeviceId<StackTime>>,
    > for BindingsNonSyncCtxImpl
{
    fn on_event(
        &mut self,
        _event: netstack3_core::ip::device::route_discovery::Ipv6RouteDiscoveryEvent<
            DeviceId<StackTime>,
        >,
    ) {
        // TODO(https://fxbug.dev/97203): Update forwarding table in response to
        // the event.
    }
}

impl BindingsNonSyncCtxImpl {
    fn notify_interface_update(&self, device: &DeviceId<StackTime>, event: InterfaceUpdate) {
        self.devices
            .get_core_device(device)
            .unwrap_or_else(|| panic!("issued event {:?} for deleted device {:?}", event, device))
            .info()
            .common_info()
            .events
            .notify(event)
            .expect("interfaces worker closed");
    }

    /// Notify `AddressStateProvider.WatchAddressAssignmentState` watchers.
    fn notify_address_update(
        &self,
        device: &DeviceId<StackTime>,
        address: SpecifiedAddr<IpAddr>,
        state: netstack3_core::ip::device::IpAddressState,
    ) {
        // Note that not all addresses have an associated watcher (e.g. loopback
        // address & autoconfigured SLAAC addresses).
        if let Some(address_info) = self
            .devices
            .get_core_device(device)
            .expect("device not present")
            .info()
            .common_info()
            .addresses
            .get(&address)
        {
            address_info
                .assignment_state_sender
                .unbounded_send(state.into_fidl())
                .expect("assignment state receiver unexpectedly disconnected");
        }
    }
}

trait MutableDeviceState {
    /// Invoke a function on the state associated with the device `id`.
    fn update_device_state<F: FnOnce(&mut DeviceInfo<DeviceId<StackTime>>)>(
        &mut self,
        id: u64,
        f: F,
    );
}

impl<NonSyncCtx> MutableDeviceState for Ctx<NonSyncCtx>
where
    NonSyncCtx: NonSyncContext<Instant = StackTime>
        + DeviceStatusNotifier
        + AsRef<Devices<DeviceId<StackTime>>>
        + AsMut<Devices<DeviceId<StackTime>>>,
{
    fn update_device_state<F: FnOnce(&mut DeviceInfo<DeviceId<StackTime>>)>(
        &mut self,
        id: u64,
        f: F,
    ) {
        if let Some(device_info) = self.non_sync_ctx.as_mut().get_device_mut(id) {
            f(device_info);
            self.non_sync_ctx.device_status_changed(id)
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
    NonSyncCtx: NonSyncContext
        + AsRef<Devices<DeviceId<NonSyncCtx::Instant>>>
        + AsMut<Devices<DeviceId<NonSyncCtx::Instant>>>,
>(
    Ctx { sync_ctx, non_sync_ctx }: &mut Ctx<NonSyncCtx>,
    id: u64,
    should_enable: bool,
) -> Result<(), fidl_net_stack::Error> {
    let device = non_sync_ctx.as_mut().get_device_mut(id).ok_or(fidl_net_stack::Error::NotFound)?;
    let core_id = device.core_id().clone();

    let dev_enabled = match device.info_mut() {
        DeviceSpecificInfo::Ethernet(EthernetInfo {
            common_info:
                CommonInfo { admin_enabled, mtu: _, events: _, name: _, control_hook: _, addresses: _ },
            client: _,
            mac: _,
            features: _,
            phy_up,
            interface_control: _,
        })
        | DeviceSpecificInfo::Netdevice(NetdeviceInfo {
            common_info:
                CommonInfo { admin_enabled, mtu: _, events: _, name: _, control_hook: _, addresses: _ },
            handler: _,
            mac: _,
            phy_up,
        }) => *admin_enabled && *phy_up,
        DeviceSpecificInfo::Loopback(LoopbackInfo {
            common_info:
                CommonInfo { admin_enabled, mtu: _, events: _, name: _, control_hook: _, addresses: _ },
            rx_notifier: _,
        }) => *admin_enabled,
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

    netstack3_core::device::update_ipv4_configuration(sync_ctx, non_sync_ctx, &core_id, |config| {
        config.ip_config.ip_enabled = should_enable;
    });
    netstack3_core::device::update_ipv6_configuration(sync_ctx, non_sync_ctx, &core_id, |config| {
        config.ip_config.ip_enabled = should_enable;
    });

    Ok(())
}

fn add_loopback_ip_addrs<NonSyncCtx: NonSyncContext>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    non_sync_ctx: &mut NonSyncCtx,
    loopback: &DeviceId<NonSyncCtx::Instant>,
) -> Result<(), NetstackError> {
    for addr_subnet in [
        AddrSubnetEither::V4(
            AddrSubnet::from_witness(Ipv4::LOOPBACK_ADDRESS, Ipv4::LOOPBACK_SUBNET.prefix())
                .expect("error creating IPv4 loopback AddrSub"),
        ),
        AddrSubnetEither::V6(
            AddrSubnet::from_witness(Ipv6::LOOPBACK_ADDRESS, Ipv6::LOOPBACK_SUBNET.prefix())
                .expect("error creating IPv6 loopback AddrSub"),
        ),
    ] {
        add_ip_addr_subnet(sync_ctx, non_sync_ctx, loopback, addr_subnet)?
    }
    Ok(())
}

impl<NonSyncCtx> InterfaceControl for Ctx<NonSyncCtx>
where
    NonSyncCtx: NonSyncContext
        + AsRef<Devices<DeviceId<NonSyncCtx::Instant>>>
        + AsMut<Devices<DeviceId<NonSyncCtx::Instant>>>,
{
    fn enable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        set_interface_enabled(self, id, true /* should_enable */)
    }

    fn disable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        set_interface_enabled(self, id, false /* should_enable */)
    }
}

type NetstackContext = Arc<Mutex<Ctx<BindingsNonSyncCtxImpl>>>;

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
    type NonSyncCtx = BindingsNonSyncCtxImpl;
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

pub(crate) trait InterfaceControlRunner {
    fn spawn_interface_control(
        &self,
        id: BindingId,
        stop_receiver: futures::channel::oneshot::Receiver<
            fnet_interfaces_admin::InterfaceRemovedReason,
        >,
        control_receiver: futures::channel::mpsc::Receiver<interfaces_admin::OwnedControlHandle>,
    ) -> fuchsia_async::Task<()>;
}

impl InterfaceControlRunner for Netstack {
    fn spawn_interface_control(
        &self,
        id: BindingId,
        stop_receiver: futures::channel::oneshot::Receiver<
            fnet_interfaces_admin::InterfaceRemovedReason,
        >,
        control_receiver: futures::channel::mpsc::Receiver<interfaces_admin::OwnedControlHandle>,
    ) -> fuchsia_async::Task<()> {
        fuchsia_async::Task::spawn(interfaces_admin::run_interface_control(
            self.ctx.clone(),
            id,
            stop_receiver,
            control_receiver,
        ))
    }
}

enum Service {
    Stack(fidl_fuchsia_net_stack::StackRequestStream),
    Socket(fidl_fuchsia_posix_socket::ProviderRequestStream),
    PacketSocket(fidl_fuchsia_posix_socket_packet::ProviderRequestStream),
    RawSocket(fidl_fuchsia_posix_socket_raw::ProviderRequestStream),
    Interfaces(fidl_fuchsia_net_interfaces::StateRequestStream),
    InterfacesAdmin(fidl_fuchsia_net_interfaces_admin::InstallerRequestStream),
    Filter(fidl_fuchsia_net_filter::FilterRequestStream),
    DebugInterfaces(fidl_fuchsia_net_debug::InterfacesRequestStream),
    DebugDiagnostics(fidl::endpoints::ServerEnd<fidl_fuchsia_net_debug::DiagnosticsMarker>),
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

        // The Sender is unused because Loopback should never be canceled.
        let (_loopback_interface_control_stop_sender, loopback_interface_control_stop_receiver) =
            futures::channel::oneshot::channel();
        let loopback_interface_control_task = {
            let mut ctx = netstack.lock().await;
            let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();

            // Add and initialize the loopback interface with the IPv4 and IPv6
            // loopback addresses and on-link routes to the loopback subnets.
            let loopback = netstack3_core::device::add_loopback_device(
                sync_ctx,
                non_sync_ctx,
                DEFAULT_LOOPBACK_MTU,
            )
            .expect("error adding loopback device");
            let devices: &mut Devices<_> = non_sync_ctx.as_mut();
            let (control_sender, control_receiver) =
                interfaces_admin::OwnedControlHandle::new_channel();
            let loopback_rx_notifier = Default::default();
            crate::bindings::devices::spawn_rx_task(
                &loopback_rx_notifier,
                netstack.clone(),
                loopback.clone(),
            );
            let binding_id = devices
                .add_device(loopback.clone(), |id| {
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
                            control_hook: control_sender,
                            addresses: HashMap::new(),
                        },
                        rx_notifier: loopback_rx_notifier,
                    })
                })
                .expect("error adding loopback device");
            // Don't need DAD and IGMP/MLD on loopback.
            netstack3_core::device::update_ipv4_configuration(
                sync_ctx,
                non_sync_ctx,
                &loopback,
                |config| {
                    *config = Ipv4DeviceConfiguration {
                        ip_config: IpDeviceConfiguration { ip_enabled: true, gmp_enabled: false },
                    };
                },
            );
            netstack3_core::device::update_ipv6_configuration(
                sync_ctx,
                non_sync_ctx,
                &loopback,
                |config| {
                    *config = Ipv6DeviceConfiguration {
                        dad_transmits: None,
                        max_router_solicitations: None,
                        slaac_config: SlaacConfiguration {
                            enable_stable_addresses: true,
                            temporary_address_configuration: None,
                        },
                        ip_config: IpDeviceConfiguration { ip_enabled: true, gmp_enabled: false },
                    };
                },
            );
            add_loopback_ip_addrs(sync_ctx, non_sync_ctx, &loopback)
                .expect("error adding loopback addresses");

            // Start servicing timers.
            let BindingsNonSyncCtxImpl {
                rng: _,
                timers,
                devices: _,
                icmp_echo_sockets: _,
                udp_sockets: _,
                tcp_listeners: _,
            } = non_sync_ctx;
            timers.spawn(netstack.clone());

            netstack.spawn_interface_control(
                binding_id,
                loopback_interface_control_stop_receiver,
                control_receiver,
            )
        };

        let interfaces_worker_task = fuchsia_async::Task::spawn(async move {
            let result = interfaces_worker.run().await;
            // The worker is not expected to end for the lifetime of the stack.
            panic!("interfaces worker finished unexpectedly {:?}", result);
        });

        let mut fs = ServiceFs::new_local();
        let _: &mut ServiceFsDir<'_, _> = fs
            .dir("svc")
            .add_service_connector(Service::DebugDiagnostics)
            .add_fidl_service(Service::DebugInterfaces)
            .add_fidl_service(Service::Stack)
            .add_fidl_service(Service::Socket)
            .add_fidl_service(Service::PacketSocket)
            .add_fidl_service(Service::RawSocket)
            .add_fidl_service(Service::Interfaces)
            .add_fidl_service(Service::InterfacesAdmin)
            .add_fidl_service(Service::Filter);

        let services = fs.take_and_serve_directory_handle().context("directory handle")?;

        // Buffer size doesn't matter much, we're just trying to reduce
        // allocations.
        const TASK_CHANNEL_BUFFER_SIZE: usize = 16;
        let (task_sink, task_stream) = mpsc::channel(TASK_CHANNEL_BUFFER_SIZE);
        let work_items = futures::stream::select(
            services.map(WorkItem::Incoming),
            task_stream.map(WorkItem::Task),
        );
        let diagnostics_handler = debug_fidl_worker::DiagnosticsHandler::default();
        let work_items_fut = work_items.for_each_concurrent(None, |wi| async {
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
                WorkItem::Incoming(Service::PacketSocket(socket)) => {
                    socket.serve_with(|rs| socket::packet::serve(rs)).await
                }
                WorkItem::Incoming(Service::RawSocket(socket)) => {
                    socket.serve_with(|rs| socket::raw::serve(rs)).await
                }
                WorkItem::Incoming(Service::Interfaces(interfaces)) => {
                    interfaces
                        .serve_with(|rs| {
                            interfaces_watcher::serve(rs, interfaces_watcher_sink.clone())
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
                WorkItem::Incoming(Service::DebugInterfaces(debug_interfaces)) => {
                    debug_interfaces
                        .serve_with(|rs| debug_fidl_worker::serve_interfaces(netstack.clone(), rs))
                        .await
                }
                WorkItem::Incoming(Service::DebugDiagnostics(debug_diagnostics)) => {
                    diagnostics_handler.serve_diagnostics(debug_diagnostics).await
                }
                WorkItem::Incoming(Service::Filter(filter)) => {
                    filter.serve_with(|rs| filter_worker::serve(rs)).await
                }
                WorkItem::Task(task) => task.await,
            }
        });

        let ((), (), ()) =
            futures::join!(work_items_fut, interfaces_worker_task, loopback_interface_control_task);
        debug!("Services stream finished");
        Ok(())
    }
}
