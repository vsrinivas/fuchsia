// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::convert::TryFrom;
use std::num::NonZeroU16;
use std::ops::DerefMut;
use std::sync::{Arc, Once};

use anyhow::{format_err, Context as _, Error};
use fidl::encoding::Decodable;
use fidl_fuchsia_net as fidl_net;
use fidl_fuchsia_net_icmp as fidl_icmp;
use fidl_fuchsia_net_stack::{self as fidl_net_stack, AdministrativeStatus, PhysicalStatus};
use fidl_fuchsia_net_stack_ext::FidlReturn;
use fidl_fuchsia_netemul_network as net;
use fidl_fuchsia_netemul_sandbox as sandbox;
use fuchsia_async as fasync;
use fuchsia_component::client;
use futures::{lock::Mutex, Future, StreamExt};
use log::debug;
use net_types::{
    ethernet::Mac,
    ip::{AddrSubnetEither, IpAddr, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr, SubnetEither},
    SpecifiedAddr, Witness,
};
use netstack3_core::{
    error::NoRouteError,
    icmp::{
        self as core_icmp, BufferIcmpEventDispatcher, IcmpConnId, IcmpEventDispatcher, IcmpIpExt,
    },
    Context, DeviceId, DeviceLayerEventDispatcher, EntryDest, EntryEither, EventDispatcher,
    IpLayerEventDispatcher, StackStateBuilder, TimerId, TransportLayerEventDispatcher,
};
use packet::{Buf, BufferMut, Serializer};

use crate::bindings::{
    context::{FromOuterValue, GuardContext, InnerValue, LockableContext},
    devices::DeviceInfo,
    icmp::InnerIcmpConnId,
    util::{ConversionContext, IntoFidl, TryFromFidlWithContext, TryIntoFidl},
    BindingsDispatcher, LockedStackContext, StackContext, StackDispatcher,
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
pub(crate) struct TestDispatcher {
    disp: BindingsDispatcher,
    /// A oneshot signal that is hit whenever changes to interface status occur
    /// and it is set.
    status_changed_signal: Option<futures::channel::oneshot::Sender<()>>,
    /// An optional interceptor for all ICMP responses.
    /// If it is set, ICMP responses will be directed to the provided channel
    /// instead of flowing through the ICMP sockets module.
    pub(crate) icmp_responses:
        Option<futures::channel::mpsc::UnboundedSender<(InnerIcmpConnId, u16, Vec<u8>)>>,
}

impl TestDispatcher {
    fn new() -> Self {
        Self { disp: BindingsDispatcher::new(), status_changed_signal: None, icmp_responses: None }
    }
}

impl StackDispatcher for TestDispatcher {
    fn device_status_changed(&mut self, id: u64) {
        if let Some(s) = self.status_changed_signal.take() {
            s.send(()).unwrap();
        }
        // we can always send that forward to the real dispatcher, no need to
        // short-circuit it.
        self.disp.device_status_changed(id);
    }
}

impl<T> InnerValue<T> for TestDispatcher
where
    T: FromOuterValue<BindingsDispatcher>,
{
    fn inner(&self) -> &T {
        T::from_outer(&self.disp)
    }

    fn inner_mut(&mut self) -> &mut T {
        T::from_outer_mut(&mut self.disp)
    }
}

impl<I: crate::bindings::socket::udp::UdpSocketIpExt> netstack3_core::UdpEventDispatcher<I>
    for TestDispatcher
{
    fn receive_udp_from_conn(
        &mut self,
        conn: netstack3_core::UdpConnId<I>,
        src_ip: I::Addr,
        src_port: NonZeroU16,
        body: &[u8],
    ) {
        self.disp.receive_udp_from_conn(conn, src_ip, src_port, body)
    }

    /// Receive a UDP packet for a listener.
    fn receive_udp_from_listen(
        &mut self,
        listener: netstack3_core::UdpListenerId<I>,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        src_port: Option<NonZeroU16>,
        body: &[u8],
    ) {
        self.disp.receive_udp_from_listen(listener, src_ip, dst_ip, src_port, body)
    }
}

impl EventDispatcher for TestDispatcher {
    type Instant = <BindingsDispatcher as EventDispatcher>::Instant;
    fn now(&self) -> Self::Instant {
        self.disp.now()
    }

    fn schedule_timeout_instant(
        &mut self,
        time: Self::Instant,
        id: TimerId,
    ) -> Option<Self::Instant> {
        self.disp.schedule_timeout_instant(time, id)
    }

    fn cancel_timeout(&mut self, id: TimerId) -> Option<Self::Instant> {
        self.disp.cancel_timeout(id)
    }

    fn cancel_timeouts_with<F: FnMut(&TimerId) -> bool>(&mut self, f: F) {
        self.disp.cancel_timeouts_with(f)
    }

    fn scheduled_instant(&self, id: TimerId) -> Option<Self::Instant> {
        self.disp.scheduled_instant(id)
    }

    type Rng = <BindingsDispatcher as EventDispatcher>::Rng;

    fn rng(&self) -> &Self::Rng {
        self.disp.rng()
    }

    fn rng_mut(&mut self) -> &mut Self::Rng {
        self.disp.rng_mut()
    }
}

impl<B: BufferMut> DeviceLayerEventDispatcher<B> for TestDispatcher {
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: DeviceId,
        frame: S,
    ) -> Result<(), S> {
        self.disp.send_frame(device, frame)
    }
}

impl TransportLayerEventDispatcher<Ipv4> for TestDispatcher {}
impl TransportLayerEventDispatcher<Ipv6> for TestDispatcher {}

impl<I: IcmpIpExt> IcmpEventDispatcher<I> for TestDispatcher {
    fn receive_icmp_error(&mut self, conn: IcmpConnId<I>, seq_num: u16, err: I::ErrorCode) {
        IcmpEventDispatcher::<I>::receive_icmp_error(&mut self.disp, conn, seq_num, err)
    }

    fn close_icmp_connection(&mut self, conn: IcmpConnId<I>, err: NoRouteError) {
        self.disp.close_icmp_connection(conn, err)
    }
}

impl<I, B> BufferIcmpEventDispatcher<I, B> for TestDispatcher
where
    I: crate::bindings::icmp::IpExt,
    B: BufferMut,
{
    fn receive_icmp_echo_reply(&mut self, conn: IcmpConnId<I>, seq_num: u16, data: B) {
        if let Some(ch) = self.icmp_responses.as_mut() {
            let () = ch
                .unbounded_send((conn.into(), seq_num, data.as_ref().to_owned()))
                .expect("Failed to send on icmp_responses channel");
        } else {
            self.disp.receive_icmp_echo_reply(conn, seq_num, data)
        }
    }
}

impl<B: BufferMut> IpLayerEventDispatcher<B> for TestDispatcher {}

/// A netstack context for testing.
/// `TestContext` replaces [`crate::bindings::Netstack`] for testing. It
/// implements [`StackContext`] appropriately, using [`TestDispatcher`] as its
/// Event Dispatcher to allow for special inspection for certain tests.
pub(crate) struct TestContext {
    ctx: Arc<Mutex<Context<TestDispatcher>>>,
}

impl TestContext {
    fn new(builder: StackStateBuilder) -> Self {
        Self { ctx: Arc::new(Mutex::new(Context::new(builder.build(), TestDispatcher::new()))) }
    }
}

impl Clone for TestContext {
    fn clone(&self) -> Self {
        Self { ctx: Arc::clone(&self.ctx) }
    }
}

impl GuardContext<Context<TestDispatcher>> for TestContext {
    type Guard = Context<TestDispatcher>;
}

impl AsRef<Mutex<Context<TestDispatcher>>> for TestContext {
    fn as_ref(&self) -> &Mutex<Context<TestDispatcher>> {
        self.ctx.as_ref()
    }
}

impl StackContext for TestContext {
    type Dispatcher = TestDispatcher;
}

/// A holder for a [`TestContext`].
/// `TestStack` is obtained from [`TestSetupBuilder`] and offers utility methods
/// to connect to the FIDL APIs served by [`TestContext`], as well as keeps
/// track of configured interfaces during the setup procedure.
pub(crate) struct TestStack {
    ctx: TestContext,
    endpoint_ids: HashMap<String, u64>,
}

impl TestStack {
    /// Connects to the `fuchsia.net.stack.Stack` service.
    pub(crate) fn connect_stack(&self) -> Result<fidl_fuchsia_net_stack::StackProxy, Error> {
        let (stack, rs) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_stack::StackMarker>()?;
        crate::bindings::stack_fidl_worker::StackFidlWorker::spawn(self.ctx.clone(), rs);
        Ok(stack)
    }

    /// Connects to the `fuchsia.posix.socket.Provider` service.
    pub(crate) fn connect_socket_provider(
        &self,
    ) -> Result<fidl_fuchsia_posix_socket::ProviderProxy, Error> {
        let (stack, rs) = fidl::endpoints::create_proxy_and_stream::<
            fidl_fuchsia_posix_socket::ProviderMarker,
        >()?;
        crate::bindings::socket::SocketProviderWorker::spawn(self.ctx.clone(), rs);
        Ok(stack)
    }

    /// Connects to the `fuchsia.net.icmp.Provider` service.
    pub(crate) fn connect_icmp_provider(&self) -> Result<fidl_icmp::ProviderProxy, Error> {
        let (provider, rs) =
            fidl::endpoints::create_proxy_and_stream::<fidl_icmp::ProviderMarker>()?;
        crate::bindings::icmp::IcmpProviderWorker::spawn(self.ctx.clone(), rs);
        Ok(provider)
    }

    /// Waits for interface with given `if_id` to come online.
    pub(crate) async fn wait_for_interface_online(&mut self, if_id: u64) {
        let check_online = |status: &DeviceInfo| status.phy_up();
        self.wait_for_interface_status(if_id, check_online).await;
    }

    /// Waits for interface with given `if_id` to go offline.
    pub(crate) async fn wait_for_interface_offline(&mut self, if_id: u64) {
        let check_offline = |status: &DeviceInfo| !status.phy_up();
        self.wait_for_interface_status(if_id, check_offline).await;
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
                    ctx.dispatcher()
                        .get_device_info(if_id)
                        .expect("Wait for interface status on unknown device"),
                ) {
                    return;
                }
                let (sender, receiver) = futures::channel::oneshot::channel();
                ctx.dispatcher_mut().status_changed_signal = Some(sender);
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
        // Create a new TestStack with Duplicate Address Detection disabled for tests.
        //
        // TODO(fxbug.dev/36238): Remove this code when an event is dispatched when Duplicate Address
        // Detection finishes or when an IPv6 address has been assigned. Without such events, tests
        // do not know how long to wait for the stack to be ready for events.
        use netstack3_core::NdpConfigurations;
        let mut builder = StackStateBuilder::default();
        let mut config = NdpConfigurations::default();
        config.set_dup_addr_detect_transmits(None);
        config.set_max_router_solicitations(None);
        builder.device_builder().set_default_ndp_configs(config);
        let ctx = TestContext::new(builder);
        TestStack { ctx, endpoint_ids: HashMap::new() }
    }

    /// Helper function to invoke a closure that provides a locked
    /// [`Context<TestDispatcher>`] provided by this `TestStack`.
    pub(crate) async fn with_ctx<R, F: FnOnce(&mut Context<TestDispatcher>) -> R>(
        &mut self,
        f: F,
    ) -> R {
        let mut ctx = self.ctx.lock().await;
        f(ctx.deref_mut())
    }

    /// Acquire a lock on this `TestStack`'s context.
    pub(crate) fn ctx(&self) -> impl Future<Output = LockedStackContext<'_, TestContext>> {
        self.ctx.lock()
    }
}

/// A test setup that than contain multiple stack instances networked together.
pub(crate) struct TestSetup {
    // Let connection to sandbox be made lazily, so a netemul sandbox is not
    // created for tests that don't need it.
    sandbox: Option<sandbox::SandboxProxy>,
    nets: Option<fidl::endpoints::ClientEnd<net::SetupHandleMarker>>,
    stacks: Vec<TestStack>,
}

impl TestSetup {
    /// Gets the [`TestStack`] at index `i`.
    pub(crate) fn get(&mut self, i: usize) -> &mut TestStack {
        &mut self.stacks[i]
    }

    /// Clones the [`TestContext`] at index `i`.
    pub(crate) fn clone_ctx(&self, i: usize) -> TestContext {
        self.stacks[i].ctx.clone()
    }

    /// Acquires a lock on the [`TestContext`] at index `i`.
    pub(crate) async fn ctx(&mut self, i: usize) -> LockedStackContext<'_, TestContext> {
        self.get(i).ctx.lock().await
    }

    async fn get_endpoint<'a>(
        &'a mut self,
        ep_name: &'a str,
    ) -> Result<fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>, Error>
    {
        let (net_ctx, net_ctx_server) =
            fidl::endpoints::create_proxy::<net::NetworkContextMarker>()?;
        self.sandbox().get_network_context(net_ctx_server)?;
        let (epm, epm_server) = fidl::endpoints::create_proxy::<net::EndpointManagerMarker>()?;
        net_ctx.get_endpoint_manager(epm_server)?;
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
        let (net_ctx, net_ctx_server) =
            fidl::endpoints::create_proxy::<net::NetworkContextMarker>()?;
        self.sandbox().get_network_context(net_ctx_server)?;
        let (epm, epm_server) = fidl::endpoints::create_proxy::<net::EndpointManagerMarker>()?;
        net_ctx.get_endpoint_manager(epm_server)?;
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
        Ok(Self { sandbox: None, nets: None, stacks: Vec::new() })
    }

    fn sandbox(&mut self) -> &sandbox::SandboxProxy {
        self.sandbox.get_or_insert_with(|| {
            client::connect_to_service::<sandbox::SandboxMarker>()
                .expect("Failed to connect to sandbox service")
        })
    }

    fn get_network_context(&mut self) -> Result<net::NetworkContextProxy, Error> {
        let (net_ctx, net_ctx_server) =
            fidl::endpoints::create_proxy::<net::NetworkContextMarker>()?;
        self.sandbox().get_network_context(net_ctx_server)?;
        Ok(net_ctx)
    }

    async fn configure_network(
        &mut self,
        ep_names: impl Iterator<Item = String>,
    ) -> Result<(), Error> {
        let net_ctx = self.get_network_context()?;
        let (status, handle) = net_ctx
            .setup(
                &mut vec![&mut net::NetworkSetup {
                    name: "test_net".to_owned(),
                    config: net::NetworkConfig::new_empty(),
                    endpoints: ep_names.map(|name| new_endpoint_setup(name)).collect(),
                }]
                .into_iter(),
            )
            .await?;

        self.nets = Some(handle.ok_or_else(|| format_err!("Create network failed: {}", status))?);
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

                stack.endpoint_ids.insert(ep_name, if_id);
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
/// May panic if `prefix` is longer than the number of bits in this type of IP address (32 for
/// IPv4), or if `ip` is not a unicast address in the resulting subnet (see
/// [`net_types::ip::IpAddress::is_unicast_in_subnet`]).
pub fn new_ipv4_addr_subnet(ip: [u8; 4], prefix: u8) -> AddrSubnetEither {
    AddrSubnetEither::new(IpAddr::V4(Ipv4Addr::from(ip)), prefix).unwrap()
}

/// Shorthand function to create an IPv6 [`AddrSubnetEither`].
///
/// # Panics
///
/// May panic if `prefix` is longer than the number of bits in this type of IP address (128 for
/// IPv6), or if `ip` is not a unicast address in the resulting subnet (see
/// [`net_types::ip::IpAddress::is_unicast_in_subnet`]).
pub fn new_ipv6_addr_subnet(ip: [u8; 16], prefix: u8) -> AddrSubnetEither {
    AddrSubnetEither::new(IpAddr::V6(Ipv6Addr::from(ip)), prefix).unwrap()
}

/// Helper struct to create stack configurations for [`TestSetupBuilder`].
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
        .add_interface_address(if_id, &mut addr.into_fidl())
        .await
        .squash_result()
        .context("Add interface address")?;

    // add route to ensure `addr` is valid, the result can be safely discarded
    let _ = AddrSubnetEither::try_from(addr)
        .expect("Invalid test subnet configuration")
        .into_addr_subnet();

    let () = cli
        .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
            subnet: addr.into_addr_subnet().1.into_fidl(),
            destination: fidl_fuchsia_net_stack::ForwardingDestination::DeviceId(if_id),
        })
        .await
        .squash_result()
        .context("Add forwarding entry")?;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_ping() {
    const ALICE_IP: [u8; 4] = [192, 168, 0, 1];
    const BOB_IP: [u8; 4] = [192, 168, 0, 2];
    // simple test to ping between two stacks:
    let mut t = TestSetupBuilder::new()
        .add_named_endpoint("bob")
        .add_named_endpoint("alice")
        .add_stack(
            StackSetupBuilder::new()
                .add_named_endpoint("alice", Some(new_ipv4_addr_subnet(ALICE_IP, 24))),
        )
        .add_stack(
            StackSetupBuilder::new()
                .add_named_endpoint("bob", Some(new_ipv4_addr_subnet(BOB_IP, 24))),
        )
        .build()
        .await
        .expect("Test Setup succeeds");

    const ICMP_ID: u16 = 1;

    debug!("creating icmp connection");
    // create icmp connection on alice:
    let conn_id = core_icmp::new_icmpv4_connection(
        t.ctx(0).await.deref_mut(),
        Some(SpecifiedAddr::new(ALICE_IP.into()).unwrap()),
        SpecifiedAddr::new(BOB_IP.into()).unwrap(),
        ICMP_ID,
    )
    .unwrap();

    let ping_bod = [1, 2, 3, 4, 5, 6];

    let (sender, mut recv) = futures::channel::mpsc::unbounded();

    t.ctx(0).await.dispatcher_mut().icmp_responses = Some(sender);

    // alice will ping bob 4 times:
    for seq in 1..=4 {
        debug!("sending ping seq {}", seq);
        // send ping request:
        core_icmp::send_icmpv4_echo_request(
            t.ctx(0).await.deref_mut(),
            conn_id,
            seq,
            Buf::new(ping_bod.to_vec(), ..),
        )
        .unwrap();

        // wait until the response comes along:
        let (rsp_id, rsp_seq, rsp_bod) = recv.next().await.unwrap();
        debug!("Received ping seq {}", rsp_seq);
        // validate seq and body:
        assert_eq!(rsp_id, conn_id.into());
        assert_eq!(rsp_seq, seq);
        assert_eq!(&rsp_bod, &ping_bod);
    }
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
    // check that the created ID matches the one saved in the event loop state:

    assert_eq!(
        test_stack
            .ctx
            .lock()
            .await
            .dispatcher()
            .get_device_info(if_id)
            .expect("Get device client")
            .path(),
        "fake_topo_path"
    );

    // remove the interface:
    let () = stack.del_ethernet_interface(if_id).await.squash_result().expect("Remove interface");
    // ensure the interface disappeared from records:
    assert!(test_stack.ctx.lock().await.dispatcher().get_device_info(if_id).is_none());

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
    let stack = test_stack.connect_stack().unwrap();
    let if_id = test_stack.get_endpoint_id(1);

    let () = t.get(0).wait_for_interface_online(if_id).await;

    // Get the interface info to confirm status indicators are correct.
    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);

    // Ensure that the device has been added to the core.
    assert!(t.ctx(0).await.dispatcher().get_device_info(if_id).unwrap().is_active());

    // Setting the link down should disable the interface and remove it from
    // the core. The AdministrativeStatus should remain unchanged.
    assert!(t.set_endpoint_link_up(&ep_name, false).await.is_ok());
    t.get(0).wait_for_interface_offline(if_id).await;

    // Get the interface info to confirm that it is disabled.
    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Down);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);

    // Ensure that the device has been removed from the core.
    assert!(t.ctx(0).await.dispatcher().get_device_info(if_id).unwrap().is_active() == false);

    // Setting the link down again should cause no effect on the device state,
    // and should be handled gracefully.
    assert!(t.set_endpoint_link_up(&ep_name, false).await.is_ok());

    // Get the interface info to confirm that it is disabled.
    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Down);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);

    // Ensure that the device has been removed from the core.
    assert!(t.ctx(0).await.dispatcher().get_device_info(if_id).unwrap().is_active() == false);

    // Setting the link up should reenable the interface and add it to
    // the core.
    assert!(t.set_endpoint_link_up(&ep_name, true).await.is_ok());
    t.get(0).wait_for_interface_online(if_id).await;

    // Get the interface info to confirm that it is reenabled.
    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);

    // Ensure that the device has been added to the core.
    assert!(t.ctx(0).await.dispatcher().get_device_info(if_id).unwrap().is_active());

    // Setting the link up again should cause no effect on the device state,
    // and should be handled gracefully.
    assert!(t.set_endpoint_link_up(&ep_name, true).await.is_ok());

    // Get the interface info to confirm that there have been no changes.
    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);

    // Ensure that the device has been added to the core.
    let core_id = t
        .get(0)
        .with_ctx(|ctx| {
            let device_info = ctx.dispatcher().get_device_info(if_id).unwrap();
            assert!(device_info.is_active());
            device_info.core_id().unwrap()
        })
        .await;

    // call directly into core to prove that the device was correctly
    // initialized (core will panic if we try to use the device and initialize
    // hasn't been called)
    netstack3_core::receive_frame(t.ctx(0).await.deref_mut(), core_id, Buf::new(&mut [], ..));
}

#[fasync::run_singlethreaded(test)]
async fn test_list_interfaces() {
    let mut t = TestSetupBuilder::new()
        .add_endpoint()
        .add_endpoint()
        .add_endpoint()
        .add_empty_stack()
        .build()
        .await
        .unwrap();

    let stack = t.get(0).connect_stack().unwrap();
    // check that we can list when no interfaces exist:
    // TODO(brunodalbo) this test may require tunning when we expose the
    //  loopback interface over FIDL
    let ifs = stack.list_interfaces().await.expect("List interfaces");
    assert!(ifs.is_empty());

    let mut if_props = HashMap::new();
    // collect created endpoint and add them to the stack:
    for i in 1..=3 {
        let ep_name = test_ep_name(i);
        let ep = t.get_endpoint(&ep_name).await.unwrap().into_proxy().unwrap();
        let ep_info = ep.get_info().await.unwrap();
        let mut if_ip = new_ipv4_addr_subnet([192, 168, 0, i as u8], 24).into_fidl();
        let if_ipv6_ll = Mac::new(ep_info.mac.octets).to_ipv6_link_local().into_addr().ipv6_bytes();
        let if_ipv6_ll = new_ipv6_addr_subnet(if_ipv6_ll, 64).into_fidl();

        let ep = t.get_endpoint(&ep_name).await.unwrap();
        let if_id = stack
            .add_ethernet_interface("fake_topo_path", ep)
            .await
            .squash_result()
            .expect("Add interface succeeds");
        // NOTE(brunodalbo) wait for interface to come online before trying to
        // add the address. We currently don't support adding addresses for
        // offline interfaces, because they're removed from core. This wait can
        // be removed once we allow that.
        t.get(0).wait_for_interface_online(if_id).await;
        let () = stack
            .add_interface_address(if_id, &mut if_ip)
            .await
            .squash_result()
            .expect("Add interface address succeeds");
        if_props.insert(if_id, (ep_info, vec![if_ip, if_ipv6_ll]));
    }

    let ifs = stack.list_interfaces().await.expect("List interfaces");
    assert_eq!(ifs.len(), 3);
    // check that what we served over FIDL is correct:
    for ifc in ifs.iter() {
        let (ep_info, if_ip) = if_props.remove(&ifc.id).unwrap();
        assert_eq!(&ifc.properties.topopath, "fake_topo_path");
        assert_eq!(ifc.properties.mac.as_ref().unwrap().as_ref(), &ep_info.mac);
        assert_eq!(ifc.properties.mtu, ep_info.mtu);
        assert_eq!(ifc.properties.features, ep_info.features);
        assert_eq!(ifc.properties.addresses, if_ip);
        assert_eq!(ifc.properties.administrative_status, AdministrativeStatus::Enabled);
        assert_eq!(ifc.properties.physical_status, PhysicalStatus::Up);
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_get_interface_info() {
    let ip = new_ipv4_addr_subnet([192, 168, 0, 1], 24);
    let mut t = TestSetupBuilder::new()
        .add_endpoint()
        .add_stack(StackSetupBuilder::new().add_endpoint(1, Some(ip.clone())))
        .build()
        .await
        .unwrap();
    let ep_name = test_ep_name(1);
    let ep = t.get_endpoint(&ep_name).await.unwrap();
    // get the device info from the ethernet driver:
    let ep_info = ep.into_proxy().unwrap().get_info().await.unwrap();
    let test_stack = t.get(0);
    let stack = test_stack.connect_stack().unwrap();
    let if_id = test_stack.get_endpoint_id(1);

    test_stack.wait_for_interface_online(1).await;

    let if_ipv6_ll = Mac::new(ep_info.mac.octets).to_ipv6_link_local().into_addr().ipv6_bytes();
    let if_ipv6_ll = new_ipv6_addr_subnet(if_ipv6_ll, 64).into_fidl();

    // get the interface info:
    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(&if_info.properties.topopath, "fake_topo_path");
    assert_eq!(if_info.properties.mac.as_ref().unwrap().as_ref(), &ep_info.mac);
    assert_eq!(if_info.properties.mtu, ep_info.mtu);
    assert_eq!(if_info.properties.features, ep_info.features);
    assert_eq!(if_info.properties.addresses, vec![ip.try_into_fidl().unwrap(), if_ipv6_ll]);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // check that we get the correct error for a non-existing interface id:
    let err = stack.get_interface_info(12345).await.unwrap().expect_err("Get interface info fails");
    assert_eq!(err, fidl_net_stack::Error::NotFound);
}

#[fasync::run_singlethreaded(test)]
async fn test_disable_enable_interface() {
    let mut t = TestSetupBuilder::new()
        .add_endpoint()
        .add_stack(StackSetupBuilder::new().add_endpoint(1, None))
        .build()
        .await
        .unwrap();
    let test_stack = t.get(0);
    let stack = test_stack.connect_stack().unwrap();
    let if_id = test_stack.get_endpoint_id(1);

    // Get the interface info to confirm that it is enabled.
    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // Disable the interface and test again, physical_status should be
    // unchanged.
    let () =
        stack.disable_interface(if_id).await.squash_result().expect("Disable interface succeeds");

    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Disabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // Ensure that the device has been removed from the core.
    assert!(
        test_stack.ctx().await.dispatcher().get_device_info(if_id).unwrap().is_active() == false
    );

    // Enable the interface and test again.
    let () =
        stack.enable_interface(if_id).await.squash_result().expect("Enable interface succeeds");

    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // Ensure that the device has been added to the core.
    assert!(test_stack.ctx().await.dispatcher().get_device_info(if_id).unwrap().is_active());

    // Check that we get the correct error for a non-existing interface id.
    assert_eq!(
        stack.enable_interface(12345).await.unwrap().unwrap_err(),
        fidl_net_stack::Error::NotFound
    );

    // Check that we get the correct error for a non-existing interface id.
    assert_eq!(
        stack.disable_interface(12345).await.unwrap().unwrap_err(),
        fidl_net_stack::Error::NotFound
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_phy_admin_interface_state_interaction() {
    let mut t = TestSetupBuilder::new()
        .add_endpoint()
        .add_stack(StackSetupBuilder::new().add_endpoint(1, None))
        .build()
        .await
        .unwrap();
    let ep_name = test_ep_name(1);
    let test_stack = t.get(0);
    let stack = test_stack.connect_stack().unwrap();
    let if_id = test_stack.get_endpoint_id(1);

    t.get(0).wait_for_interface_online(if_id).await;

    // Get the interface info to confirm that it is enabled.
    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // Disable the interface and test again, physical_status should be
    // unchanged.
    let () =
        stack.disable_interface(if_id).await.squash_result().expect("Disable interface succeeds");

    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Disabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // Ensure that the device has been removed from the core.
    assert!(t.ctx(0).await.dispatcher().get_device_info(if_id).unwrap().is_active() == false);

    // Setting the link down now that the interface is already down should only
    // change the cached state. Both phy and admin should be down now.
    assert!(t.set_endpoint_link_up(&ep_name, false).await.is_ok());
    t.get(0).wait_for_interface_offline(if_id).await;

    // Get the interface info to confirm that it is disabled.
    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Down);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Disabled);

    // Ensure that the device is still removed from the core.
    assert!(t.ctx(0).await.dispatcher().get_device_info(if_id).unwrap().is_active() == false);

    // Enable the interface and test again, only cached status should be changed
    // and core state should still be disabled.
    let () =
        stack.enable_interface(if_id).await.squash_result().expect("Enable interface succeeds");

    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Down);

    // Ensure that the device is still removed from the core.
    assert!(t.ctx(0).await.dispatcher().get_device_info(if_id).unwrap().is_active() == false);

    // Disable the interface and test again, both should be down now.
    let () =
        stack.disable_interface(if_id).await.squash_result().expect("Disable interface succeeds");

    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Disabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Down);

    // Ensure that the device is still removed from the core.
    assert!(t.ctx(0).await.dispatcher().get_device_info(if_id).unwrap().is_active() == false);

    // Setting the link up should only affect cached state
    assert!(t.set_endpoint_link_up(&ep_name, true).await.is_ok());
    t.get(0).wait_for_interface_online(if_id).await;

    // Get the interface info to confirm that it is reenabled.
    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Disabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // Ensure that the device is still removed from the core.
    assert!(t.ctx(0).await.dispatcher().get_device_info(if_id).unwrap().is_active() == false);

    // Finally, setting admin status up should update the cached state and
    // re-add the device to the core.
    let () =
        stack.enable_interface(if_id).await.squash_result().expect("Enable interface succeeds");

    // Get the interface info to confirm that it is reenabled.
    let if_info = stack.get_interface_info(if_id).await.unwrap().expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);

    // Ensure that the device has been added to the core.
    assert!(t.ctx(0).await.dispatcher().get_device_info(if_id).unwrap().is_active());
}

#[fasync::run_singlethreaded(test)]
async fn test_add_del_interface_address() {
    let mut t = TestSetupBuilder::new()
        .add_endpoint()
        .add_stack(StackSetupBuilder::new().add_endpoint(1, None))
        .build()
        .await
        .unwrap();
    let test_stack = t.get(0);
    let stack = test_stack.connect_stack().unwrap();
    let if_id = test_stack.get_endpoint_id(1);
    let mut addr = new_ipv4_addr_subnet([192, 168, 0, 1], 24).into_fidl();

    // The first IP address added should succeed.
    let () = stack
        .add_interface_address(if_id, &mut addr)
        .await
        .squash_result()
        .expect("Add interface address should succeed");
    let if_info = stack
        .get_interface_info(if_id)
        .await
        .squash_result()
        .expect("Get interface info should succeed");
    assert!(if_info.properties.addresses.contains(&addr));

    // Adding the same IP address again should fail with already exists.
    let err = stack
        .add_interface_address(if_id, &mut addr)
        .await
        .expect("Add interface address FIDL call should succeed")
        .expect_err("Adding same address should fail");
    assert_eq!(err, fidl_net_stack::Error::AlreadyExists);

    // Deleting an IP address that exists should succeed.
    let () = stack
        .del_interface_address(if_id, &mut addr)
        .await
        .squash_result()
        .expect("Delete interface address succeeds");
    let if_info = stack
        .get_interface_info(if_id)
        .await
        .squash_result()
        .expect("Get interface info should succeed");
    assert!(!if_info.properties.addresses.contains(&addr));

    // Deleting an IP address that doesn't exist should fail with not found.
    let err = stack
        .del_interface_address(if_id, &mut addr)
        .await
        .expect("Delete interface address FIDL call should succeed")
        .expect_err("Deleting non-existent address should fail");
    assert_eq!(err, fidl_net_stack::Error::NotFound);
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
        destination: fidl_net_stack::ForwardingDestination::DeviceId(if_id),
    };
    let mut fwd_entry2 = fidl_net_stack::ForwardingEntry {
        subnet: fidl_net::Subnet {
            addr: fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: [10, 0, 0, 0] }),
            prefix_len: 24,
        },
        destination: fidl_net_stack::ForwardingDestination::DeviceId(if_id),
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
        destination: fidl_net_stack::ForwardingDestination::DeviceId(if_id),
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
        destination: fidl_net_stack::ForwardingDestination::DeviceId(if_id),
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
        destination: fidl_net_stack::ForwardingDestination::DeviceId(10),
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
    let mut t = TestSetupBuilder::new()
        .add_endpoint()
        .add_stack(StackSetupBuilder::new().add_endpoint(1, None))
        .build()
        .await
        .unwrap();

    let test_stack = t.get(0);
    let stack = test_stack.connect_stack().unwrap();
    let if_id = test_stack.get_endpoint_id(1);

    let route1 = EntryEither::new(
        SubnetEither::new(Ipv4Addr::from([192, 168, 0, 0]).into(), 24).unwrap(),
        EntryDest::Local {
            device: test_stack.ctx().await.dispatcher().get_core_id(if_id).unwrap(),
        },
    )
    .unwrap();
    let route2 = EntryEither::new(
        SubnetEither::new(Ipv4Addr::from([10, 0, 0, 0]).into(), 24).unwrap(),
        EntryDest::Remote {
            next_hop: SpecifiedAddr::new(Ipv4Addr::from([10, 0, 0, 1]).into()).unwrap(),
        },
    )
    .unwrap();

    let () = test_stack
        .with_ctx(|ctx| {
            // add a couple of routes directly into core:
            netstack3_core::add_route(ctx, route1).unwrap();
            netstack3_core::add_route(ctx, route2).unwrap();
        })
        .await;

    let routes = stack.get_forwarding_table().await.expect("Can get forwarding table");
    assert_eq!(routes.len(), 2);
    let routes: Vec<_> = test_stack
        .with_ctx(|ctx| {
            routes
                .into_iter()
                .map(|e| EntryEither::try_from_fidl_with_ctx(ctx.dispatcher(), e).unwrap())
                .collect()
        })
        .await;
    assert!(routes.iter().any(|e| e == &route1));
    assert!(routes.iter().any(|e| e == &route2));

    // delete route1:
    let mut fidl = route1.into_subnet_dest().0.into_fidl();
    let () = stack
        .del_forwarding_entry(&mut fidl)
        .await
        .squash_result()
        .expect("can delete device forwarding entry");
    // can't delete again:
    let mut fidl = route1.into_subnet_dest().0.into_fidl();
    assert_eq!(
        stack.del_forwarding_entry(&mut fidl).await.unwrap().unwrap_err(),
        fidl_net_stack::Error::NotFound
    );

    // check that route was deleted (should've disappeared from core)
    let all_routes: Vec<_> =
        netstack3_core::get_all_routes(test_stack.ctx().await.deref_mut()).collect();
    assert!(!all_routes.iter().any(|e| e == &route1));
    assert!(all_routes.iter().any(|e| e == &route2));

    // delete route2:
    let mut fidl = route2.into_subnet_dest().0.into_fidl();
    let () = stack
        .del_forwarding_entry(&mut fidl)
        .await
        .squash_result()
        .expect("can delete next-hop forwarding entry");
    // can't delete again:
    let mut fidl = route2.into_subnet_dest().0.into_fidl();
    assert_eq!(
        stack.del_forwarding_entry(&mut fidl).await.unwrap().unwrap_err(),
        fidl_net_stack::Error::NotFound
    );

    // check that both routes were deleted (should've disappeared from core)
    let all_routes: Vec<_> =
        netstack3_core::get_all_routes(test_stack.ctx().await.deref_mut()).collect();
    assert!(!all_routes.iter().any(|e| e == &route1));
    assert!(!all_routes.iter().any(|e| e == &route2));
}

#[fasync::run_singlethreaded(test)]
async fn test_add_remote_routes() {
    let mut t = TestSetupBuilder::new().add_empty_stack().build().await.unwrap();
    let test_stack = t.get(0);
    let stack = test_stack.connect_stack().unwrap();

    let mut fwd_entry = fidl_net_stack::ForwardingEntry {
        subnet: fidl_net::Subnet {
            addr: fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: [192, 168, 0, 0] }),
            prefix_len: 24,
        },
        destination: fidl_net_stack::ForwardingDestination::NextHop(fidl_net::IpAddress::Ipv4(
            fidl_net::Ipv4Address { addr: [192, 168, 0, 1] },
        )),
    };

    let () = stack
        .add_forwarding_entry(&mut fwd_entry)
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
        destination: fidl_net_stack::ForwardingDestination::NextHop(fidl_net::IpAddress::Ipv4(
            fidl_net::Ipv4Address { addr: [192, 168, 0, 1] },
        )),
    };
    assert_eq!(
        stack.add_forwarding_entry(&mut bad_entry).await.unwrap().unwrap_err(),
        fidl_net_stack::Error::AlreadyExists
    );
}
