// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error, ResultExt};
use fidl::encoding::Decodable;
use fidl_fuchsia_io as fidl_io;
use fidl_fuchsia_net_stack_ext::FidlReturn;
use fidl_fuchsia_netemul_network as net;
use fidl_fuchsia_netemul_sandbox as sandbox;
use fuchsia_async as fasync;
use fuchsia_component::client;
use futures::{future, Future};
use net_types::ip::{AddrSubnetEither, IpAddr, Ipv4Addr, Ipv6Addr, SubnetEither};
use net_types::{SpecifiedAddr, Witness};
use netstack3_core::icmp::{self as core_icmp, IcmpConnId};
use netstack3_core::EntryDest;
use packet::Buf;
use pin_utils::pin_mut;
use std::collections::HashMap;
use std::convert::TryFrom;
use std::sync::{Arc, Mutex, Once};

use super::*;
use crate::eventloop::util::{FidlCompatible, IntoFidlExt};

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
fn set_logger_for_test() {
    // log::set_logger will panic if called multiple times; using a Once makes
    // set_logger_for_test idempotent
    LOGGER_ONCE.call_once(|| {
        log::set_logger(&LOGGER).unwrap();
        log::set_max_level(log::LevelFilter::Trace);
    })
}

#[derive(Debug)]
pub enum TestEvent {
    DeviceStatusChanged { id: u64, status: EthernetStatus },
    IcmpEchoReply { conn: IcmpConnId, seq_num: u16, data: Vec<u8> },
}

#[derive(Default)]
struct TestData {
    device_status_cache: HashMap<u64, EthernetStatus>,
}

pub struct TestStack {
    event_loop: EventLoop,
    event_sender: mpsc::UnboundedSender<Event>,
    test_events: Arc<Mutex<Option<mpsc::UnboundedSender<TestEvent>>>>,
    data: Arc<Mutex<TestData>>,
    endpoint_ids: HashMap<String, u64>,
}

impl TestStack {
    fn get_endpoint_id(&self, index: usize) -> u64 {
        self.get_named_endpoint_id(test_ep_name(index))
    }

    fn get_named_endpoint_id(&self, name: impl Into<String>) -> u64 {
        *self.endpoint_ids.get(&name.into()).unwrap()
    }

    fn connect_stack(&self) -> Result<fidl_fuchsia_net_stack::StackProxy, Error> {
        let (stack, rs) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_stack::StackMarker>()?;
        let events =
            self.event_sender.clone().sink_map_err(|e| panic!("event sender error: {}", e));
        fasync::spawn_local(
            rs.map_ok(Event::FidlStackEvent).map_err(|_| ()).forward(events).map(|_| ()),
        );
        Ok(stack)
    }

    pub fn connect_socket_provider(
        &self,
    ) -> Result<fidl_fuchsia_posix_socket::ProviderProxy, Error> {
        let (stack, rs) = fidl::endpoints::create_proxy_and_stream::<
            fidl_fuchsia_posix_socket::ProviderMarker,
        >()?;
        let events =
            self.event_sender.clone().sink_map_err(|e| panic!("event sender error: {}", e));
        fasync::spawn_local(
            rs.map_ok(Event::FidlSocketProviderEvent).map_err(|_| ()).forward(events).map(|_| ()),
        );
        Ok(stack)
    }

    pub fn connect_icmp_provider(&self) -> Result<fidl_icmp::ProviderProxy, Error> {
        let (provider, rs) =
            fidl::endpoints::create_proxy_and_stream::<fidl_icmp::ProviderMarker>()?;
        let events =
            self.event_sender.clone().sink_map_err(|e| panic!("event sender error: {}", e));
        fasync::spawn_local(
            rs.map_ok(Event::FidlIcmpProviderEvent).map_err(|_| ()).forward(events).map(|_| ()),
        );
        Ok(provider)
    }

    pub async fn wait_for_interface_online(&mut self, if_id: u64) {
        let check_online = |status: &EthernetStatus| status.contains(EthernetStatus::ONLINE);
        self.wait_for_interface_status(if_id, check_online).await;
    }

    async fn wait_for_interface_offline(&mut self, if_id: u64) {
        let check_offline = |status: &EthernetStatus| !status.contains(EthernetStatus::ONLINE);
        self.wait_for_interface_status(if_id, check_offline).await;
    }

    async fn wait_for_interface_status<F: Fn(&EthernetStatus) -> bool>(
        &mut self,
        if_id: u64,
        check_status: F,
    ) {
        if let Some(status) = self.data.lock().unwrap().device_status_cache.get(&if_id) {
            if check_status(&status) {
                // already has expected status
                return;
            }
        }

        // install event listener and wait for event:
        let (snd, rcv) = mpsc::unbounded();
        self.set_event_listener(snd);

        let rcv = rcv.filter_map(|e| {
            future::ready(match e {
                TestEvent::DeviceStatusChanged { id, status } => {
                    if if_id == id && check_status(&status) {
                        Some(())
                    } else {
                        None
                    }
                }
                _ => None,
            })
        });
        pin_mut!(rcv);
        let () = self
            .event_loop
            .run_until(rcv.next())
            .await
            .expect("Wait for interface signal")
            .unwrap();

        // the cache should have the status entry now:
        assert!(check_status(&self.data.lock().unwrap().device_status_cache.get(&if_id).unwrap()));

        self.clear_event_listener();
    }

    /// Test events will be sent to the receiver end of `chan`.
    fn set_event_listener(&mut self, chan: mpsc::UnboundedSender<TestEvent>) {
        *self.test_events.lock().unwrap() = Some(chan);
    }

    /// Remove test event listener, if installed.
    fn clear_event_listener(&mut self) {
        *self.test_events.lock().unwrap() = None;
    }

    fn new() -> Self {
        // Create a new EventLoop with Duplicate Address Detection disabled for tests.
        //
        // TODO(fxb/36238): Remove this code when an event is dispatched when Duplicate Address
        // Detection finishes or when an IPv6 address has been assigned. Without such events, tests
        // do not know how long to wait for the stack to be ready for events.
        use netstack3_core::NdpConfigurations;
        let mut builder = StackStateBuilder::default();
        let mut config = NdpConfigurations::default();
        config.set_dup_addr_detect_transmits(None);
        config.set_max_router_solicitations(None);
        builder.device_builder().set_default_ndp_configs(config);

        let (event_sender, evt_rcv) = futures::channel::mpsc::unbounded();
        let mut event_loop = EventLoop::new_with_channels(builder, event_sender.clone(), evt_rcv);

        let (test_sender, mut test_receiver) = futures::channel::mpsc::unbounded();
        event_loop.ctx.dispatcher_mut().test_events = Some(test_sender);
        let data = Arc::new(Mutex::new(TestData::default()));
        let data_clone = Arc::clone(&data);
        let test_events = Arc::<Mutex<Option<mpsc::UnboundedSender<TestEvent>>>>::default();
        let test_events_clone = Arc::clone(&test_events);

        // There are some test events that we always want to be observing,
        // such as the DeviceStatusChanged event for example.
        // We set the event loop to always send test events into this
        // test_receiver which runs all the time. After doing whatever we must
        // with the intercepted event, we just forward it to whatever sender
        // is installed in TestStack.test_events (if any).
        fasync::spawn_local(async move {
            loop {
                let evt = if let Some(evt) = test_receiver.next().await {
                    evt
                } else {
                    break;
                };

                match &evt {
                    TestEvent::DeviceStatusChanged { id, status } => {
                        data.lock().unwrap().device_status_cache.insert(*id, *status);
                    }
                    _ => {}
                }
                // pass event along if any listeners are installed:
                if let Some(evts) = test_events.lock().unwrap().as_mut() {
                    evts.unbounded_send(evt).expect("Failed to forward TestEvent");
                }
            }
        });
        TestStack {
            event_loop,
            event_sender,
            data: data_clone,
            test_events: test_events_clone,
            endpoint_ids: HashMap::new(),
        }
    }

    /// Runs the test stack until the future `fut` completes.
    pub async fn run_future<F: Future>(&mut self, fut: F) -> F::Output {
        pin_mut!(fut);
        self.event_loop.run_until(fut).await.expect("Stack execution failed")
    }
}

pub struct TestSetup {
    sandbox: sandbox::SandboxProxy,
    nets: Option<fidl::endpoints::ClientEnd<net::SetupHandleMarker>>,
    stacks: Vec<TestStack>,
}

impl TestSetup {
    pub fn get(&mut self, i: usize) -> &mut TestStack {
        &mut self.stacks[i]
    }

    pub fn event_loop(&mut self, i: usize) -> &mut EventLoop {
        &mut self.get(i).event_loop
    }

    fn ctx(&mut self, i: usize) -> &mut Context<EventLoopInner> {
        &mut self.get(i).event_loop.ctx
    }

    /// Runs all stacks in `TestSetup` until the future `fut` completes.
    pub async fn run_until<V>(&mut self, fut: impl Future<Output = V> + Unpin) -> Result<V, Error> {
        // Create senders so we can signal each event loop to stop running
        let (end_senders, stacks): (Vec<_>, Vec<_>) = self
            .stacks
            .iter_mut()
            .map(|s| {
                let (snd, rcv) = mpsc::unbounded::<()>();
                (snd, (rcv, s))
            })
            .unzip();
        // let all stacks run concurrently:
        let stacks_fut =
            futures::stream::iter(stacks).for_each_concurrent(None, |(mut rcv, stack)| {
                async move {
                    stack.event_loop.run_until(rcv.next()).await.expect("Stack loop run error");
                }
            });
        pin_mut!(stacks_fut);

        // run both futures, but the receiver must end first:
        match future::select(fut, stacks_fut).await {
            future::Either::Left((result, other)) => {
                // finish all other tasks:
                for snd in end_senders.into_iter() {
                    snd.unbounded_send(()).unwrap();
                }
                let () = other.await;
                Ok(result)
            }
            _ => panic!("stacks can't finish before hitting end_senders"),
        }
    }
    async fn get_endpoint<'a>(
        &'a self,
        ep_name: &'a str,
    ) -> Result<fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>, Error>
    {
        let (net_ctx, net_ctx_server) =
            fidl::endpoints::create_proxy::<net::NetworkContextMarker>()?;
        self.sandbox.get_network_context(net_ctx_server)?;
        let (epm, epm_server) = fidl::endpoints::create_proxy::<net::EndpointManagerMarker>()?;
        net_ctx.get_endpoint_manager(epm_server)?;
        let ep = match epm.get_endpoint(ep_name).await? {
            Some(ep) => ep.into_proxy()?,
            None => {
                return Err(format_err!("Failed to retrieve endpoint {}", ep_name));
            }
        };

        Ok(ep.get_ethernet_device().await?)
    }

    async fn set_endpoint_link_up(&mut self, ep_name: &str, up: bool) -> Result<(), Error> {
        let (net_ctx, net_ctx_server) =
            fidl::endpoints::create_proxy::<net::NetworkContextMarker>()?;
        self.sandbox.get_network_context(net_ctx_server)?;
        let (epm, epm_server) = fidl::endpoints::create_proxy::<net::EndpointManagerMarker>()?;
        net_ctx.get_endpoint_manager(epm_server)?;
        if let Some(ep) = epm.get_endpoint(ep_name).await? {
            ep.into_proxy()?.set_link_up(up).await?;
            Ok(())
        } else {
            Err(format_err!("Failed to retrieve endpoint {}", ep_name))
        }
    }

    fn new() -> Result<Self, Error> {
        set_logger_for_test();
        let sandbox = client::connect_to_service::<sandbox::SandboxMarker>()?;
        Ok(Self { sandbox, nets: None, stacks: Vec::new() })
    }

    fn get_network_context(&self) -> Result<net::NetworkContextProxy, Error> {
        let (net_ctx, net_ctx_server) =
            fidl::endpoints::create_proxy::<net::NetworkContextMarker>()?;
        self.sandbox.get_network_context(net_ctx_server)?;
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

pub fn test_ep_name(i: usize) -> String {
    format!("test-ep{}", i)
}

pub struct TestSetupBuilder {
    endpoints: Vec<String>,
    stacks: Vec<StackSetupBuilder>,
}

impl TestSetupBuilder {
    /// Creates an empty `SetupBuilder`.
    pub fn new() -> Self {
        Self { endpoints: Vec::new(), stacks: Vec::new() }
    }

    /// Adds an automatically-named endpoint to the setup builder. The automatic
    /// names are taken using [`test_ep_name`] with index starting at 1.
    ///
    /// Multiple calls to `add_endpoint` will result in the creation of multiple
    /// endpoints with sequential indices.
    pub fn add_endpoint(self) -> Self {
        let id = self.endpoints.len() + 1;
        self.add_named_endpoint(test_ep_name(id))
    }

    /// Ads an endpoint with a given `name`.
    pub fn add_named_endpoint(mut self, name: impl Into<String>) -> Self {
        self.endpoints.push(name.into());
        self
    }

    /// Adds a stack to create upon building. Stack configuration is provided
    /// by [`StackSetupBuilder`].
    pub fn add_stack(mut self, stack: StackSetupBuilder) -> Self {
        self.stacks.push(stack);
        self
    }

    /// Adds an empty stack to create upon building. An empty stack contains
    /// no endpoints.
    pub fn add_empty_stack(mut self) -> Self {
        self.stacks.push(StackSetupBuilder::new());
        self
    }

    /// Attempts to build a [`TestSetup`] with the provided configuration.
    pub async fn build(self) -> Result<TestSetup, Error> {
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
                let if_id = stack.run_future(add_stack_endpoint(&cli, endpoint)).await?;
                // We'll ALWAYS await for the newly created interface to come up
                // online before returning, so users of `TestSetupBuilder` can
                // be 100% sure of the state once the setup is done.
                stack.wait_for_interface_online(if_id).await;
                if let Some(addr) = addr {
                    stack.run_future(configure_endpoint_address(&cli, if_id, addr)).await?;
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
    pub fn new() -> Self {
        Self { endpoints: Vec::new() }
    }

    /// Adds endpoint number  `index` with optional address configuration
    /// `address` to the builder.
    fn add_endpoint(self, index: usize, address: Option<AddrSubnetEither>) -> Self {
        self.add_named_endpoint(test_ep_name(index), address)
    }

    /// Adds named endpoint `name` with optional address configuration `address`
    /// to the builder.
    pub fn add_named_endpoint(
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

fn new_endpoint_setup(name: String) -> net::EndpointSetup {
    net::EndpointSetup { config: None, link_up: true, name }
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
    let conn_id = core_icmp::new_icmp_connection::<_, Ipv4Addr>(
        t.ctx(0),
        Some(SpecifiedAddr::new(ALICE_IP.into()).unwrap()),
        SpecifiedAddr::new(BOB_IP.into()).unwrap(),
        ICMP_ID,
    )
    .unwrap();

    let ping_bod = [1, 2, 3, 4, 5, 6];

    let (sender, recv) = mpsc::unbounded();

    t.get(0).set_event_listener(sender);

    let recv = recv.filter_map(|f| {
        future::ready(match f {
            TestEvent::IcmpEchoReply { conn, seq_num, data } => Some((conn, seq_num, data)),
            _ => None,
        })
    });
    pin_mut!(recv);

    // alice will ping bob 4 times:
    for seq in 1..=4 {
        debug!("sending ping seq {}", seq);
        // send ping request:
        core_icmp::send_icmpv4_echo_request(
            t.ctx(0),
            conn_id,
            seq,
            Buf::new(ping_bod.to_vec(), ..),
        );

        // wait until the response comes along:
        let (rsp_id, rsp_seq, rsp_bod) = t.run_until(recv.next()).await.unwrap().unwrap();
        debug!("Received ping seq {}", rsp_seq);
        // validate seq and body:
        assert_eq!(rsp_id, conn_id);
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
    let if_id = test_stack
        .run_future(stack.add_ethernet_interface("fake_topo_path", ep))
        .await
        .squash_result()
        .expect("Add interface succeeds");
    // check that the created ID matches the one saved in the event loop state:
    let dev_info =
        test_stack.event_loop.ctx.dispatcher().get_device_info(if_id).expect("Get device client");
    assert_eq!(dev_info.path(), "fake_topo_path");

    // remove the interface:
    let () = test_stack
        .run_future(stack.del_ethernet_interface(if_id))
        .await
        .squash_result()
        .expect("Remove interface");
    // ensure the interface disappeared from records:
    assert!(test_stack.event_loop.ctx.dispatcher().get_device_info(if_id).is_none());

    // if we try to remove it again, NotFound should be returned:
    let res = test_stack
        .run_future(stack.del_ethernet_interface(if_id))
        .await
        .unwrap()
        .expect_err("Failed to remove twice");
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

    t.get(0).wait_for_interface_online(if_id).await;

    // Get the interface info to confirm status indicators are correct.
    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);

    // Ensure that the device has been added to the core.
    assert!(t.get(0).event_loop.ctx.dispatcher().get_device_info(if_id).unwrap().is_active());

    // Setting the link down should disable the interface and remove it from
    // the core. The AdministrativeStatus should remain unchanged.
    assert!(t.set_endpoint_link_up(&ep_name, false).await.is_ok());
    t.get(0).wait_for_interface_offline(if_id).await;

    // Get the interface info to confirm that it is disabled.
    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Down);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);

    // Ensure that the device has been removed from the core.
    assert!(
        t.get(0).event_loop.ctx.dispatcher().get_device_info(if_id).unwrap().is_active() == false
    );

    // Setting the link down again should cause no effect on the device state,
    // and should be handled gracefully.
    assert!(t.set_endpoint_link_up(&ep_name, false).await.is_ok());

    // Get the interface info to confirm that it is disabled.
    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Down);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);

    // Ensure that the device has been removed from the core.
    assert!(
        t.get(0).event_loop.ctx.dispatcher().get_device_info(if_id).unwrap().is_active() == false
    );

    // Setting the link up should reenable the interface and add it to
    // the core.
    assert!(t.set_endpoint_link_up(&ep_name, true).await.is_ok());
    t.get(0).wait_for_interface_online(if_id).await;

    // Get the interface info to confirm that it is reenabled.
    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);

    // Ensure that the device has been added to the core.
    assert!(t.get(0).event_loop.ctx.dispatcher().get_device_info(if_id).unwrap().is_active());

    // Setting the link up again should cause no effect on the device state,
    // and should be handled gracefully.
    assert!(t.set_endpoint_link_up(&ep_name, true).await.is_ok());

    // Get the interface info to confirm that there have been no changes.
    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);

    // Ensure that the device has been added to the core.
    let device_info = t.get(0).event_loop.ctx.dispatcher().get_device_info(if_id).unwrap();
    assert!(device_info.is_active());

    // call directly into core to prove that the device was correctly
    // initialized (core will panic if we try to use the device and initialize
    // hasn't been called)
    let core_id = device_info.core_id().unwrap();
    netstack3_core::receive_frame(&mut t.get(0).event_loop.ctx, core_id, Buf::new(&mut [], ..));
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
    let ifs = t.get(0).run_future(stack.list_interfaces()).await.expect("List interfaces");
    assert!(ifs.is_empty());

    let mut if_props = HashMap::new();
    // collect created endpoint and add them to the stack:
    for i in 1..=3 {
        let ep_name = test_ep_name(i);
        let ep = t.get_endpoint(&ep_name).await.unwrap().into_proxy().unwrap();
        let ep_info = ep.get_info().await.unwrap();
        let mut if_ip = new_ipv4_addr_subnet([192, 168, 0, i as u8], 24).into_fidl();

        let ep = t.get_endpoint(&ep_name).await.unwrap();
        let if_id = t
            .get(0)
            .run_future(stack.add_ethernet_interface("fake_topo_path", ep))
            .await
            .squash_result()
            .expect("Add interface succeeds");
        // NOTE(brunodalbo) wait for interface to come online before trying to
        // add the address. We currently don't support adding addresses for
        // offline interfaces, because they're removed from core. This wait can
        // be removed once we allow that.
        t.get(0).wait_for_interface_online(if_id).await;
        let () = t
            .get(0)
            .run_future(stack.add_interface_address(if_id, &mut if_ip))
            .await
            .squash_result()
            .expect("Add interface address succeeds");
        if_props.insert(if_id, (ep_info, vec![if_ip]));
        t.get(0).wait_for_interface_online(i as u64).await;
    }

    let test_stack = t.get(0);
    let ifs = test_stack.run_future(stack.list_interfaces()).await.expect("List interfaces");
    assert_eq!(ifs.len(), 3);
    // check that what we served over FIDL is correct:
    for ifc in ifs.iter() {
        let (ep_info, if_ip) = if_props.remove(&ifc.id).unwrap();
        assert_eq!(&ifc.properties.topopath, "fake_topo_path");
        assert_eq!(ifc.properties.mac.as_ref().unwrap().as_ref(), &ep_info.mac);
        assert_eq!(ifc.properties.mtu, ep_info.mtu);

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

    // get the interface info:
    let if_info = test_stack
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(&if_info.properties.topopath, "fake_topo_path");
    assert_eq!(if_info.properties.mac.as_ref().unwrap().as_ref(), &ep_info.mac);
    assert_eq!(if_info.properties.mtu, ep_info.mtu);

    assert_eq!(if_info.properties.addresses, vec![ip.try_into_fidl().unwrap()]);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // check that we get the correct error for a non-existing interface id:
    let err = test_stack
        .run_future(stack.get_interface_info(12345))
        .await
        .unwrap()
        .expect_err("Get interface info fails");
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
    let if_info = test_stack
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // Disable the interface and test again, physical_status should be
    // unchanged.
    let () = test_stack
        .run_future(stack.disable_interface(if_id))
        .await
        .squash_result()
        .expect("Disable interface succeeds");

    let if_info = test_stack
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Disabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // Ensure that the device has been removed from the core.
    assert!(
        test_stack.event_loop.ctx.dispatcher().get_device_info(if_id).unwrap().is_active() == false
    );

    // Enable the interface and test again.
    let () = test_stack
        .run_future(stack.enable_interface(if_id))
        .await
        .squash_result()
        .expect("Enable interface succeeds");

    let if_info = test_stack
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // Ensure that the device has been added to the core.
    assert!(test_stack.event_loop.ctx.dispatcher().get_device_info(if_id).unwrap().is_active());

    // Check that we get the correct error for a non-existing interface id.
    assert_eq!(
        test_stack.run_future(stack.enable_interface(12345)).await.unwrap().unwrap_err(),
        fidl_net_stack::Error::NotFound
    );

    // Check that we get the correct error for a non-existing interface id.
    assert_eq!(
        test_stack.run_future(stack.disable_interface(12345)).await.unwrap().unwrap_err(),
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
    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // Disable the interface and test again, physical_status should be
    // unchanged.
    let () = t
        .get(0)
        .run_future(stack.disable_interface(if_id))
        .await
        .squash_result()
        .expect("Disable interface succeeds");

    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Disabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // Ensure that the device has been removed from the core.
    assert!(
        t.get(0).event_loop.ctx.dispatcher().get_device_info(if_id).unwrap().is_active() == false
    );

    // Setting the link down now that the interface is already down should only
    // change the cached state. Both phy and admin should be down now.
    assert!(t.set_endpoint_link_up(&ep_name, false).await.is_ok());
    t.get(0).wait_for_interface_offline(if_id).await;

    // Get the interface info to confirm that it is disabled.
    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Down);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Disabled);

    // Ensure that the device is still removed from the core.
    assert!(
        t.get(0).event_loop.ctx.dispatcher().get_device_info(if_id).unwrap().is_active() == false
    );

    // Enable the interface and test again, only cached status should be changed
    // and core state should still be disabled.
    let () = t
        .get(0)
        .run_future(stack.enable_interface(if_id))
        .await
        .squash_result()
        .expect("Enable interface succeeds");

    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Down);

    // Ensure that the device is still removed from the core.
    assert!(
        t.get(0).event_loop.ctx.dispatcher().get_device_info(if_id).unwrap().is_active() == false
    );

    // Disable the interface and test again, both should be down now.
    let () = t
        .get(0)
        .run_future(stack.disable_interface(if_id))
        .await
        .squash_result()
        .expect("Disable interface succeeds");

    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Disabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Down);

    // Ensure that the device is still removed from the core.
    assert!(
        t.get(0).event_loop.ctx.dispatcher().get_device_info(if_id).unwrap().is_active() == false
    );

    // Setting the link up should only affect cached state
    assert!(t.set_endpoint_link_up(&ep_name, true).await.is_ok());
    t.get(0).wait_for_interface_online(if_id).await;

    // Get the interface info to confirm that it is reenabled.
    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Disabled);
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);

    // Ensure that the device is still removed from the core.
    assert!(
        t.get(0).event_loop.ctx.dispatcher().get_device_info(if_id).unwrap().is_active() == false
    );

    // Finally, setting admin status up should update the cached state and
    // re-add the device to the core.
    let () = t
        .get(0)
        .run_future(stack.enable_interface(if_id))
        .await
        .squash_result()
        .expect("Enable interface succeeds");

    // Get the interface info to confirm that it is reenabled.
    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .unwrap()
        .expect("Get interface info");
    assert_eq!(if_info.properties.physical_status, PhysicalStatus::Up);
    assert_eq!(if_info.properties.administrative_status, AdministrativeStatus::Enabled);

    // Ensure that the device has been added to the core.
    assert!(t.get(0).event_loop.ctx.dispatcher().get_device_info(if_id).unwrap().is_active());
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
    let () = t
        .get(0)
        .run_future(stack.add_interface_address(if_id, &mut addr))
        .await
        .squash_result()
        .expect("Add interface address should succeed");
    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .squash_result()
        .expect("Get interface info should succeed");
    assert!(if_info.properties.addresses.contains(&addr));

    // Adding the same IP address again should fail with already exists.
    let err = t
        .get(0)
        .run_future(stack.add_interface_address(if_id, &mut addr))
        .await
        .expect("Add interface address FIDL call should succeed")
        .expect_err("Adding same address should fail");
    assert_eq!(err, fidl_net_stack::Error::AlreadyExists);

    // Deleting an IP address that exists should succeed.
    let () = t
        .get(0)
        .run_future(stack.del_interface_address(if_id, &mut addr))
        .await
        .squash_result()
        .expect("Delete interface address succeeds");
    let if_info = t
        .get(0)
        .run_future(stack.get_interface_info(if_id))
        .await
        .squash_result()
        .expect("Get interface info should succeed");
    assert!(!if_info.properties.addresses.contains(&addr));

    // Deleting an IP address that doesn't exist should fail with not found.
    let err = t
        .get(0)
        .run_future(stack.del_interface_address(if_id, &mut addr))
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

    let () = test_stack
        .run_future(stack.add_forwarding_entry(&mut fwd_entry1))
        .await
        .squash_result()
        .expect("Add forwarding entry succeeds");
    let () = test_stack
        .run_future(stack.add_forwarding_entry(&mut fwd_entry2))
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
        test_stack
            .run_future(stack.add_forwarding_entry(&mut bad_entry))
            .await
            .unwrap()
            .unwrap_err(),
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
        test_stack
            .run_future(stack.add_forwarding_entry(&mut bad_entry))
            .await
            .unwrap()
            .unwrap_err(),
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
        test_stack
            .run_future(stack.add_forwarding_entry(&mut bad_entry))
            .await
            .unwrap()
            .unwrap_err(),
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
            device: test_stack.event_loop.ctx.dispatcher().get_core_id(if_id).unwrap(),
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

    // add a couple of routes directly into core:
    netstack3_core::add_route(&mut test_stack.event_loop.ctx, route1).unwrap();
    netstack3_core::add_route(&mut test_stack.event_loop.ctx, route2).unwrap();

    let routes = test_stack
        .run_future(stack.get_forwarding_table())
        .await
        .expect("Can get forwarding table");
    assert_eq!(routes.len(), 2);
    let routes: Vec<_> = routes
        .into_iter()
        .map(|e| {
            EntryEither::try_from_fidl_with_ctx(test_stack.event_loop.ctx.dispatcher(), e).unwrap()
        })
        .collect();
    assert!(routes.iter().any(|e| e == &route1));
    assert!(routes.iter().any(|e| e == &route2));

    // delete route1:
    let mut fidl = route1.into_subnet_dest().0.into_fidl();
    let () = test_stack
        .run_future(stack.del_forwarding_entry(&mut fidl))
        .await
        .squash_result()
        .expect("can delete device forwarding entry");
    // can't delete again:
    let mut fidl = route1.into_subnet_dest().0.into_fidl();
    assert_eq!(
        test_stack.run_future(stack.del_forwarding_entry(&mut fidl)).await.unwrap().unwrap_err(),
        fidl_net_stack::Error::NotFound
    );

    // check that route was deleted (should've disappeared from core)
    let all_routes: Vec<_> =
        netstack3_core::get_all_routes(&mut test_stack.event_loop.ctx).collect();
    assert!(!all_routes.iter().any(|e| e == &route1));
    assert!(all_routes.iter().any(|e| e == &route2));

    // delete route2:
    let mut fidl = route2.into_subnet_dest().0.into_fidl();
    let () = test_stack
        .run_future(stack.del_forwarding_entry(&mut fidl))
        .await
        .squash_result()
        .expect("can delete next-hop forwarding entry");
    // can't delete again:
    let mut fidl = route2.into_subnet_dest().0.into_fidl();
    assert_eq!(
        test_stack.run_future(stack.del_forwarding_entry(&mut fidl)).await.unwrap().unwrap_err(),
        fidl_net_stack::Error::NotFound
    );

    // check that both routes were deleted (should've disappeared from core)
    let all_routes: Vec<_> =
        netstack3_core::get_all_routes(&mut test_stack.event_loop.ctx).collect();
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

    let () = test_stack
        .run_future(stack.add_forwarding_entry(&mut fwd_entry))
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
        test_stack
            .run_future(stack.add_forwarding_entry(&mut bad_entry))
            .await
            .unwrap()
            .unwrap_err(),
        fidl_net_stack::Error::AlreadyExists
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_get_socket() {
    let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
    let test_stack = t.get(0);
    let socket_provider = test_stack.connect_socket_provider().unwrap();
    let socket_response = test_stack
        .run_future(socket_provider.socket(libc::AF_INET as i16, libc::SOCK_DGRAM as i16, 0))
        .await
        .expect("Socket call succeeds");
    assert_eq!(socket_response.0, 0);
}

#[fasync::run_singlethreaded(test)]
async fn test_socket_describe() {
    let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
    let test_stack = t.get(0);
    let socket_provider = test_stack.connect_socket_provider().unwrap();
    let socket_response = test_stack
        .run_future(socket_provider.socket(libc::AF_INET as i16, libc::SOCK_DGRAM as i16, 0))
        .await
        .expect("Socket call succeeds");
    assert_eq!(socket_response.0, 0);
    let info = test_stack
        .run_future(
            socket_response.1.expect("Socket returns a channel").into_proxy().unwrap().describe(),
        )
        .await
        .expect("Describe call succeeds");
    match info {
        fidl_io::NodeInfo::Socket(_) => (),
        _ => panic!("Socket Describe call did not return Node of type Socket"),
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_main_loop() {
    let (event_sender, evt_rcv) = futures::channel::mpsc::unbounded();
    let event_loop =
        EventLoop::new_with_channels(StackStateBuilder::default(), event_sender.clone(), evt_rcv);
    fasync::spawn_local(
        event_loop.run().unwrap_or_else(|e| panic!("Event loop failed with error {:?}", e)),
    );
    let (stack, rs) =
        fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_stack::StackMarker>().unwrap();
    let events = event_sender.clone().sink_map_err(|e| panic!("event sender error: {}", e));
    fasync::spawn_local(
        rs.map_ok(Event::FidlStackEvent).map_err(|_| ()).forward(events).map(|_| ()),
    );

    assert_eq!(stack.list_interfaces().await.unwrap().len(), 0);
}
