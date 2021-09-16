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
mod socket;
mod stack_fidl_worker;
mod timers;
mod util;

use std::convert::TryFrom as _;
use std::sync::Arc;
use std::time::Duration;

use fidl_fuchsia_net_stack as fidl_net_stack;
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use futures::{lock::Mutex, StreamExt as _};
use log::{debug, error, trace, warn};
use net_types::ethernet::Mac;
use net_types::ip::{Ipv4, Ipv6};
use packet::{BufferMut, Serializer};
use rand::rngs::OsRng;
use util::ConversionContext;

use context::Lockable;
use devices::{DeviceInfo, Devices, ToggleError};
use socket::udp::UdpSocketCollection;
use timers::TimerDispatcher;

use netstack3_core::{
    context::{InstantContext, RngContext},
    error::NoRouteError,
    handle_timeout,
    icmp::{BufferIcmpEventDispatcher, IcmpConnId, IcmpEventDispatcher, IcmpIpExt},
    initialize_device, remove_device, Ctx, DeviceId, DeviceLayerEventDispatcher, EventDispatcher,
    IpLayerEventDispatcher, StackStateBuilder, TimerId, TransportLayerEventDispatcher,
};

/// A shorthand definition for the rather gnarly type signature of the lock
/// obtained by the [`Lockable`] trait bound on [`StackContext`] that provides a
/// [`Ctx`].
type LockedStackContext<'a, C> = <C as Lockable<'a, Ctx<<C as StackContext>::Dispatcher>>>::Guard;

/// A StackContext that provides an asynchronous lock to a specified `Ctx` to be
/// used in calls into core.
// NOTE(brunodalbo): Currently StackContext only provides a cannonball lock on
// the entire context that is provided to core. The pattern here is expected to
// expand (possibly by submodules such as posix sockets). The vision is
// that we reduce the time that the core context needs to be locked to a
// minimum, until it eventually becomes slow path only.
pub(crate) trait StackContext:
    Send + Sync + 'static + Clone + for<'a> Lockable<'a, Ctx<<Self as StackContext>::Dispatcher>>
{
    /// The [`EventDispatcher`] used by this `StackContext`.
    type Dispatcher: StackDispatcher;
}

/// The dispatcher used by a [`StackContext`].
///
/// `StackDispatcher` has numerous trait bounds that allow a type implementing
/// it to be `EventDispatcher` for core, as well as providing bindings-specific
/// information for all submodules in this crate.
pub(crate) trait StackDispatcher:
    EventDispatcher
    + for<'a> IpLayerEventDispatcher<packet::Buf<&'a mut [u8]>>
    + for<'a> DeviceLayerEventDispatcher<packet::Buf<&'a mut [u8]>>
    + ConversionContext
    + Sync
    + Send
    + AsRef<Devices>
    + AsMut<Devices>
    + AsRef<timers::TimerDispatcher<TimerId>>
    + AsMut<timers::TimerDispatcher<TimerId>>
    + 'static
{
    /// Shorthand method to get a [`DeviceInfo`] from the device's bindings
    /// identifier.
    fn get_device_info(&self, id: u64) -> Option<&DeviceInfo> {
        AsRef::<Devices>::as_ref(self).get_device(id)
    }

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

/// The "real" implementation of [`StackDispatcher`].
///
/// `BindingsDispatcher` is the dispatcher used by [`Netstack`] and it
/// implements the regular network stack operation, sending outgoing frames to
/// the appropriate devices, and proxying calls to their appropriate submodules.
///
/// Implementation of some traits required by [`EventDispatcher`] may be in this
/// crate's submodules, closer to where the implementation logic makes more
/// sense.
pub(crate) struct BindingsDispatcher {
    devices: Devices,
    timers: timers::TimerDispatcher<TimerId>,
    rng: OsRng,
    udp_sockets: socket::udp::UdpSocketCollection,
}

impl BindingsDispatcher {
    fn new() -> Self {
        BindingsDispatcher {
            devices: Devices::default(),
            timers: timers::TimerDispatcher::new(),
            // TODO(joshlf): Is unwrapping safe here? Alternatively,
            // wait until we upgrade to rand 0.7, where OsRng is an
            // empty struct.
            rng: OsRng::new().unwrap(),
            udp_sockets: Default::default(),
        }
    }
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

impl StackDispatcher for BindingsDispatcher {
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

impl AsRef<UdpSocketCollection> for BindingsDispatcher {
    fn as_ref(&self) -> &UdpSocketCollection {
        &self.udp_sockets
    }
}

impl AsMut<UdpSocketCollection> for BindingsDispatcher {
    fn as_mut(&mut self) -> &mut UdpSocketCollection {
        &mut self.udp_sockets
    }
}

impl<D> timers::TimerHandler<TimerId> for Ctx<D>
where
    D: StackDispatcher,
{
    fn handle_expired_timer(&mut self, timer: TimerId) {
        handle_timeout(self, timer)
    }

    fn get_timer_dispatcher(&mut self) -> &mut timers::TimerDispatcher<TimerId> {
        self.dispatcher_mut().as_mut()
    }
}

impl<C> timers::TimerContext<TimerId> for C
where
    C: StackContext,
{
    type Handler = Ctx<C::Dispatcher>;
}

impl<D> ConversionContext for D
where
    D: StackDispatcher,
{
    fn get_core_id(&self, binding_id: u64) -> Option<DeviceId> {
        AsRef::<Devices>::as_ref(self).get_core_id(binding_id)
    }

    fn get_binding_id(&self, core_id: DeviceId) -> Option<u64> {
        AsRef::<Devices>::as_ref(self).get_binding_id(core_id)
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

impl EventDispatcher for BindingsDispatcher {
    fn schedule_timeout_instant(&mut self, time: StackTime, id: TimerId) -> Option<StackTime> {
        self.timers.schedule_timer(id, time)
    }

    fn cancel_timeout(&mut self, id: TimerId) -> Option<StackTime> {
        self.timers.cancel_timer(&id)
    }

    fn cancel_timeouts_with<F: FnMut(&TimerId) -> bool>(&mut self, f: F) {
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
        match self.devices.get_core_device_mut(device) {
            Some(dev) => {
                dev.client_mut().send(frame.as_ref());
            }
            None => error!("Tried to send frame on device that is not listed: {:?}", device),
        }
        Ok(())
    }
}

impl TransportLayerEventDispatcher<Ipv4> for BindingsDispatcher {}
impl TransportLayerEventDispatcher<Ipv6> for BindingsDispatcher {}

impl<I: IcmpIpExt> IcmpEventDispatcher<I> for BindingsDispatcher {
    fn receive_icmp_error(&mut self, _conn: IcmpConnId<I>, _seq_num: u16, _err: I::ErrorCode) {
        // TODO(https://fxbug.dev/47321): implement.
        warn!("IcmpEventDispatcher::receive_icmp_error unimplemented; ignoring error");
    }

    fn close_icmp_connection(&mut self, _conn: IcmpConnId<I>, _err: NoRouteError) {
        // TODO(https://fxbug.dev/47321): implement.
        unimplemented!()
    }
}

impl<I: IcmpIpExt, B: BufferMut> BufferIcmpEventDispatcher<I, B> for BindingsDispatcher {
    fn receive_icmp_echo_reply(&mut self, _conn: IcmpConnId<I>, seq_num: u16, data: B) {
        // TODO(https://fxbug.dev/47321): implement.
        trace!("Received ICMP echo reply w/ seq_num={}, len={}", seq_num, data.len());
    }
}

impl<B: BufferMut> IpLayerEventDispatcher<B> for BindingsDispatcher {}

/// Utility operations that can be performed on a locked `Ctx<D>`.
trait ContextExt {
    /// Invoke a function on the state associated with the device `id`.
    fn update_device_state<F: FnOnce(&mut DeviceInfo)>(&mut self, id: u64, f: F);

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

impl<D: StackDispatcher> ContextExt for Ctx<D> {
    fn update_device_state<F: FnOnce(&mut DeviceInfo)>(&mut self, id: u64, f: F) {
        if let Some(device_info) =
            AsMut::<Devices>::as_mut(self.dispatcher_mut()).get_device_mut(id)
        {
            f(device_info);
            self.dispatcher_mut().device_status_changed(id)
        }
    }

    fn enable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        let (state, disp) = self.state_and_dispatcher();
        let device = disp.get_device_info(id).ok_or(fidl_net_stack::Error::NotFound)?;

        if device.admin_enabled() && device.phy_up() {
            // TODO(rheacock, fxbug.dev/21135): Handle core and driver state in
            // two stages: add device to the core to get an id, then reach into
            // the driver to get updated info before triggering the core to
            // allow traffic on the interface.
            let generate_core_id = |info: &DeviceInfo| {
                state.add_ethernet_device(Mac::new(info.mac().octets), info.mtu())
            };
            match AsMut::<Devices>::as_mut(disp).activate_device(id, generate_core_id) {
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
        match AsMut::<Devices>::as_mut(self.dispatcher_mut()).deactivate_device(id) {
            Ok((core_id, device_info)) => {
                // Sanity check that there is a reason that the device is
                // disabled.
                assert!(!device_info.admin_enabled() || !device_info.phy_up());
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
pub struct Netstack {
    ctx: Arc<Mutex<Ctx<BindingsDispatcher>>>,
}

impl Clone for Netstack {
    fn clone(&self) -> Self {
        Self { ctx: Arc::clone(&self.ctx) }
    }
}

impl StackContext for Netstack {
    type Dispatcher = BindingsDispatcher;
}

impl Netstack {
    /// Creates a new netstack with default options.
    pub fn new() -> Self {
        Self::new_with_builder(StackStateBuilder::default())
    }

    /// Creates a new netstack with the provided core state builder.
    pub fn new_with_builder(builder: StackStateBuilder) -> Self {
        Netstack { ctx: Arc::new(Mutex::new(Ctx::new(builder.build(), BindingsDispatcher::new()))) }
    }

    /// Starts servicing timers.
    async fn spawn_timers(&self) {
        self.lock().await.dispatcher_mut().timers.spawn(self.clone());
    }

    /// Consumes the netstack and starts serving all the FIDL services it
    /// implements to the outgoing service directory.
    pub async fn serve(self) -> Result<(), anyhow::Error> {
        use anyhow::Context as _;

        debug!("Serving netstack");
        self.spawn_timers().await;
        let mut fs = ServiceFs::new_local();
        let _: &mut ServiceFsDir<'_, _> = fs
            .dir("svc")
            .add_fidl_service(|rs: fidl_fuchsia_net_stack::StackRequestStream| {
                stack_fidl_worker::StackFidlWorker::spawn(self.clone(), rs)
            })
            .add_fidl_service(|rs: fidl_fuchsia_posix_socket::ProviderRequestStream| {
                socket::SocketProviderWorker::spawn(self.clone(), rs)
            });
        let fs = fs.take_and_serve_directory_handle().context("directory handle")?;
        let () = fs.collect::<()>().await;
        debug!("Services stream finished");
        Ok(())
    }
}
