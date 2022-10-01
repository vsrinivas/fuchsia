// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{HashMap, HashSet};
use std::convert::TryFrom as _;
use std::num::NonZeroU16;
use std::ops::{Deref as _, DerefMut as _};
use std::sync::{Arc, Once};

use anyhow::{format_err, Context as _, Error};
use assert_matches::assert_matches;
use fidl_fuchsia_net as fidl_net;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fidl_fuchsia_net_stack as fidl_net_stack;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fidl_fuchsia_netemul_network as net;
use fuchsia_async as fasync;
use futures::lock::Mutex;
use net_types::ip::{Ip as _, Ipv4, Ipv6, Subnet};
use net_types::{
    ip::{AddrSubnetEither, IpAddr, Ipv4Addr, Ipv6Addr},
    SpecifiedAddr,
};
use netstack3_core::{
    context::{CounterContext, EventContext, InstantContext, RngContext, TimerContext},
    device::{BufferDeviceLayerEventDispatcher, DeviceId, DeviceLayerEventDispatcher},
    get_all_ip_addr_subnets,
    ip::{
        icmp::{BufferIcmpContext, IcmpConnId, IcmpContext, IcmpIpExt},
        types::{AddableEntry, AddableEntryEither},
        IpExt,
    },
    transport::{
        tcp::socket::{ListenerId, TcpNonSyncContext},
        udp::{BufferUdpContext, UdpBoundId, UdpContext},
    },
    Ctx, NonSyncContext, TimerId,
};
use packet::{Buf, BufferMut, Serializer};
use packet_formats::icmp::{IcmpEchoReply, IcmpMessage, IcmpUnusedCode};
use rand::rngs::OsRng;

use crate::bindings::{
    context::Lockable,
    devices::{
        BindingId, CommonInfo, DeviceInfo, DeviceSpecificInfo, Devices, EthernetInfo, LoopbackInfo,
        NetdeviceInfo,
    },
    interfaces_admin,
    socket::{
        datagram::{IcmpEcho, SocketCollectionIpExt, Udp},
        stream,
    },
    util::{ConversionContext as _, IntoFidl as _, TryFromFidlWithContext as _, TryIntoFidl as _},
    BindingsNonSyncCtxImpl, DeviceStatusNotifier, InterfaceControlRunner,
    InterfaceEventProducerFactory as _, InterfaceProperties, InterfaceUpdate, LockableContext,
    RequestStreamExt as _, StackTime, DEFAULT_LOOPBACK_MTU, LOOPBACK_NAME,
};

/// log::Log implementation that uses stdout.
///
/// Useful when debugging tests.
struct Logger;

impl log::Log for Logger {
    fn enabled(&self, _metadata: &log::Metadata<'_>) -> bool {
        true
    }

    fn log(&self, record: &log::Record<'_>) {
        println!("[{}] ({}) {}", record.level(), record.module_path().unwrap_or(""), record.args())
    }

    fn flush(&self) {}
}

static LOGGER: Logger = Logger;

static LOGGER_ONCE: Once = Once::new();

/// Install a logger for tests.
pub(crate) fn set_logger_for_test() {
    // log::set_logger will panic if called multiple times; using a Once makes
    // set_logger_for_test idempotent
    LOGGER_ONCE.call_once(|| {
        log::set_logger(&LOGGER).unwrap();
        log::set_max_level(log::LevelFilter::Trace);
    })
}

/// A dispatcher that can be used for tests with the ability to optionally
/// intercept events to use as signals during testing.
///
/// `TestDispatcher` implements [`StackDispatcher`] and keeps an internal
/// [`BindingsDispatcherState`]. All the traits that are needed to have a
/// correct [`EventDispatcher`] are re-implemented by it so any events can be
/// short circuited into internal event watchers as opposed to routing into the
/// internal [`BindingsDispatcherState]`.
#[derive(Default)]
pub(crate) struct TestNonSyncCtx {
    ctx: BindingsNonSyncCtxImpl,
    /// A oneshot signal that is hit whenever changes to interface status occur
    /// and it is set.
    status_changed_signal: Option<futures::channel::oneshot::Sender<()>>,
    /// Holds when timers are scheduled to fire.
    ///
    /// Note that the timers will not actually fire/be dispatched.
    scheduled_timers: HashMap<TimerId, StackTime>,
}

impl TestNonSyncCtx {
    /// Shorthand method to get a [`DeviceInfo`] from the device's bindings
    /// identifier.
    fn get_device_info(&self, id: u64) -> Option<&DeviceInfo> {
        AsRef::<Devices>::as_ref(self).get_device(id)
    }
}

impl DeviceStatusNotifier for TestNonSyncCtx {
    fn device_status_changed(&mut self, id: u64) {
        if let Some(s) = self.status_changed_signal.take() {
            s.send(()).unwrap();
        }
        // we can always send that forward to the real dispatcher, no need to
        // short-circuit it.
        self.ctx.device_status_changed(id);
    }
}

impl<T> AsRef<T> for TestNonSyncCtx
where
    BindingsNonSyncCtxImpl: AsRef<T>,
{
    fn as_ref(&self) -> &T {
        self.ctx.as_ref()
    }
}

impl<T> AsMut<T> for TestNonSyncCtx
where
    BindingsNonSyncCtxImpl: AsMut<T>,
{
    fn as_mut(&mut self) -> &mut T {
        self.ctx.as_mut()
    }
}

impl stream::SocketWorkerDispatcher for TestNonSyncCtx {
    fn register_listener(&mut self, id: ListenerId, socket: fuchsia_zircon::Socket) {
        self.ctx.register_listener(id, socket)
    }

    fn unregister_listener(&mut self, id: ListenerId) {
        self.ctx.unregister_listener(id)
    }
}

impl CounterContext for TestNonSyncCtx {
    fn increment_counter(&mut self, key: &'static str) {
        self.ctx.increment_counter(key)
    }
}

impl RngContext for TestNonSyncCtx {
    type Rng = OsRng;

    fn rng(&self) -> &OsRng {
        &self.ctx.rng
    }

    fn rng_mut(&mut self) -> &mut OsRng {
        &mut self.ctx.rng
    }
}

impl InstantContext for TestNonSyncCtx {
    type Instant = StackTime;

    fn now(&self) -> StackTime {
        self.ctx.now()
    }
}

impl TcpNonSyncContext for TestNonSyncCtx {
    type ReceiveBuffer = stream::ReceiveBufferWithZirconSocket;
    type SendBuffer = stream::SendBufferWithZirconSocket;
    type ReturnedBuffers = stream::PeerZirconSocketAndWatcher;
    type ProvidedBuffers = stream::LocalZirconSocketAndNotifier;

    fn on_new_connection(&mut self, _listener: ListenerId) {}

    fn new_passive_open_buffers() -> (Self::ReceiveBuffer, Self::SendBuffer, Self::ReturnedBuffers)
    {
        BindingsNonSyncCtxImpl::new_passive_open_buffers()
    }
}

// An implementation that keeps track of when timers are scheduled to fire but
// does not actually fire timers.
//
// This is OK as current tests do not expect to fire timers.
impl TimerContext<TimerId> for TestNonSyncCtx {
    fn schedule_timer_instant(&mut self, time: StackTime, id: TimerId) -> Option<StackTime> {
        self.scheduled_timers.insert(id, time)
    }

    fn cancel_timer(&mut self, id: TimerId) -> Option<StackTime> {
        self.scheduled_timers.remove(&id)
    }

    fn cancel_timers_with<F: FnMut(&TimerId) -> bool>(&mut self, mut f: F) {
        self.scheduled_timers.retain(|id, _time| !f(id))
    }

    fn scheduled_instant(&self, id: TimerId) -> Option<StackTime> {
        self.scheduled_timers.get(&id).cloned()
    }
}

impl DeviceLayerEventDispatcher for TestNonSyncCtx {
    fn wake_rx_task(&mut self, device: DeviceId) {
        self.ctx.wake_rx_task(device)
    }
}

impl<B: BufferMut> BufferDeviceLayerEventDispatcher<B> for TestNonSyncCtx {
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: DeviceId,
        frame: S,
    ) -> Result<(), S> {
        self.ctx.send_frame(device, frame)
    }
}

impl<T: 'static + Send> EventContext<T> for TestNonSyncCtx {
    fn on_event(&mut self, _event: T) {}
}

impl<I: SocketCollectionIpExt<Udp> + IcmpIpExt> UdpContext<I> for TestNonSyncCtx {
    fn receive_icmp_error(&mut self, id: UdpBoundId<I>, err: I::ErrorCode) {
        UdpContext::receive_icmp_error(&mut self.ctx, id, err)
    }
}

impl<I: SocketCollectionIpExt<Udp> + IpExt, B: BufferMut> BufferUdpContext<I, B>
    for TestNonSyncCtx
{
    fn receive_udp_from_conn(
        &mut self,
        conn: netstack3_core::transport::udp::UdpConnId<I>,
        src_ip: I::Addr,
        src_port: NonZeroU16,
        body: &B,
    ) {
        self.ctx.receive_udp_from_conn(conn, src_ip, src_port, body)
    }

    /// Receive a UDP packet for a listener.
    fn receive_udp_from_listen(
        &mut self,
        listener: netstack3_core::transport::udp::UdpListenerId<I>,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        src_port: Option<NonZeroU16>,
        body: &B,
    ) {
        self.ctx.receive_udp_from_listen(listener, src_ip, dst_ip, src_port, body)
    }
}

impl<I: SocketCollectionIpExt<IcmpEcho> + IcmpIpExt> IcmpContext<I> for TestNonSyncCtx {
    fn receive_icmp_error(&mut self, conn: IcmpConnId<I>, seq_num: u16, err: I::ErrorCode) {
        IcmpContext::<I>::receive_icmp_error(&mut self.ctx, conn, seq_num, err)
    }
}

impl<I, B> BufferIcmpContext<I, B> for TestNonSyncCtx
where
    I: SocketCollectionIpExt<IcmpEcho> + IcmpIpExt,
    B: BufferMut,
    IcmpEchoReply: for<'a> IcmpMessage<I, &'a [u8], Code = IcmpUnusedCode>,
{
    fn receive_icmp_echo_reply(
        &mut self,
        conn: IcmpConnId<I>,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        id: u16,
        seq_num: u16,
        data: B,
    ) {
        self.ctx.receive_icmp_echo_reply(conn, src_ip, dst_ip, id, seq_num, data)
    }
}

#[derive(Clone)]
/// A netstack context for testing.
pub(crate) struct TestContext {
    ctx: Arc<Mutex<Ctx<TestNonSyncCtx>>>,
    _interfaces_worker: Arc<super::interfaces_watcher::Worker>,
    interfaces_sink: super::interfaces_watcher::WorkerInterfaceSink,
}

impl TestContext {
    fn new() -> Self {
        let (worker, _, interfaces_sink) = super::interfaces_watcher::Worker::new();
        Self {
            ctx: Arc::new(Mutex::new(Ctx::default())),
            _interfaces_worker: Arc::new(worker),
            interfaces_sink,
        }
    }
}

impl super::InterfaceEventProducerFactory for TestContext {
    fn create_interface_event_producer(
        &self,
        id: super::devices::BindingId,
        properties: super::interfaces_watcher::InterfaceProperties,
    ) -> super::interfaces_watcher::InterfaceEventProducer {
        self.interfaces_sink.add_interface(id, properties).expect("interfaces worker not running")
    }
}

impl<'a> Lockable<'a, Ctx<TestNonSyncCtx>> for TestContext {
    type Guard = futures::lock::MutexGuard<'a, Ctx<TestNonSyncCtx>>;
    type Fut = futures::lock::MutexLockFuture<'a, Ctx<TestNonSyncCtx>>;
    fn lock(&'a self) -> Self::Fut {
        self.ctx.lock()
    }
}

impl LockableContext for TestContext {
    type NonSyncCtx = TestNonSyncCtx;
}

impl InterfaceControlRunner for TestContext {
    fn spawn_interface_control(
        &self,
        _id: BindingId,
        stop_receiver: futures::channel::oneshot::Receiver<
            fnet_interfaces_admin::InterfaceRemovedReason,
        >,
        _control_receiver: futures::channel::mpsc::Receiver<interfaces_admin::OwnedControlHandle>,
    ) -> fuchsia_async::Task<()> {
        // Placeholder interface_control implementation that waits to be canceled.
        fuchsia_async::Task::spawn(async move {
            let _removed_reason: fnet_interfaces_admin::InterfaceRemovedReason =
                stop_receiver.await.expect("failed to receive stop");
        })
    }
}

/// A holder for a [`TestContext`].
/// `TestStack` is obtained from [`TestSetupBuilder`] and offers utility methods
/// to connect to the FIDL APIs served by [`TestContext`], as well as keeps
/// track of configured interfaces during the setup procedure.
pub(crate) struct TestStack {
    ctx: TestContext,
    endpoint_ids: HashMap<String, u64>,
}

struct InterfaceInfo {
    admin_enabled: bool,
    phy_up: bool,
    addresses: Vec<fidl_net::Subnet>,
}

impl TestStack {
    /// Connects to the `fuchsia.net.stack.Stack` service.
    pub(crate) fn connect_stack(&self) -> Result<fidl_fuchsia_net_stack::StackProxy, Error> {
        let (stack, rs) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_stack::StackMarker>()?;
        fasync::Task::spawn(rs.serve_with(|rs| {
            crate::bindings::stack_fidl_worker::StackFidlWorker::serve(self.ctx.clone(), rs)
        }))
        .detach();
        Ok(stack)
    }

    /// Connects to the `fuchsia.posix.socket.Provider` service.
    pub(crate) fn connect_socket_provider(
        &self,
    ) -> Result<fidl_fuchsia_posix_socket::ProviderProxy, Error> {
        let (stack, rs) = fidl::endpoints::create_proxy_and_stream::<
            fidl_fuchsia_posix_socket::ProviderMarker,
        >()?;
        fasync::Task::spawn(
            rs.serve_with(|rs| crate::bindings::socket::serve(self.ctx.clone(), rs)),
        )
        .detach();
        Ok(stack)
    }

    fn is_interface_link_up(info: &DeviceInfo) -> bool {
        match info.info() {
            DeviceSpecificInfo::Ethernet(EthernetInfo {
                common_info: _,
                client: _,
                mac: _,
                features: _,
                phy_up,
                interface_control: _,
            })
            | DeviceSpecificInfo::Netdevice(NetdeviceInfo {
                common_info: _,
                handler: _,
                mac: _,
                phy_up,
            }) => *phy_up,
            DeviceSpecificInfo::Loopback(LoopbackInfo { common_info: _, rx_notifier: _ }) => true,
        }
    }

    /// Waits for interface with given `if_id` to come online.
    pub(crate) async fn wait_for_interface_online(&mut self, if_id: u64) {
        self.wait_for_interface_status(if_id, Self::is_interface_link_up).await;
    }

    /// Waits for interface with given `if_id` to go offline.
    pub(crate) async fn wait_for_interface_offline(&mut self, if_id: u64) {
        self.wait_for_interface_status(if_id, |info| !Self::is_interface_link_up(info)).await;
    }

    async fn wait_for_interface_status<F: Fn(&DeviceInfo) -> bool>(
        &mut self,
        if_id: u64,
        check_status: F,
    ) {
        loop {
            let signal = {
                let mut ctx = self.ctx.lock().await;
                if check_status(
                    ctx.non_sync_ctx
                        .get_device_info(if_id)
                        .expect("Wait for interface status on unknown device"),
                ) {
                    return;
                }
                let (sender, receiver) = futures::channel::oneshot::channel();
                ctx.non_sync_ctx.status_changed_signal = Some(sender);
                receiver
            };
            let () = signal.await.expect("Stream ended before it was signalled");
        }
    }

    /// Gets an installed interface identifier from the configuration endpoint
    /// `index`.
    pub(crate) fn get_endpoint_id(&self, index: usize) -> u64 {
        self.get_named_endpoint_id(test_ep_name(index))
    }

    /// Gets an installed interface identifier from the configuration endpoint
    /// `name`.
    pub(crate) fn get_named_endpoint_id(&self, name: impl Into<String>) -> u64 {
        *self.endpoint_ids.get(&name.into()).unwrap()
    }

    /// Creates a new `TestStack`.
    pub(crate) fn new() -> Self {
        let ctx = TestContext::new();
        TestStack { ctx, endpoint_ids: HashMap::new() }
    }

    /// Helper function to invoke a closure that provides a locked
    /// [`Ctx< BindingsContext>`] provided by this `TestStack`.
    pub(crate) async fn with_ctx<R, F: FnOnce(&mut Ctx<TestNonSyncCtx>) -> R>(
        &mut self,
        f: F,
    ) -> R {
        let mut ctx = self.ctx.lock().await;
        f(ctx.deref_mut())
    }

    /// Acquire a lock on this `TestStack`'s context.
    pub(crate) async fn ctx(&self) -> <TestContext as Lockable<'_, Ctx<TestNonSyncCtx>>>::Guard {
        self.ctx.lock().await
    }

    async fn get_interface_info(&self, id: u64) -> InterfaceInfo {
        let ctx = self.ctx().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref();
        let device = non_sync_ctx.get_device_info(id).expect("device");
        let addresses = get_all_ip_addr_subnets(sync_ctx, device.core_id())
            .into_iter()
            .map(|addr| addr.try_into_fidl().expect("convert to FIDL"))
            .collect();

        let (admin_enabled, phy_up) = assert_matches::assert_matches!(
            device.info(),
            DeviceSpecificInfo::Ethernet(EthernetInfo {
                common_info: CommonInfo {
                    admin_enabled,
                    mtu: _,
                    events: _,
                    name: _,
                    control_hook: _,
                    addresses: _,
                },
                client: _,
                mac: _,
                features: _,
                phy_up,
                interface_control: _,
            }) => (*admin_enabled, *phy_up));
        InterfaceInfo { admin_enabled, phy_up, addresses }
    }
}

/// A test setup that than contain multiple stack instances networked together.
pub(crate) struct TestSetup {
    // Let connection to sandbox be made lazily, so a netemul sandbox is not
    // created for tests that don't need it.
    sandbox: Option<netemul::TestSandbox>,
    // Keep around the handle to the virtual networks and endpoints we create to
    // ensure they're not cleaned up before test execution is complete.
    _network: Option<net::SetupHandleProxy>,
    stacks: Vec<TestStack>,
}

impl TestSetup {
    /// Gets the [`TestStack`] at index `i`.
    pub(crate) fn get(&mut self, i: usize) -> &mut TestStack {
        &mut self.stacks[i]
    }

    /// Acquires a lock on the [`TestContext`] at index `i`.
    pub(crate) async fn ctx(
        &mut self,
        i: usize,
    ) -> <TestContext as Lockable<'_, Ctx<TestNonSyncCtx>>>::Guard {
        self.get(i).ctx.lock().await
    }

    async fn get_endpoint(
        &mut self,
        ep_name: &str,
    ) -> Result<fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>, Error>
    {
        let epm = self.sandbox().get_endpoint_manager()?;
        let ep = match epm.get_endpoint(ep_name).await? {
            Some(ep) => ep.into_proxy()?,
            None => {
                return Err(format_err!("Failed to retrieve endpoint {}", ep_name));
            }
        };

        match ep.get_device().await? {
            fidl_fuchsia_netemul_network::DeviceConnection::Ethernet(e) => Ok(e),
            fidl_fuchsia_netemul_network::DeviceConnection::NetworkDevice(n) => {
                todo!("(48853) Support NetworkDevice for integration tests.  Got unexpected network device {:?}.", n)
            }
        }
    }

    /// Changes a named endpoint `ep_name` link status to `up`.
    pub(crate) async fn set_endpoint_link_up(
        &mut self,
        ep_name: &str,
        up: bool,
    ) -> Result<(), Error> {
        let epm = self.sandbox().get_endpoint_manager()?;
        if let Some(ep) = epm.get_endpoint(ep_name).await? {
            ep.into_proxy()?.set_link_up(up).await?;
            Ok(())
        } else {
            Err(format_err!("Failed to retrieve endpoint {}", ep_name))
        }
    }

    /// Creates a new empty `TestSetup`.
    fn new() -> Result<Self, Error> {
        set_logger_for_test();
        Ok(Self { sandbox: None, _network: None, stacks: Vec::new() })
    }

    fn sandbox(&mut self) -> &netemul::TestSandbox {
        self.sandbox
            .get_or_insert_with(|| netemul::TestSandbox::new().expect("create netemul sandbox"))
    }

    async fn configure_network(
        &mut self,
        ep_names: impl Iterator<Item = String>,
    ) -> Result<(), Error> {
        let handle = self
            .sandbox()
            .setup_networks(vec![net::NetworkSetup {
                name: "test_net".to_owned(),
                config: net::NetworkConfig::EMPTY,
                endpoints: ep_names.map(|name| new_endpoint_setup(name)).collect(),
            }])
            .await
            .context("create network")?
            .into_proxy();

        self._network = Some(handle);
        Ok(())
    }

    fn add_stack(&mut self, stack: TestStack) {
        self.stacks.push(stack)
    }
}

/// Helper function to retrieve the internal name of an endpoint specified only
/// by an index `i`.
pub(crate) fn test_ep_name(i: usize) -> String {
    format!("test-ep{}", i)
}

fn new_endpoint_setup(name: String) -> net::EndpointSetup {
    net::EndpointSetup { config: None, link_up: true, name }
}

/// A builder structure for [`TestSetup`].
pub(crate) struct TestSetupBuilder {
    endpoints: Vec<String>,
    stacks: Vec<StackSetupBuilder>,
}

impl TestSetupBuilder {
    /// Creates an empty `SetupBuilder`.
    pub(crate) fn new() -> Self {
        Self { endpoints: Vec::new(), stacks: Vec::new() }
    }

    /// Adds an automatically-named endpoint to the setup builder. The automatic
    /// names are taken using [`test_ep_name`] with index starting at 1.
    ///
    /// Multiple calls to `add_endpoint` will result in the creation of multiple
    /// endpoints with sequential indices.
    pub(crate) fn add_endpoint(self) -> Self {
        let id = self.endpoints.len() + 1;
        self.add_named_endpoint(test_ep_name(id))
    }

    /// Ads an endpoint with a given `name`.
    pub(crate) fn add_named_endpoint(mut self, name: impl Into<String>) -> Self {
        self.endpoints.push(name.into());
        self
    }

    /// Adds a stack to create upon building. Stack configuration is provided
    /// by [`StackSetupBuilder`].
    pub(crate) fn add_stack(mut self, stack: StackSetupBuilder) -> Self {
        self.stacks.push(stack);
        self
    }

    /// Adds an empty stack to create upon building. An empty stack contains
    /// no endpoints.
    pub(crate) fn add_empty_stack(mut self) -> Self {
        self.stacks.push(StackSetupBuilder::new());
        self
    }

    /// Attempts to build a [`TestSetup`] with the provided configuration.
    pub(crate) async fn build(self) -> Result<TestSetup, Error> {
        let mut setup = TestSetup::new()?;
        if !self.endpoints.is_empty() {
            let () = setup.configure_network(self.endpoints.into_iter()).await?;
        }

        // configure all the stacks:
        for stack_cfg in self.stacks.into_iter() {
            println!("Adding stack: {:?}", stack_cfg);
            let mut stack = TestStack::new();
            let netstack = stack.ctx.clone();
            let binding_id = stack
                .with_ctx(|Ctx { sync_ctx, non_sync_ctx }| {
                    let loopback = netstack3_core::device::add_loopback_device(
                        sync_ctx,
                        non_sync_ctx,
                        DEFAULT_LOOPBACK_MTU,
                    )
                    .expect("add loopback device");
                    crate::bindings::add_loopback_ip_addrs(sync_ctx, non_sync_ctx, loopback)
                        .expect("add loopback addresses");

                    netstack3_core::device::update_ipv4_configuration(
                        sync_ctx,
                        non_sync_ctx,
                        loopback,
                        |config| {
                            config.ip_config.ip_enabled = true;
                        },
                    );
                    netstack3_core::device::update_ipv6_configuration(
                        sync_ctx,
                        non_sync_ctx,
                        loopback,
                        |config| {
                            config.ip_config.ip_enabled = true;
                        },
                    );

                    let devices: &mut Devices = non_sync_ctx.as_mut();
                    let (control_sender, _control_receiver) =
                        interfaces_admin::OwnedControlHandle::new_channel();
                    let loopback_rx_notifier = Default::default();
                    crate::bindings::devices::spawn_rx_task(
                        &loopback_rx_notifier,
                        netstack.clone(),
                        loopback,
                    );
                    devices
                        .add_device(loopback, |id| {
                            let events = netstack.create_interface_event_producer(
                                id,
                                InterfaceProperties {
                                    name: LOOPBACK_NAME.to_string(),
                                    device_class:
                                        fidl_fuchsia_net_interfaces::DeviceClass::Loopback(
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
                        .expect("error adding loopback device")
                })
                .await;

            assert_eq!(stack.endpoint_ids.insert(LOOPBACK_NAME.to_string(), binding_id), None);

            for (ep_name, addr) in stack_cfg.endpoints.into_iter() {
                // get the endpoint from the sandbox config:
                let endpoint = setup.get_endpoint(&ep_name).await?;
                let cli = stack.connect_stack()?;
                let if_id = add_stack_endpoint(&cli, endpoint).await?;
                // We'll ALWAYS await for the newly created interface to come up
                // online before returning, so users of `TestSetupBuilder` can
                // be 100% sure of the state once the setup is done.
                stack.wait_for_interface_online(if_id).await;
                if let Some(addr) = addr {
                    configure_endpoint_address(&cli, if_id, addr).await?;
                }

                assert_eq!(stack.endpoint_ids.insert(ep_name, if_id), None);
            }

            setup.add_stack(stack)
        }

        Ok(setup)
    }
}

/// Shorthand function to create an IPv4 [`AddrSubnetEither`].
///
/// # Panics
///
/// May panic if `prefix` is longer than the number of bits in this type of IP
/// address (32 for IPv4), or if `ip` is not a unicast address in the resulting
/// subnet (see [`net_types::ip::IpAddress::is_unicast_in_subnet`]).
pub fn new_ipv4_addr_subnet(ip: [u8; 4], prefix: u8) -> AddrSubnetEither {
    AddrSubnetEither::new(IpAddr::V4(Ipv4Addr::from(ip)), prefix).unwrap()
}

/// Shorthand function to create an IPv6 [`AddrSubnetEither`].
///
/// # Panics
///
/// May panic if `prefix` is longer than the number of bits in this type of IP
/// address (128 for IPv6), or if `ip` is not a unicast address in the resulting
/// subnet (see [`net_types::ip::IpAddress::is_unicast_in_subnet`]).
pub fn new_ipv6_addr_subnet(ip: [u8; 16], prefix: u8) -> AddrSubnetEither {
    AddrSubnetEither::new(IpAddr::V6(Ipv6Addr::from(ip)), prefix).unwrap()
}

/// Helper struct to create stack configuration for [`TestSetupBuilder`].
#[derive(Debug)]
pub struct StackSetupBuilder {
    endpoints: Vec<(String, Option<AddrSubnetEither>)>,
}

impl StackSetupBuilder {
    /// Creates a new empty stack (no endpoints) configuration.
    pub(crate) fn new() -> Self {
        Self { endpoints: Vec::new() }
    }

    /// Adds endpoint number  `index` with optional address configuration
    /// `address` to the builder.
    fn add_endpoint(self, index: usize, address: Option<AddrSubnetEither>) -> Self {
        self.add_named_endpoint(test_ep_name(index), address)
    }

    /// Adds named endpoint `name` with optional address configuration `address`
    /// to the builder.
    pub(crate) fn add_named_endpoint(
        mut self,
        name: impl Into<String>,
        address: Option<AddrSubnetEither>,
    ) -> Self {
        self.endpoints.push((name.into(), address));
        self
    }
}

async fn add_stack_endpoint(
    cli: &fidl_fuchsia_net_stack::StackProxy,
    endpoint: fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>,
) -> Result<u64, Error> {
    // add interface:
    let if_id = cli
        .add_ethernet_interface("fake_topo_path", endpoint)
        .await
        .squash_result()
        .context("Add ethernet interface")?;
    Ok(if_id)
}

async fn configure_endpoint_address(
    cli: &fidl_fuchsia_net_stack::StackProxy,
    if_id: u64,
    addr: AddrSubnetEither,
) -> Result<(), Error> {
    // add address:
    let () = cli
        .add_interface_address_deprecated(if_id, &mut addr.into_fidl())
        .await
        .squash_result()
        .context("Add interface address")?;

    // add route to ensure `addr` is valid, the result can be safely discarded
    let _ =
        AddrSubnetEither::try_from(addr).expect("Invalid test subnet configuration").addr_subnet();

    let () = cli
        .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
            subnet: addr.addr_subnet().1.into_fidl(),
            device_id: if_id,
            next_hop: None,
            metric: 0,
        })
        .await
        .squash_result()
        .context("Add forwarding entry")?;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_add_remove_interface() {
    let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
    let ep = t.get_endpoint("test-ep1").await.unwrap();
    let test_stack = t.get(0);
    let stack = test_stack.connect_stack().unwrap();
    let if_id = stack
        .add_ethernet_interface("fake_topo_path", ep)
        .await
        .squash_result()
        .expect("Add interface succeeds");

    // remove the interface:
    let () = stack.del_ethernet_interface(if_id).await.squash_result().expect("Remove interface");
    // ensure the interface disappeared from records:
    assert_matches!(test_stack.ctx.lock().await.non_sync_ctx.get_device_info(if_id), None);

    // if we try to remove it again, NotFound should be returned:
    let res =
        stack.del_ethernet_interface(if_id).await.unwrap().expect_err("Failed to remove twice");
    assert_eq!(res, fidl_net_stack::Error::NotFound);
}

#[fasync::run_singlethreaded(test)]
async fn test_ethernet_link_up_down() {
    let mut t = TestSetupBuilder::new()
        .add_endpoint()
        .add_stack(StackSetupBuilder::new().add_endpoint(1, None))
        .build()
        .await
        .unwrap();
    let ep_name = test_ep_name(1);
    let test_stack = t.get(0);
    let if_id = test_stack.get_endpoint_id(1);

    let () = test_stack.wait_for_interface_online(if_id).await;

    // Get the interface info to confirm status indicators are correct.
    let if_info = test_stack.get_interface_info(if_id).await;
    assert!(if_info.phy_up);
    assert!(if_info.admin_enabled);

    // Ensure that the device has been enabled in the core.
    let core_id = {
        let mut ctx = test_stack.ctx().await;
        let core_id = ctx.non_sync_ctx.get_device_info(if_id).unwrap().core_id();
        check_ip_enabled(ctx.deref_mut(), core_id, true);
        core_id
    };

    // Setting the link down should disable the interface and disable it from
    // the core. The AdministrativeStatus should remain unchanged.
    assert!(t.set_endpoint_link_up(&ep_name, false).await.is_ok());
    let test_stack = t.get(0);
    test_stack.wait_for_interface_offline(if_id).await;

    // Get the interface info to confirm that it is disabled.
    let if_info = test_stack.get_interface_info(if_id).await;
    assert!(!if_info.phy_up);
    assert!(if_info.admin_enabled);

    // Ensure that the device has been disabled in the core.
    check_ip_enabled(test_stack.ctx().await.deref_mut(), core_id, false);

    // Setting the link down again should cause no effect on the device state,
    // and should be handled gracefully.
    assert!(t.set_endpoint_link_up(&ep_name, false).await.is_ok());

    // Get the interface info to confirm that it is disabled.
    let test_stack = t.get(0);
    let if_info = test_stack.get_interface_info(if_id).await;
    assert!(!if_info.phy_up);
    assert!(if_info.admin_enabled);

    // Ensure that the device has been disabled in the core.
    check_ip_enabled(test_stack.ctx().await.deref_mut(), core_id, false);

    // Setting the link up should reenable the interface and enable it in
    // the core.
    assert!(t.set_endpoint_link_up(&ep_name, true).await.is_ok());
    t.get(0).wait_for_interface_online(if_id).await;

    // Get the interface info to confirm that it is reenabled.
    let test_stack = t.get(0);
    let if_info = test_stack.get_interface_info(if_id).await;
    assert!(if_info.phy_up);
    assert!(if_info.admin_enabled);

    // Ensure that the device has been enabled in the core.
    check_ip_enabled(test_stack.ctx().await.deref_mut(), core_id, true);

    // Setting the link up again should cause no effect on the device state,
    // and should be handled gracefully.
    assert!(t.set_endpoint_link_up(&ep_name, true).await.is_ok());

    // Get the interface info to confirm that there have been no changes.
    let test_stack = t.get(0);
    let if_info = test_stack.get_interface_info(if_id).await;
    assert!(if_info.phy_up);
    assert!(if_info.admin_enabled);

    // Ensure that the device has been enabled in the core.
    let core_id = t
        .get(0)
        .with_ctx(|ctx| {
            let core_id = ctx.non_sync_ctx.get_device_info(if_id).unwrap().core_id();
            check_ip_enabled(ctx, core_id, true);
            core_id
        })
        .await;

    // call directly into core to prove that the device was correctly
    // initialized (core will panic if we try to use the device and initialize
    // hasn't been called)
    let mut ctx = t.ctx(0).await;
    let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
    netstack3_core::device::receive_frame(sync_ctx, non_sync_ctx, core_id, Buf::new(&mut [], ..))
        .expect("error receiving frame");
}

fn check_ip_enabled<NonSyncCtx: NonSyncContext>(
    Ctx { sync_ctx, non_sync_ctx: _ }: &mut Ctx<NonSyncCtx>,
    core_id: DeviceId,
    expected: bool,
) {
    let ipv4_enabled =
        netstack3_core::device::get_ipv4_configuration(sync_ctx, core_id).ip_config.ip_enabled;
    let ipv6_enabled =
        netstack3_core::device::get_ipv6_configuration(sync_ctx, core_id).ip_config.ip_enabled;

    assert_eq!((ipv4_enabled, ipv6_enabled), (expected, expected));
}

#[fasync::run_singlethreaded(test)]
async fn test_add_del_interface_address_deprecated() {
    let mut t = TestSetupBuilder::new()
        .add_endpoint()
        .add_stack(StackSetupBuilder::new().add_endpoint(1, None))
        .build()
        .await
        .unwrap();
    let test_stack = t.get(0);
    let stack = test_stack.connect_stack().unwrap();
    let if_id = test_stack.get_endpoint_id(1);
    for addr in [
        new_ipv4_addr_subnet([192, 168, 0, 1], 24).into_fidl(),
        new_ipv6_addr_subnet([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15], 64)
            .into_fidl(),
    ]
    .iter_mut()
    {
        // The first IP address added should succeed.
        let () = stack
            .add_interface_address_deprecated(if_id, addr)
            .await
            .squash_result()
            .expect("Add interface address should succeed");
        let if_info = test_stack.get_interface_info(if_id).await;
        assert!(if_info.addresses.contains(&addr));

        // Adding the same IP address again should fail with already exists.
        let err = stack
            .add_interface_address_deprecated(if_id, addr)
            .await
            .expect("Add interface address FIDL call should succeed")
            .expect_err("Adding same address should fail");
        assert_eq!(err, fidl_net_stack::Error::AlreadyExists);

        // Deleting an IP address that exists should succeed.
        let () = stack
            .del_interface_address_deprecated(if_id, addr)
            .await
            .squash_result()
            .expect("Delete interface address succeeds");
        let if_info = test_stack.get_interface_info(if_id).await;
        assert!(!if_info.addresses.contains(&addr));

        // Deleting an IP address that doesn't exist should fail with not found.
        let err = stack
            .del_interface_address_deprecated(if_id, addr)
            .await
            .expect("Delete interface address FIDL call should succeed")
            .expect_err("Deleting non-existent address should fail");
        assert_eq!(err, fidl_net_stack::Error::NotFound);
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_add_device_routes() {
    // create a stack and add a single endpoint to it so we have the interface
    // id:
    let mut t = TestSetupBuilder::new()
        .add_endpoint()
        .add_stack(StackSetupBuilder::new().add_endpoint(1, None))
        .build()
        .await
        .unwrap();
    let test_stack = t.get(0);
    let stack = test_stack.connect_stack().unwrap();
    let if_id = test_stack.get_endpoint_id(1);

    let mut fwd_entry1 = fidl_net_stack::ForwardingEntry {
        subnet: fidl_net::Subnet {
            addr: fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: [192, 168, 0, 0] }),
            prefix_len: 24,
        },
        device_id: if_id,
        next_hop: None,
        metric: 0,
    };
    let mut fwd_entry2 = fidl_net_stack::ForwardingEntry {
        subnet: fidl_net::Subnet {
            addr: fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: [10, 0, 0, 0] }),
            prefix_len: 24,
        },
        device_id: if_id,
        next_hop: None,
        metric: 0,
    };

    let () = stack
        .add_forwarding_entry(&mut fwd_entry1)
        .await
        .squash_result()
        .expect("Add forwarding entry succeeds");
    let () = stack
        .add_forwarding_entry(&mut fwd_entry2)
        .await
        .squash_result()
        .expect("Add forwarding entry succeeds");

    // finally, check that bad routes will fail:
    // a duplicate entry should fail with AlreadyExists:
    let mut bad_entry = fidl_net_stack::ForwardingEntry {
        subnet: fidl_net::Subnet {
            addr: fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: [192, 168, 0, 0] }),
            prefix_len: 24,
        },
        device_id: if_id,
        next_hop: None,
        metric: 0,
    };
    assert_eq!(
        stack.add_forwarding_entry(&mut bad_entry).await.unwrap().unwrap_err(),
        fidl_net_stack::Error::AlreadyExists
    );
    // an entry with an invalid subnet should fail with Invalidargs:
    let mut bad_entry = fidl_net_stack::ForwardingEntry {
        subnet: fidl_net::Subnet {
            addr: fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: [10, 0, 0, 0] }),
            prefix_len: 64,
        },
        device_id: if_id,
        next_hop: None,
        metric: 0,
    };
    assert_eq!(
        stack.add_forwarding_entry(&mut bad_entry).await.unwrap().unwrap_err(),
        fidl_net_stack::Error::InvalidArgs
    );
    // an entry with a bad devidce id should fail with NotFound:
    let mut bad_entry = fidl_net_stack::ForwardingEntry {
        subnet: fidl_net::Subnet {
            addr: fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: [10, 0, 0, 0] }),
            prefix_len: 24,
        },
        device_id: 10,
        next_hop: None,
        metric: 0,
    };
    assert_eq!(
        stack.add_forwarding_entry(&mut bad_entry).await.unwrap().unwrap_err(),
        fidl_net_stack::Error::NotFound
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_list_del_routes() {
    // create a stack and add a single endpoint to it so we have the interface
    // id:
    const EP_NAME: &str = "testep";
    let mut t = TestSetupBuilder::new()
        .add_named_endpoint(EP_NAME)
        .add_stack(StackSetupBuilder::new().add_named_endpoint(EP_NAME, None))
        .build()
        .await
        .unwrap();

    let test_stack = t.get(0);
    let stack = test_stack.connect_stack().unwrap();
    let if_id = test_stack.get_named_endpoint_id(EP_NAME);
    let loopback_id = test_stack.get_named_endpoint_id(LOOPBACK_NAME);
    assert_ne!(loopback_id, if_id);
    let device = test_stack.ctx().await.non_sync_ctx.get_core_id(if_id).expect("device exists");
    let loopback =
        test_stack.ctx().await.non_sync_ctx.get_core_id(loopback_id).expect("device exists");
    let route1_subnet_bytes = [192, 168, 0, 0];
    let route1_subnet_prefix = 24;
    let route1: AddableEntryEither<_> = AddableEntry::without_gateway(
        Subnet::<Ipv4Addr>::new(Ipv4Addr::from(route1_subnet_bytes).into(), route1_subnet_prefix)
            .unwrap(),
        device,
    )
    .into();
    let sub10 = Subnet::<Ipv4Addr>::new(Ipv4Addr::from([10, 0, 0, 0]).into(), 24).unwrap();
    let route2 = AddableEntry::without_gateway(sub10, device).into();
    let sub10_gateway = SpecifiedAddr::new(Ipv4Addr::from([10, 0, 0, 1])).unwrap().into();
    let route3 = AddableEntry::with_gateway(sub10, None, sub10_gateway).into();

    let () = test_stack
        .with_ctx(|Ctx { sync_ctx, non_sync_ctx }| {
            // add a couple of routes directly into core:
            netstack3_core::add_route(sync_ctx, non_sync_ctx, route1).unwrap();
            netstack3_core::add_route(sync_ctx, non_sync_ctx, route2).unwrap();
            netstack3_core::add_route(sync_ctx, non_sync_ctx, route3).unwrap();
        })
        .await;

    let routes = stack.get_forwarding_table().await.expect("Can get forwarding table");
    let route3_with_device = AddableEntry::with_gateway(sub10, Some(device), sub10_gateway).into();

    let auto_routes = [
        // Link local route is added automatically by core.
        AddableEntry::without_gateway(net_declare::net_subnet_v6!("fe80::/64").into(), device)
            .into(),
        // Loopback routes are added on netstack creation.
        AddableEntry::without_gateway(Ipv4::LOOPBACK_SUBNET, loopback).into(),
        AddableEntry::without_gateway(Ipv6::LOOPBACK_SUBNET, loopback).into(),
        // Multicast routes are added automatically by core.
        AddableEntry::without_gateway(Ipv4::MULTICAST_SUBNET, loopback).into(),
        AddableEntry::without_gateway(Ipv6::MULTICAST_SUBNET, loopback).into(),
        AddableEntry::without_gateway(Ipv4::MULTICAST_SUBNET, device).into(),
        AddableEntry::without_gateway(Ipv6::MULTICAST_SUBNET, device).into(),
    ];
    let got = test_stack
        .with_ctx(|ctx| {
            routes
                .into_iter()
                .map(|e| AddableEntryEither::try_from_fidl_with_ctx(&ctx.non_sync_ctx, e).unwrap())
                .collect::<HashSet<_>>()
        })
        .await;
    let want =
        HashSet::from_iter([route1, route2, route3_with_device].into_iter().chain(auto_routes));
    assert_eq!(got, want, "got - want = {:?}", got.symmetric_difference(&want));

    // delete route1:
    let mut fwd_entry = fidl_net_stack::ForwardingEntry {
        subnet: fidl_net::Subnet {
            addr: fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: route1_subnet_bytes }),
            prefix_len: route1_subnet_prefix,
        },
        device_id: 0,
        next_hop: None,
        metric: 0,
    };
    let () = stack
        .del_forwarding_entry(&mut fwd_entry)
        .await
        .squash_result()
        .expect("can delete device forwarding entry");
    // can't delete again:
    assert_eq!(
        stack.del_forwarding_entry(&mut fwd_entry).await.unwrap().unwrap_err(),
        fidl_net_stack::Error::NotFound
    );

    // check that route was deleted (should've disappeared from core)
    let routes = stack.get_forwarding_table().await.expect("Can get forwarding table");
    assert_eq!(
        test_stack
            .with_ctx(|ctx| {
                routes
                    .into_iter()
                    .map(|e| {
                        AddableEntryEither::try_from_fidl_with_ctx(&ctx.non_sync_ctx, e).unwrap()
                    })
                    .collect::<HashSet<_>>()
            })
            .await,
        HashSet::from_iter([route2, route3_with_device].into_iter().chain(auto_routes))
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_add_remote_routes() {
    let mut t = TestSetupBuilder::new()
        .add_endpoint()
        .add_stack(StackSetupBuilder::new().add_endpoint(1, None))
        .build()
        .await
        .unwrap();

    let test_stack = t.get(0);
    let stack = test_stack.connect_stack().unwrap();
    let device_id = test_stack.get_endpoint_id(1);

    let subnet = fidl_net::Subnet {
        addr: fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: [192, 168, 0, 0] }),
        prefix_len: 24,
    };
    let mut fwd_entry = fidl_net_stack::ForwardingEntry {
        subnet,
        device_id: 0,
        next_hop: Some(Box::new(fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address {
            addr: [192, 168, 0, 1],
        }))),
        metric: 0,
    };

    // Cannot add gateway route without device set or on-link route to gateway.
    assert_eq!(
        stack.add_forwarding_entry(&mut fwd_entry).await.unwrap(),
        Err(fidl_net_stack::Error::BadState)
    );
    let mut device_fwd_entry = fidl_net_stack::ForwardingEntry {
        subnet: fwd_entry.subnet,
        device_id,
        next_hop: None,
        metric: 0,
    };
    let () = stack
        .add_forwarding_entry(&mut device_fwd_entry)
        .await
        .squash_result()
        .expect("add device route");

    let () =
        stack.add_forwarding_entry(&mut fwd_entry).await.squash_result().expect("add device route");

    // finally, check that bad routes will fail:
    // a duplicate entry should fail with AlreadyExists:
    assert_eq!(
        stack.add_forwarding_entry(&mut fwd_entry).await.unwrap(),
        Err(fidl_net_stack::Error::AlreadyExists)
    );
}
