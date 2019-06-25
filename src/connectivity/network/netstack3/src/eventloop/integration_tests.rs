// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use crate::eventloop::util::{FidlCompatible, IntoFidlExt};
use failure::{format_err, Error};
use fidl::encoding::Decodable;
use fidl_fuchsia_netemul_network as net;
use fidl_fuchsia_netemul_sandbox as sandbox;
use fuchsia_async as fasync;
use fuchsia_component::client;
use netstack3_core::{AddrSubnetEither, IpAddr, Ipv4Addr};
use std::convert::TryFrom;
use zx;

struct TestStack {
    event_loop: EventLoop,
    event_sender: futures::channel::mpsc::UnboundedSender<Event>,
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
}

fn new_endpoint_setup(name: String) -> net::EndpointSetup {
    net::EndpointSetup { config: None, link_up: true, name }
}

#[derive(Clone)]
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

impl TestSetup {
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

        println!("Created base network");
        let mut test_setup = TestSetup { sandbox, _nets: handle, stacks: Vec::new() };

        for cfg in stacks {
            await!(test_setup.new_stack(cfg));
        }

        Ok(test_setup)
    }

    async fn new_stack<'a>(&'a mut self, cfg: StackConfig) -> Result<(), Error> {
        // get the endpoint from the sandbox config:
        let endpoint = await!(self.get_endpoint(&cfg.ep_name))?;

        let (event_sender, evt_rcv) = futures::channel::mpsc::unbounded();
        let event_loop = EventLoop::new_with_channels(event_sender.clone(), evt_rcv);
        let mut stack = TestStack { event_loop, event_sender };
        let cli = stack.connect_stack()?;

        let (mut signal_sender, mut signal_rcv) =
            futures::channel::mpsc::unbounded::<Result<u64, Error>>();
        fasync::spawn_local(async move {
            signal_sender.unbounded_send(await!(configure_stack(cli, endpoint, cfg))).unwrap();
        });

        let if_id = await!(stack.event_loop.run_until(&mut signal_rcv))??;

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

#[fasync::run_singlethreaded]
#[test]
async fn test_ping() {
    // simple test to ping between two stacks:
    let _t = await!(TestSetup::new_simple_network(
        vec![StackConfig::new_ipv4("bob", [192, 168, 0, 1], 24)].into_iter()
    ))
    .expect("Test Setup succeeds");

    // TODO(brunodalbo): for now, we're just checking that we can build stacks
    //  with the helper functions in this mod. Come back to this test and
    //  actually create two stacks that ping
}
