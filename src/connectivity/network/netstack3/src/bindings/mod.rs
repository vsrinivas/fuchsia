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
mod interfaces_watcher;
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
use futures::channel::mpsc;
use futures::{lock::Mutex, sink::SinkExt as _, FutureExt as _, StreamExt as _, TryStreamExt as _};
use log::{debug, error, warn};
use packet::{BufferMut, Serializer};
use packet_formats::icmp::{IcmpEchoReply, IcmpMessage, IcmpUnusedCode};
use rand::rngs::OsRng;
use util::ConversionContext;

use context::Lockable;
use devices::{
    CommonInfo, DeviceInfo, DeviceSpecificInfo, Devices, EthernetInfo, LoopbackInfo, ToggleError,
};
use timers::TimerDispatcher;

use net_types::ip::{AddrSubnet, AddrSubnetEither, Ip, Ipv4, Ipv6};
use netstack3_core::{
    add_ip_addr_subnet, add_route,
    context::{InstantContext, RngContext, TimerContext},
    handle_timer, icmp, initialize_device, remove_device, set_ipv4_configuration,
    set_ipv6_configuration, BufferUdpContext, Ctx, DeviceId, DeviceLayerEventDispatcher, EntryDest,
    EntryEither, EventDispatcher, IpDeviceConfiguration, IpExt, IpSockCreationError,
    Ipv4DeviceConfiguration, Ipv6DeviceConfiguration, TimerId, UdpConnId, UdpContext,
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

pub(crate) trait LockableContext: for<'a> Lockable<'a, Ctx<Self::Dispatcher>> {
    type Dispatcher: EventDispatcher;
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
    timers: timers::TimerDispatcher<TimerId>,
    rng: OsRng,
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

impl AsRef<timers::TimerDispatcher<TimerId>> for BindingsDispatcher {
    fn as_ref(&self) -> &TimerDispatcher<TimerId> {
        &self.timers
    }
}

impl AsMut<timers::TimerDispatcher<TimerId>> for BindingsDispatcher {
    fn as_mut(&mut self) -> &mut TimerDispatcher<TimerId> {
        &mut self.timers
    }
}

impl DeviceStatusNotifier for BindingsDispatcher {
    fn device_status_changed(&mut self, _id: u64) {
        // NOTE(brunodalbo) we may want to do more things here in the future,
        // for now this is only intercepted for testing
    }
}

impl<'a> Lockable<'a, Ctx<BindingsDispatcher>> for Netstack {
    type Guard = futures::lock::MutexGuard<'a, Ctx<BindingsDispatcher>>;
    type Fut = futures::lock::MutexLockFuture<'a, Ctx<BindingsDispatcher>>;
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

impl<D> timers::TimerHandler<TimerId> for Ctx<D>
where
    D: EventDispatcher,
    D: AsMut<timers::TimerDispatcher<TimerId>>,
    D: Send + Sync + 'static,
{
    fn handle_expired_timer(&mut self, timer: TimerId) {
        handle_timer(self, timer)
    }

    fn get_timer_dispatcher(&mut self) -> &mut timers::TimerDispatcher<TimerId> {
        self.dispatcher.as_mut()
    }
}

impl<C> timers::TimerContext<TimerId> for C
where
    C: LockableContext,
    C: Clone + Send + Sync + 'static,
    C::Dispatcher: AsMut<timers::TimerDispatcher<TimerId>>,
    C::Dispatcher: Send + Sync + 'static,
{
    type Handler = Ctx<C::Dispatcher>;
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

impl InstantContext for BindingsDispatcher {
    type Instant = StackTime;

    fn now(&self) -> StackTime {
        StackTime(fasync::Time::now())
    }
}

impl RngContext for BindingsDispatcher {
    type Rng = OsRng;

    fn rng(&self) -> &OsRng {
        &self.rng
    }

    fn rng_mut(&mut self) -> &mut OsRng {
        &mut self.rng
    }
}

impl TimerContext<TimerId> for BindingsDispatcher {
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
                common_info: CommonInfo { admin_enabled, mtu: _ },
                client,
                mac: _,
                features: _,
                phy_up,
            }) => {
                if *admin_enabled && *phy_up {
                    client.send(frame.as_ref())
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
    fn receive_icmp_error(
        &mut self,
        id: Result<UdpConnId<I>, UdpListenerId<I>>,
        err: I::ErrorCode,
    ) {
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

trait MutableDeviceState {
    /// Invoke a function on the state associated with the device `id`.
    fn update_device_state<F: FnOnce(&mut DeviceInfo)>(&mut self, id: u64, f: F);
}

impl<D> MutableDeviceState for Ctx<D>
where
    D: EventDispatcher,
    D: AsMut<Devices>,
    D: DeviceStatusNotifier,
{
    fn update_device_state<F: FnOnce(&mut DeviceInfo)>(&mut self, id: u64, f: F) {
        if let Some(device_info) = self.dispatcher.as_mut().get_device_mut(id) {
            f(device_info);
            self.dispatcher.device_status_changed(id)
        }
    }
}

trait InterfaceControl {
    /// Enables an interface, adding it to the core if it is not currently
    /// enabled.
    ///
    /// Both `admin_enabled` and `phy_up` must be true for the interface to be
    /// enabled.
    fn enable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error>;

    /// Disables an interface, removing it from the core if it is currently
    /// enabled.
    ///
    /// Either an Admin (fidl) or Phy change can disable an interface.
    fn disable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error>;
}

impl<D> InterfaceControl for Ctx<D>
where
    D: EventDispatcher,
    D: AsRef<Devices> + AsMut<Devices>,
{
    fn enable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        let Ctx { state, dispatcher } = self;
        let device = dispatcher.as_ref().get_device(id).ok_or(fidl_net_stack::Error::NotFound)?;

        let enabled = match device.info() {
            DeviceSpecificInfo::Ethernet(EthernetInfo {
                common_info: CommonInfo { admin_enabled, mtu: _ },
                client: _,
                mac: _,
                features: _,
                phy_up,
            }) => *admin_enabled && *phy_up,
            DeviceSpecificInfo::Loopback(LoopbackInfo {
                common_info: CommonInfo { admin_enabled, mtu: _ },
            }) => *admin_enabled,
        };

        if enabled {
            // TODO(rheacock, fxbug.dev/21135): Handle core and driver state in
            // two stages: add device to the core to get an id, then reach into
            // the driver to get updated info before triggering the core to
            // allow traffic on the interface.
            let generate_core_id = |info: &DeviceInfo| match info.info() {
                DeviceSpecificInfo::Ethernet(EthernetInfo {
                    common_info: CommonInfo { admin_enabled: _, mtu },
                    client: _,
                    mac,
                    features: _,
                    phy_up: _,
                }) => state.add_ethernet_device(*mac, *mtu),
                DeviceSpecificInfo::Loopback(LoopbackInfo {
                    common_info: CommonInfo { admin_enabled: _, mtu },
                }) => {
                    // Should not panic as we only reach this point if a device
                    // was previously disabled and (as of writing) disabled
                    // devices are not held in core.
                    //
                    // TODO(https://fxbug.dev/92656): Hold disabled interfaces
                    // in core so we can avoid adding interfaces when
                    // transitioning from disabled to enabled.
                    state.add_loopback_device(*mtu).expect("error adding loopback device")
                }
            };
            match dispatcher.as_mut().activate_device(id, generate_core_id) {
                Ok(device_info) => {
                    // we can unwrap core_id here because activate_device just
                    // succeeded.
                    let core_id = device_info.core_id().unwrap();
                    // don't forget to initialize the device in core!
                    initialize_device(self, core_id);
                    Ok(())
                }
                Err(toggle_error) => {
                    match toggle_error {
                        ToggleError::NoChange => Ok(()),
                        // Invalid device ID
                        ToggleError::NotFound => Err(fidl_net_stack::Error::NotFound),
                    }
                }
            }
        } else {
            Ok(())
        }
    }

    fn disable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        match self.dispatcher.as_mut().deactivate_device(id) {
            Ok((core_id, device_info)) => {
                // Sanity check that there is a reason that the device is
                // disabled.
                assert!(match device_info.info() {
                    DeviceSpecificInfo::Ethernet(EthernetInfo {
                        common_info: CommonInfo { admin_enabled, mtu: _ },
                        client: _,
                        mac: _,
                        features: _,
                        phy_up,
                    }) => !admin_enabled || !phy_up,
                    DeviceSpecificInfo::Loopback(LoopbackInfo {
                        common_info: CommonInfo { admin_enabled, mtu: _ },
                    }) => {
                        !admin_enabled
                    }
                });

                // Disabling the interface deactivates it in the bindings, and
                // will remove it completely from the core.
                match remove_device(self, core_id) {
                    // TODO(rheacock): schedule and send the received frames
                    Some(_) => Ok(()),
                    None => Ok(()),
                }
            }
            Err(toggle_error) => {
                match toggle_error {
                    ToggleError::NoChange => Ok(()),
                    // Invalid device ID
                    ToggleError::NotFound => Err(fidl_net_stack::Error::NotFound),
                }
            }
        }
    }
}

/// The netstack.
///
/// Provides the entry point for creating a netstack to be served as a
/// component.
#[derive(Default)]
pub struct Netstack {
    ctx: Arc<Mutex<Ctx<BindingsDispatcher>>>,
}

impl Clone for Netstack {
    fn clone(&self) -> Self {
        Self { ctx: Arc::clone(&self.ctx) }
    }
}

impl LockableContext for Netstack {
    type Dispatcher = BindingsDispatcher;
}

enum Service {
    Stack(fidl_fuchsia_net_stack::StackRequestStream),
    Socket(fidl_fuchsia_posix_socket::ProviderRequestStream),
    Interfaces(fidl_fuchsia_net_interfaces::StateRequestStream),
    Debug(fidl_fuchsia_net_debug::InterfacesRequestStream),
}

enum Task {
    Watcher(interfaces_watcher::Watcher),
}

enum WorkItem {
    Incoming(Service),
    Spawned(Task),
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

impl Netstack {
    /// Consumes the netstack and starts serving all the FIDL services it
    /// implements to the outgoing service directory.
    pub async fn serve(self) -> Result<(), anyhow::Error> {
        use anyhow::Context as _;

        debug!("Serving netstack");

        {
            let mut ctx = self.lock().await;
            let ctx = ctx.deref_mut();

            // Add and initialize the loopback interface with the IPv4 and IPv6
            // loopback addresses and on-link routes to the loopback subnets.
            let Ctx {
                state,
                dispatcher:
                    BindingsDispatcher {
                        devices,
                        timers: _,
                        rng: _,
                        icmp_echo_sockets: _,
                        udp_sockets: _,
                    },
            } = ctx;
            let loopback = state
                .add_loopback_device(DEFAULT_LOOPBACK_MTU)
                .expect("error adding loopback device");
            let _binding_id: u64 = devices
                .add_active_device(
                    loopback,
                    DeviceSpecificInfo::Loopback(LoopbackInfo {
                        common_info: CommonInfo { mtu: DEFAULT_LOOPBACK_MTU, admin_enabled: true },
                    }),
                )
                .expect("error adding loopback device");
            initialize_device(ctx, loopback);
            // Don't need DAD and IGMP/MLD on loopback.
            set_ipv4_configuration(
                ctx,
                loopback,
                Ipv4DeviceConfiguration { ip_config: IpDeviceConfiguration { gmp_enabled: false } },
            );
            set_ipv6_configuration(
                ctx,
                loopback,
                Ipv6DeviceConfiguration {
                    dad_transmits: None,
                    ip_config: IpDeviceConfiguration { gmp_enabled: false },
                },
            );
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
                EntryEither::new(
                    Ipv4::LOOPBACK_SUBNET.into(),
                    EntryDest::Local { device: loopback },
                )
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
                EntryEither::new(
                    Ipv6::LOOPBACK_SUBNET.into(),
                    EntryDest::Local { device: loopback },
                )
                .expect("error creating IPv6 route entry"),
            )
            .expect("error adding IPv6 loopback on-link subnet route");

            // Start servicing timers.
            let Ctx {
                state: _,
                dispatcher:
                    BindingsDispatcher {
                        devices: _,
                        timers,
                        rng: _,
                        icmp_echo_sockets: _,
                        udp_sockets: _,
                    },
            } = ctx;
            timers.spawn(self.clone());
        }

        let mut fs = ServiceFs::new_local();
        let _: &mut ServiceFsDir<'_, _> = fs
            .dir("svc")
            .add_fidl_service(Service::Debug)
            .add_fidl_service(Service::Stack)
            .add_fidl_service(Service::Socket)
            .add_fidl_service(Service::Interfaces);

        let services = fs.take_and_serve_directory_handle().context("directory handle")?;
        let (work_items, tasks) = {
            let (sender, receiver) = mpsc::unbounded();
            (
                futures::stream::select(
                    receiver.map(WorkItem::Spawned),
                    services.map(WorkItem::Incoming),
                ),
                sender,
            )
        };
        let () = work_items
            .for_each_concurrent(None, |wi| async {
                match wi {
                    WorkItem::Incoming(Service::Stack(stack)) => {
                        stack
                            .serve_with(|rs| {
                                stack_fidl_worker::StackFidlWorker::serve(self.clone(), rs)
                            })
                            .await
                    }
                    WorkItem::Incoming(Service::Socket(socket)) => {
                        socket.serve_with(|rs| socket::serve(self.clone(), rs)).await
                    }
                    WorkItem::Incoming(Service::Interfaces(interfaces)) => {
                        interfaces
                            .serve_with(|rs| {
                                interfaces_watcher::serve(
                                    self.clone(),
                                    rs,
                                    tasks.clone().with(|w| {
                                        futures::future::ok::<_, futures::channel::mpsc::SendError>(
                                            Task::Watcher(w),
                                        )
                                    }),
                                )
                            })
                            .await
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
                    WorkItem::Spawned(Task::Watcher(watcher)) => {
                        watcher.await.unwrap_or_else(|err| {
                            error!("fuchsia.net.interfaces.Watcher error: {}", err)
                        })
                    }
                }
            })
            .await;
        debug!("Services stream finished");
        Ok(())
    }
}
