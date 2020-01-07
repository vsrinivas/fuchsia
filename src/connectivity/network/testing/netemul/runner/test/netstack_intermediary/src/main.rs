// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    ethernet,
    fidl_fuchsia_netemul_network::{
        EndpointManagerMarker, FakeEndpointEvent, FakeEndpointMarker, NetworkContextMarker,
        NetworkManagerMarker,
    },
    fidl_fuchsia_netemul_sync::{BusMarker, BusProxy, SyncManagerMarker},
    fidl_fuchsia_netstack::{InterfaceConfig, IpAddressConfig, NetstackMarker},
    fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::TryStreamExt,
    std::str,
    structopt::StructOpt,
};

#[derive(StructOpt, Debug)]
struct Opt {
    #[structopt(long = "mock_guest")]
    is_mock_guest: bool,
    #[structopt(long = "server")]
    is_server: bool,
    #[structopt(long)]
    network_name: Option<String>,
    #[structopt(long)]
    endpoint_name: Option<String>,
    #[structopt(long)]
    server_name: Option<String>,
}

const DEFAULT_METRIC: u32 = 100;
const BUS_NAME: &'static str = "netstack-itm-bus";

fn open_bus(cli_name: &str) -> Result<BusProxy, Error> {
    let syncm = client::connect_to_service::<SyncManagerMarker>()?;
    let (bus, bus_server_end) = fidl::endpoints::create_proxy::<BusMarker>()?;
    syncm.bus_subscribe(BUS_NAME, cli_name, bus_server_end)?;
    Ok(bus)
}

async fn run_mock_guest(
    network_name: String,
    ep_name: String,
    server_name: String,
) -> Result<(), Error> {
    // Create an ethertap client and an associated ethernet device.
    let ctx = client::connect_to_service::<NetworkContextMarker>()?;
    let (epm, epm_server_end) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
    ctx.get_endpoint_manager(epm_server_end)?;
    let (netm, netm_server_end) = fidl::endpoints::create_proxy::<NetworkManagerMarker>()?;
    ctx.get_network_manager(netm_server_end)?;

    let ep = epm.get_endpoint(&ep_name).await?.unwrap().into_proxy()?;
    let net = netm.get_network(&network_name).await?.unwrap().into_proxy()?;
    let (fake_ep, fake_ep_server_end) = fidl::endpoints::create_proxy::<FakeEndpointMarker>()?;
    net.create_fake_endpoint(fake_ep_server_end)?;

    let eth_device = ep.get_ethernet_device().await?;

    let netstack = client::connect_to_service::<NetstackMarker>()?;
    let ip_addr_config = IpAddressConfig::Dhcp(true);
    let mut cfg = InterfaceConfig {
        name: "eth-test".to_string(),
        filepath: "[TBD]".to_string(),
        ip_address_config: ip_addr_config,
        metric: DEFAULT_METRIC,
    };

    let _nicid =
        netstack.add_ethernet_device(&format!("/{}", ep_name), &mut cfg, eth_device).await?;

    // Send a message to the server and expect it to be echoed back.
    let echo_string = String::from("hello");

    let bus = open_bus(&ep_name)?;
    bus.wait_for_clients(&mut vec![server_name.as_str()].drain(..), 0).await?;

    fake_ep.write(&mut echo_string.clone().into_bytes().drain(0..))?;

    println!("To Server: {}", echo_string);

    let mut ep_events = fake_ep.take_event_stream();
    while let Some(event) = ep_events.try_next().await? {
        match event {
            FakeEndpointEvent::OnData { data } => {
                let server_string = str::from_utf8(&data)?;
                assert!(
                    echo_string == server_string,
                    "Server reply ({}) did not match client message ({})",
                    server_string,
                    echo_string
                );
                println!("From Server: {}", server_string);
                break;
            }
        }
    }

    Ok(())
}

async fn run_echo_server(ep_name: String) -> Result<(), Error> {
    // Get the Endpoint that was created in the server's environment.
    let netctx = client::connect_to_service::<NetworkContextMarker>()?;
    let (epm, epm_server_end) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
    netctx.get_endpoint_manager(epm_server_end)?;

    let ep = epm.get_endpoint(&ep_name).await?;

    let ep = match ep {
        Some(ep) => ep.into_proxy().unwrap(),
        None => return Err(format_err!("Can't find endpoint {}", &ep_name)),
    };

    // Create an EthernetClient to wrap around the Endpoint's ethernet device.
    let vmo = zx::Vmo::create(256 * ethernet::DEFAULT_BUFFER_SIZE as u64)?;

    let eth_dev = ep.get_ethernet_device().await?;
    let eth_proxy = match eth_dev.into_proxy() {
        Ok(proxy) => proxy,
        _ => return Err(format_err!("Could not get ethernet proxy")),
    };

    let mut eth_client =
        ethernet::Client::new(eth_proxy, vmo, ethernet::DEFAULT_BUFFER_SIZE, &ep_name).await?;

    eth_client.start().await?;

    // Listen for a receive event from the client, echo back the client's
    // message, and then exit.
    let mut eth_events = eth_client.get_stream();
    let mut sent_response = false;

    // get on bus to unlock mock_guest part of test
    let _bus = open_bus(&ep_name)?;

    while let Some(event) = eth_events.try_next().await? {
        match event {
            ethernet::Event::Receive(rx, _flags) => {
                if !sent_response {
                    let mut data: [u8; 100] = [0; 100];
                    rx.read(&mut data);
                    let user_message = str::from_utf8(&data[0..rx.len() as usize]).unwrap();
                    println!("From client: {}", user_message);
                    eth_client.send(&data[0..rx.len() as usize]);
                    sent_response = true;

                    // Start listening for the server's response to be
                    // transmitted to the guest.
                    eth_client.tx_listen_start().await?;
                } else {
                    // The mock guest will not send anything to the server
                    // beyond its initial request.  After the server has echoed
                    // the response, the next received message will be the
                    // server's own output since it is listening for its own
                    // Tx messages.
                    break;
                }
            }
            _ => {
                continue;
            }
        }
    }

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opt = Opt::from_args();

    if opt.is_mock_guest {
        if opt.network_name == None || opt.endpoint_name == None || opt.server_name == None {
            return Err(format_err!(
                "Must provide network_name, endpoint_name, and server_name for mock guests"
            ));
        }
        run_mock_guest(
            opt.network_name.unwrap(),
            opt.endpoint_name.unwrap(),
            opt.server_name.unwrap(),
        )
        .await?;
    } else if opt.is_server {
        match opt.endpoint_name {
            Some(endpoint_name) => {
                run_echo_server(endpoint_name).await?;
            }
            None => {
                return Err(format_err!("Must provide endpoint_name for server"));
            }
        }
    }
    Ok(())
}
