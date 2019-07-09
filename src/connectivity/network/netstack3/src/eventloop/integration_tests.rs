// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error, ResultExt};
use fidl::encoding::Decodable;
use fidl_fuchsia_netemul_network as net;
use fidl_fuchsia_netemul_sandbox as sandbox;
use fuchsia_async as fasync;
use fuchsia_component::client;
use netstack3_core::{icmp as core_icmp, AddrSubnetEither, IpAddr, Ipv4, Ipv4Addr};
use pin_utils::pin_mut;
use std::collections::HashMap;
use std::convert::TryFrom;
use std::sync::{Arc, Mutex, Once};
use zx;

use super::*;
use crate::eventloop::util::{FidlCompatible, IntoFidlExt};
use future::{Future, FutureExt};
use futures::StreamExt;

/// log::Log implementation that uses stdout.
///
/// Useful when debugging tests.
struct Logger;

impl log::Log for Logger {
    fn enabled(&self, _metadata: &log::Metadata) -> bool {
        true
    }

    fn log(&self, record: &log::Record) {
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

pub enum TestEvent {
    DeviceStatusChanged { id: u64, status: EthernetStatus },
    IcmpEchoReply { conn: IcmpConnectionId, seq_num: u16, data: Vec<u8> },
}

#[derive(Default)]
struct TestData {
    device_status_cache: HashMap<u64, EthernetStatus>,
}

struct TestStack {
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

    async fn wait_for_interface_online(&mut self, if_id: u64) {
        if let Some(status) = self.data.lock().unwrap().device_status_cache.get(&if_id) {
            if status.contains(EthernetStatus::ONLINE) {
                // already online
                return;
            }
        }

        // install event listener and wait for event:
        let (snd, rcv) = mpsc::unbounded();
        self.set_event_listener(snd);

        let mut rcv = rcv.filter_map(async move |e| match e {
            TestEvent::DeviceStatusChanged { id, status } => {
                if if_id == id && status.contains(EthernetStatus::ONLINE) {
                    Some(())
                } else {
                    None
                }
            }
            _ => None,
        });
        pin_mut!(rcv);
        let () = await!(self.event_loop.run_until(rcv.next()))
            .expect("Wait for interface signal")
            .unwrap();

        // the cache should have the online entry now:
        assert!(self
            .data
            .lock()
            .unwrap()
            .device_status_cache
            .get(&if_id)
            .unwrap()
            .contains(EthernetStatus::ONLINE));

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
        let (event_sender, evt_rcv) = futures::channel::mpsc::unbounded();
        let mut event_loop = EventLoop::new_with_channels(event_sender.clone(), evt_rcv);
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
                let evt = if let Some(evt) = await!(test_receiver.next()) {
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
    async fn run_future<F: Future>(&mut self, fut: F) -> F::Output {
        pin_mut!(fut);
        await!(self.event_loop.run_until(fut)).expect("Stack execution failed")
    }
}

/// Helper trait to reduce boilerplate issuing calls to netstack FIDL.
trait NetstackFidlReturn {
    type Item;
    fn into_result(self) -> Result<Self::Item, fidl_net_stack::Error>;
}

/// Helper trait to reduce boilerplate issuing FIDL calls.
trait FidlResult {
    type Item;
    fn squash_result(self) -> Result<Self::Item, Error>;
}

impl<R> NetstackFidlReturn for (Option<Box<fidl_net_stack::Error>>, R) {
    type Item = R;
    fn into_result(self) -> Result<R, fidl_net_stack::Error> {
        match self {
            (Some(err), _) => Err(*err),
            (None, value) => Ok(value),
        }
    }
}

impl NetstackFidlReturn for Option<Box<fidl_net_stack::Error>> {
    type Item = ();
    fn into_result(self) -> Result<(), fidl_net_stack::Error> {
        match self {
            Some(err) => Err(*err),
            None => Ok(()),
        }
    }
}

impl<R> FidlResult for Result<R, fidl::Error>
where
    R: NetstackFidlReturn,
{
    type Item = R::Item;

    fn squash_result(self) -> Result<Self::Item, Error> {
        match self {
            Ok(r) => r.into_result().map_err(|e| format_err!("Netstack error: {:?}", e)),
            Err(e) => Err(e.into()),
        }
    }
}

struct TestSetup {
    sandbox: sandbox::SandboxProxy,
    nets: Option<fidl::endpoints::ClientEnd<net::SetupHandleMarker>>,
    stacks: Vec<TestStack>,
}

impl TestSetup {
    fn get(&mut self, i: usize) -> &mut TestStack {
        &mut self.stacks[i]
    }

    fn ctx(&mut self, i: usize) -> &mut Context<EventLoopInner> {
        &mut self.get(i).event_loop.ctx
    }

    /// Runs all stacks in `TestSetup` until the future `fut` completes.
    async fn run_until<V>(&mut self, fut: impl Future<Output = V> + Unpin) -> Result<V, Error> {
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
        let stacks_fut = futures::stream::iter(stacks).for_each_concurrent(
            None,
            async move |(mut rcv, stack)| {
                await!(stack.event_loop.run_until(rcv.next())).expect("Stack loop run error");
            },
        );
        pin_mut!(stacks_fut);

        // run both futures, but the receiver must end first:
        match await!(future::select(fut, stacks_fut)) {
            future::Either::Left((result, other)) => {
                // finish all other tasks:
                for mut snd in end_senders.into_iter() {
                    snd.unbounded_send(()).unwrap();
                }
                let () = await!(other);
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
        let ep = match await!(epm.get_endpoint(ep_name))? {
            Some(ep) => ep.into_proxy()?,
            None => {
                return Err(format_err!("Failed to retrieve endpoint {}", ep_name));
            }
        };

        Ok(await!(ep.get_ethernet_device())?)
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
        let (status, handle) = await!(net_ctx.setup(
            &mut vec![&mut net::NetworkSetup {
                name: "test_net".to_owned(),
                config: net::NetworkConfig::new_empty(),
                endpoints: ep_names.map(|name| new_endpoint_setup(name)).collect(),
            }]
            .into_iter()
        ))?;

        self.nets = Some(handle.ok_or_else(|| format_err!("Create network failed: {}", status))?);
        Ok(())
    }

    fn add_stack(&mut self, stack: TestStack) {
        self.stacks.push(stack)
    }
}

fn test_ep_name(i: usize) -> String {
    format!("test-ep{}", i)
}

struct TestSetupBuilder {
    endpoints: Vec<String>,
    stacks: Vec<StackSetupBuilder>,
}

impl TestSetupBuilder {
    /// Creates an empty `SetupBuilder`.
    fn new() -> Self {
        Self { endpoints: Vec::new(), stacks: Vec::new() }
    }

    /// Adds an automatically-named endpoint to the setup builder. The automatic
    /// names are taken using [`test_ep_name`] with index starting at 1.
    ///
    /// Multiple calls to `add_endpoint` will result in the creation of multiple
    /// endpoints with sequential indices.
    fn add_endpoint(self) -> Self {
        let id = self.endpoints.len() + 1;
        self.add_named_endpoint(test_ep_name(id))
    }

    /// Ads an endpoint with a given `name`.
    fn add_named_endpoint(mut self, name: impl Into<String>) -> Self {
        self.endpoints.push(name.into());
        self
    }

    /// Adds a stack to create upon building. Stack configuration is provided
    /// by [`StackSetupBuilder`].
    fn add_stack(mut self, stack: StackSetupBuilder) -> Self {
        self.stacks.push(stack);
        self
    }

    /// Adds an empty stack to create upon building. An empty stack contains
    /// no endpoints.
    fn add_empty_stack(mut self) -> Self {
        self.stacks.push(StackSetupBuilder::new());
        self
    }

    /// Attempts to build a [`TestSetup`] with the provided configuration.
    async fn build(self) -> Result<TestSetup, Error> {
        let mut setup = TestSetup::new()?;
        if !self.endpoints.is_empty() {
            let () = await!(setup.configure_network(self.endpoints.into_iter()))?;
        }

        // configure all the stacks:
        for stack_cfg in self.stacks.into_iter() {
            println!("Adding stack: {:?}", stack_cfg);
            let mut stack = TestStack::new();

            for (ep_name, addr) in stack_cfg.endpoints.into_iter() {
                // get the endpoint from the sandbox config:
                let endpoint = await!(setup.get_endpoint(&ep_name))?;
                let cli = stack.connect_stack()?;
                let if_id = await!(stack.run_future(configure_stack(cli, endpoint, addr)))?;
                stack.endpoint_ids.insert(ep_name, if_id);
            }

            setup.add_stack(stack)
        }

        Ok(setup)
    }
}

/// Shorthand function to create an IPv4 [`AddrSubnetEither`].
fn new_ipv4_addr_subnet(ip: [u8; 4], prefix: u8) -> AddrSubnetEither {
    AddrSubnetEither::new(IpAddr::V4(Ipv4Addr::from(ip)), prefix).unwrap()
}

/// Helper struct to create stack configurations for [`TestSetupBuilder`].
#[derive(Debug)]
struct StackSetupBuilder {
    endpoints: Vec<(String, Option<AddrSubnetEither>)>,
}

impl StackSetupBuilder {
    /// Creates a new empty stack (no endpoints) configuration.
    fn new() -> Self {
        Self { endpoints: Vec::new() }
    }

    /// Adds endpoint number  `index` with optional address configuration
    /// `address` to the builder.
    fn add_endpoint(self, index: usize, address: Option<AddrSubnetEither>) -> Self {
        self.add_named_endpoint(test_ep_name(index), address)
    }

    /// Adds named endpoint `name` with optional address configuration `address`
    /// to the builder.
    fn add_named_endpoint(
        mut self,
        name: impl Into<String>,
        address: Option<AddrSubnetEither>,
    ) -> Self {
        self.endpoints.push((name.into(), address));
        self
    }
}

async fn configure_stack(
    cli: fidl_fuchsia_net_stack::StackProxy,
    endpoint: fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>,
    addr: Option<AddrSubnetEither>,
) -> Result<u64, Error> {
    // add interface:
    let if_id = await!(cli.add_ethernet_interface("fake_topo_path", endpoint))
        .squash_result()
        .context("Add ethernet interface")?;

    let addr = match addr {
        Some(a) => a,
        None => return Ok(if_id),
    };

    // add address:
    let () = await!(cli.add_interface_address(if_id, &mut addr.into_fidl()))
        .squash_result()
        .context("Add interface address")?;

    // add route:
    let (_, subnet) = AddrSubnetEither::try_from(addr)
        .expect("Invalid test subnet configuration")
        .into_addr_subnet();

    let () = await!(cli.add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
        subnet: addr.into_addr_subnet().1.into_fidl(),
        destination: fidl_fuchsia_net_stack::ForwardingDestination::DeviceId(if_id),
    }))
    .squash_result()
    .context("Add forwarding entry")?;

    Ok(if_id)
}

fn new_endpoint_setup(name: String) -> net::EndpointSetup {
    net::EndpointSetup { config: None, link_up: true, name }
}

#[fasync::run_singlethreaded]
#[test]
async fn test_ping() {
    const ALICE_IP: [u8; 4] = [192, 168, 0, 1];
    const BOB_IP: [u8; 4] = [192, 168, 0, 2];
    // simple test to ping between two stacks:
    let mut t = await!(TestSetupBuilder::new()
        .add_named_endpoint("bob")
        .add_named_endpoint("alice")
        .add_stack(
            StackSetupBuilder::new()
                .add_named_endpoint("alice", Some(new_ipv4_addr_subnet(ALICE_IP, 24)))
        )
        .add_stack(
            StackSetupBuilder::new()
                .add_named_endpoint("bob", Some(new_ipv4_addr_subnet(BOB_IP, 24)))
        )
        .build())
    .expect("Test Setup succeeds");

    // wait for interfaces on both stacks to signal online correctly:
    await!(t.get(0).wait_for_interface_online(1));
    await!(t.get(1).wait_for_interface_online(1));

    const CONN_ID: IcmpConnectionId = 13;
    const ICMP_ID: u16 = 1;

    debug!("creating icmp connection");
    // create icmp connection on alice:
    let () = core_icmp::new_icmp_connection::<_, Ipv4Addr>(
        t.ctx(0),
        CONN_ID,
        ALICE_IP.into(),
        BOB_IP.into(),
        ICMP_ID,
    )
    .unwrap();

    let ping_bod = [1, 2, 3, 4, 5, 6];

    let (sender, recv) = mpsc::unbounded();

    t.get(0).set_event_listener(sender);

    let mut recv = recv.filter_map(async move |f| match f {
        TestEvent::IcmpEchoReply { conn, seq_num, data } => Some((conn, seq_num, data)),
        _ => None,
    });
    pin_mut!(recv);

    // alice will ping bob 4 times:
    for seq in 1..=4 {
        debug!("sending ping seq {}", seq);
        // send ping request:
        core_icmp::send_icmp_echo_request::<_, Ipv4>(t.ctx(0), &CONN_ID, seq, &ping_bod);

        // wait until the response comes along:
        let (rsp_id, rsp_seq, rsp_bod) = await!(t.run_until(recv.next())).unwrap().unwrap();
        debug!("Received ping seq {}", rsp_seq);
        // validate seq and body:
        assert_eq!(rsp_id, CONN_ID);
        assert_eq!(rsp_seq, seq);
        assert_eq!(&rsp_bod, &ping_bod);
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn test_add_remove_interface() {
    let mut t = await!(TestSetupBuilder::new().add_endpoint().add_empty_stack().build()).unwrap();
    let ep = await!(t.get_endpoint("test-ep1")).unwrap();
    let test_stack = t.get(0);
    let stack = test_stack.connect_stack().unwrap();
    let if_id = await!(test_stack.run_future(stack.add_ethernet_interface("fake_topo_path", ep)))
        .squash_result()
        .expect("Add interface succeeds");
    // check that the created ID matches the one saved in the event loop state:
    let dev_info =
        test_stack.event_loop.ctx.dispatcher().get_device_info(if_id).expect("Get device client");
    assert_eq!(dev_info.path(), "fake_topo_path");

    // remove the interface:
    let () = await!(test_stack.run_future(stack.del_ethernet_interface(if_id)))
        .squash_result()
        .expect("Remove interface");
    // ensure the interface disappeared from records:
    assert!(test_stack.event_loop.ctx.dispatcher().get_device_info(if_id).is_none());

    // if we try to remove it again, NotFound should be returned:
    let res = await!(test_stack.run_future(stack.del_ethernet_interface(if_id)))
        .unwrap()
        .into_result()
        .expect_err("Failed to remove twice");
    assert_eq!(res.type_, fidl_net_stack::ErrorType::NotFound);
}

#[fasync::run_singlethreaded]
#[test]
async fn test_list_interfaces() {
    let mut t = await!(TestSetupBuilder::new()
        .add_endpoint()
        .add_endpoint()
        .add_endpoint()
        .add_empty_stack()
        .build())
    .unwrap();

    let stack = t.get(0).connect_stack().unwrap();
    // check that we can list when no interfaces exist:
    // TODO(brunodalbo) this test may require tunning when we expose the
    //  loopback interface over FIDL
    let ifs = await!(t.get(0).run_future(stack.list_interfaces())).expect("List interfaces");
    assert!(ifs.is_empty());

    let mut if_props = HashMap::new();
    // collect created endpoint and add them to the stack:
    for i in 1..=3 {
        let ep_name = test_ep_name(i);
        let ep = await!(t.get_endpoint(&ep_name)).unwrap().into_proxy().unwrap();
        let ep_info = await!(ep.get_info()).unwrap();

        let ep = await!(t.get_endpoint(&ep_name)).unwrap();
        let if_id = await!(t.get(0).run_future(stack.add_ethernet_interface("fake_topo_path", ep)))
            .squash_result()
            .expect("Add interface succeeds");
        if_props.insert(if_id, ep_info);
    }

    let mut test_stack = t.get(0);
    let ifs = await!(test_stack.run_future(stack.list_interfaces())).expect("List interfaces");
    assert_eq!(ifs.len(), 3);
    // check that what we served over FIDL is correct:
    for ifc in ifs.iter() {
        let props = if_props.remove(&ifc.id).unwrap();
        assert_eq!(&ifc.properties.path, "fake_topo_path");
        assert_eq!(ifc.properties.mac.as_ref().unwrap().as_ref(), &props.mac);
        assert_eq!(ifc.properties.mtu, props.mtu);
        // TODO(brunodalbo) also test addresses and interface status once
        //  it's implemented.
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn test_get_interface_info() {
    let mut t = await!(TestSetupBuilder::new()
        .add_endpoint()
        .add_stack(StackSetupBuilder::new().add_endpoint(1, None))
        .build())
    .unwrap();
    let ep_name = test_ep_name(1);
    let ep = await!(t.get_endpoint(&ep_name)).unwrap();
    // get the device info from the ethernet driver:
    let ep_info = await!(ep.into_proxy().unwrap().get_info()).unwrap();
    let test_stack = t.get(0);
    let stack = test_stack.connect_stack().unwrap();
    let if_id = test_stack.get_endpoint_id(1);

    // get the interface info:
    let if_info = await!(test_stack.run_future(stack.get_interface_info(if_id)))
        .unwrap()
        .0
        .expect("Get interface info");
    assert_eq!(&if_info.properties.path, "fake_topo_path");
    assert_eq!(if_info.properties.mac.as_ref().unwrap().as_ref(), &ep_info.mac);
    assert_eq!(if_info.properties.mtu, ep_info.mtu);
    // TODO(brunodalbo) also test addresses and interface status once
    //  it's implemented.

    // check that we get the correct error for a non-existing interface id:
    let err = await!(test_stack.run_future(stack.get_interface_info(12345)))
        .unwrap()
        .1
        .expect("Get interface info fails");
    assert_eq!(err.type_, fidl_net_stack::ErrorType::NotFound);
}
