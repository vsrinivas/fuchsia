// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error};
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
}

impl TestStack {
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
        event_loop.ctx.dispatcher().test_events = Some(test_sender);
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
        TestStack { event_loop, event_sender, data: data_clone, test_events: test_events_clone }
    }
}

struct TestSetup {
    sandbox: sandbox::SandboxProxy,
    _nets: fidl::endpoints::ClientEnd<net::SetupHandleMarker>,
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

    async fn new_simple_network<N: Iterator<Item = StackConfig> + Clone>(
        stacks: N,
    ) -> Result<TestSetup, Error> {
        set_logger_for_test();
        let sandbox = client::connect_to_service::<sandbox::SandboxMarker>()?;
        let (net_ctx, net_ctx_server) =
            fidl::endpoints::create_proxy::<net::NetworkContextMarker>()?;
        sandbox.get_network_context(net_ctx_server)?;

        let (status, handle) = await!(net_ctx.setup(
            &mut vec![&mut net::NetworkSetup {
                name: "test_net".to_owned(),
                config: net::NetworkConfig::new_empty(),
                endpoints: stacks.clone().map(|s| new_endpoint_setup(s.ep_name)).collect(),
            }]
            .into_iter()
        ))?;

        let handle = match handle {
            Some(handle) => handle,
            None => {
                return Err(format_err!("Create network failed: {}", status));
            }
        };

        debug!("Created base network");
        let mut test_setup = TestSetup { sandbox, _nets: handle, stacks: Vec::new() };

        for cfg in stacks {
            await!(test_setup.new_stack(cfg));
        }

        Ok(test_setup)
    }

    async fn new_stack<'a>(&'a mut self, cfg: StackConfig) -> Result<(), Error> {
        debug!("Adding stack: {:?}", cfg);
        // get the endpoint from the sandbox config:
        let endpoint = await!(self.get_endpoint(&cfg.ep_name))?;

        let mut stack = TestStack::new();
        let cli = stack.connect_stack()?;

        let (mut signal_sender, mut signal_rcv) =
            futures::channel::mpsc::unbounded::<Result<u64, Error>>();
        fasync::spawn_local(async move {
            signal_sender.unbounded_send(await!(configure_stack(cli, endpoint, cfg))).unwrap();
        });

        let if_id = await!(stack.event_loop.run_until(signal_rcv.next()))?.unwrap()?;

        // check that we actually have what was transmitted over fidl:
        assert!(stack
            .event_loop
            .ctx
            .dispatcher()
            .devices
            .iter()
            .find(|d| d.id.id() == if_id)
            .is_some());

        self.stacks.push(stack);
        Ok(())
    }
}

fn new_endpoint_setup(name: String) -> net::EndpointSetup {
    net::EndpointSetup { config: None, link_up: true, name }
}

#[derive(Clone, Debug)]
struct StackConfig {
    ep_name: String,
    static_addr: AddrSubnetEither,
}

impl StackConfig {
    fn new_ipv4<S: Into<String>>(ep_name: S, ip: [u8; 4], prefix: u8) -> Self {
        Self {
            ep_name: ep_name.into(),
            static_addr: AddrSubnetEither::new(IpAddr::V4(Ipv4Addr::from(ip)), prefix).unwrap(),
        }
    }
}

async fn configure_stack(
    cli: fidl_fuchsia_net_stack::StackProxy,
    endpoint: fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>,
    cfg: StackConfig,
) -> Result<u64, Error> {
    // add interface:
    let if_id = match await!(cli.add_ethernet_interface("fake_topo_path", endpoint))? {
        (None, id) => id,
        (Some(err), _) => {
            return Err(format_err!("Error adding interface: {:?}", err));
        }
    };

    // add address:
    if let Some(err) = await!(cli.add_interface_address(if_id, &mut cfg.static_addr.into_fidl()))? {
        return Err(format_err!("Error adding address: {:?}", err));
    }

    // add route:
    let (_, subnet) = AddrSubnetEither::try_from(cfg.static_addr)
        .expect("Invalid test subnet configuration")
        .into_addr_subnet();
    if let Some(err) =
        await!(cli.add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
            subnet: cfg.static_addr.into_addr_subnet().1.into_fidl(),
            destination: fidl_fuchsia_net_stack::ForwardingDestination::DeviceId(if_id),
        }))?
    {
        return Err(format_err!("Error adding forwarding rule: {:?}", err));
    }

    Ok(if_id)
}

#[fasync::run_singlethreaded]
#[test]
async fn test_ping() {
    const ALICE_IP: [u8; 4] = [192, 168, 0, 1];
    const BOB_IP: [u8; 4] = [192, 168, 0, 2];
    // simple test to ping between two stacks:
    let mut t = await!(TestSetup::new_simple_network(
        vec![
            StackConfig::new_ipv4("alice", ALICE_IP, 24),
            StackConfig::new_ipv4("bob", BOB_IP, 24),
        ]
        .into_iter()
    ))
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
