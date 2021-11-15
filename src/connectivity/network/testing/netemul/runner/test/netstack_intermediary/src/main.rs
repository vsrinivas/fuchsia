// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    ethernet,
    fidl::endpoints::Proxy as _,
    fidl_fuchsia_netemul_network::{
        EndpointManagerMarker, FakeEndpointMarker, NetworkContextMarker, NetworkManagerMarker,
    },
    fidl_fuchsia_netemul_sync::{BusMarker, BusProxy, SyncManagerMarker},
    fidl_fuchsia_netstack::{InterfaceConfig, NetstackMarker},
    fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::{self, FutureExt as _, TryStreamExt as _},
    std::{str, task::Poll},
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
const NETSTACK_STRING: &str = "netstack is live!";
const VIRTUALIZATION_STRING: &str = "virtualization is live!";

fn open_bus(cli_name: &str) -> Result<BusProxy, Error> {
    let syncm = client::connect_to_protocol::<SyncManagerMarker>()?;
    let (bus, bus_server_end) = fidl::endpoints::create_proxy::<BusMarker>()?;
    syncm.bus_subscribe(BUS_NAME, cli_name, bus_server_end)?;
    Ok(bus)
}

async fn run_mock_guest(network_name: String, ep_name: String, server_name: String) {
    // Create an ethertap client and an associated ethernet device.
    let ctx =
        client::connect_to_protocol::<NetworkContextMarker>().expect("connect network context");
    let (epm, epm_server_end) =
        fidl::endpoints::create_proxy::<EndpointManagerMarker>().expect("create proxy");
    let () = ctx.get_endpoint_manager(epm_server_end).expect("get endpoint manager");
    let (netm, netm_server_end) =
        fidl::endpoints::create_proxy::<NetworkManagerMarker>().expect("create proxy");
    let () = ctx.get_network_manager(netm_server_end).expect("get network manager");

    let ep = epm
        .get_endpoint(&ep_name)
        .await
        .expect("get endpoint (transport)")
        .expect("get endpoint (application)")
        .into_proxy()
        .expect("into proxy");
    let net = netm
        .get_network(&network_name)
        .await
        .expect("get network (transport)")
        .expect("get network (application)")
        .into_proxy()
        .expect("into proxy");
    let (fake_ep, fake_ep_server_end) =
        fidl::endpoints::create_proxy::<FakeEndpointMarker>().expect("create proxy");
    let () = net.create_fake_endpoint(fake_ep_server_end).expect("create fake endpoint");

    let netstack = client::connect_to_protocol::<NetstackMarker>().expect("connect netstack");
    let mut cfg = InterfaceConfig {
        name: "eth-test".to_string(),
        filepath: "[TBD]".to_string(),
        metric: DEFAULT_METRIC,
    };

    let octets = match ep.get_device().await.expect("get device") {
        fidl_fuchsia_netemul_network::DeviceConnection::Ethernet(eth_device) => {
            let eth_device = eth_device.into_proxy().expect("into proxy");
            let fidl_fuchsia_hardware_ethernet::Info {
                features: _,
                mtu: _,
                mac: fidl_fuchsia_hardware_ethernet::MacAddress { octets },
            } = eth_device.get_info().await.expect("get info (transport)");
            let eth_device = eth_device
                .into_channel()
                .map_err(|fidl_fuchsia_hardware_ethernet::DeviceProxy { .. }| {
                    format_err!("failed to convert proxy back to channel")
                })
                .expect("into channel");
            let _nicid: u32 = netstack
                .add_ethernet_device(
                    &format!("/{}", ep_name),
                    &mut cfg,
                    fidl::endpoints::ClientEnd::new(eth_device.into_zx_channel()),
                )
                .await
                .expect("add ethernet device (transport)")
                .map_err(zx::Status::from_raw)
                .expect("add ethernet device (application)");
            octets
        }
        fidl_fuchsia_netemul_network::DeviceConnection::NetworkDevice(netdevice) => {
            panic!(
                "got unexpected NetworkDevice {:?}; expected to have been configured with Ethernet",
                netdevice
            );
        }
    };

    let tun_ctl = client::connect_to_protocol::<fidl_fuchsia_net_tun::ControlMarker>()
        .expect("connect tun control");
    let tun_device = {
        let (client, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::DeviceMarker>()
                .expect("create proxy");
        let () = tun_ctl
            .create_device(
                fidl_fuchsia_net_tun::DeviceConfig {
                    blocking: Some(true),
                    ..fidl_fuchsia_net_tun::DeviceConfig::EMPTY
                },
                server,
            )
            .expect("create tun device");
        client
    };
    const PORT_ID: u8 = 7;
    let tun_port = {
        let (client, server) = fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::PortMarker>()
            .expect("create proxy");
        tun_device
            .add_port(
                fidl_fuchsia_net_tun::DevicePortConfig {
                    base: Some(fidl_fuchsia_net_tun::BasePortConfig {
                        id: Some(PORT_ID),
                        rx_types: Some(vec![fidl_fuchsia_hardware_network::FrameType::Ethernet]),
                        tx_types: Some(vec![fidl_fuchsia_hardware_network::FrameTypeSupport {
                            type_: fidl_fuchsia_hardware_network::FrameType::Ethernet,
                            features: 0,
                            supported_flags: fidl_fuchsia_hardware_network::TxFlags::empty(),
                        }]),
                        ..fidl_fuchsia_net_tun::BasePortConfig::EMPTY
                    }),
                    online: Some(true),
                    mac: Some(fidl_fuchsia_net::MacAddress { octets }),
                    ..fidl_fuchsia_net_tun::DevicePortConfig::EMPTY
                },
                server,
            )
            .expect("add tun port");
        client
    };
    let virtualization_interface = {
        let virtualization_ctl =
            client::connect_to_protocol::<fidl_fuchsia_net_virtualization::ControlMarker>()
                .expect("connect virtualization control");
        let virtualization_network = {
            let (client, server) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_net_virtualization::NetworkMarker>()
                    .expect("create proxy");
            let () = virtualization_ctl
                .create_network(
                    &mut fidl_fuchsia_net_virtualization::Config::Bridged(
                        fidl_fuchsia_net_virtualization::Bridged::EMPTY,
                    ),
                    server,
                )
                .expect("create network");
            client
        };
        let port = {
            let (client, server) =
                fidl::endpoints::create_endpoints::<fidl_fuchsia_hardware_network::PortMarker>()
                    .expect("create endpoints");
            let () = tun_port.get_port(server).expect("get port");
            client
        };
        let virtualization_interface = {
            let (client, server) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_net_virtualization::InterfaceMarker>()
                    .expect("create proxy");
            let () = virtualization_network.add_port(port, server).expect("add device");
            client
        };
        virtualization_interface
    };

    let mut on_closed = virtualization_interface.on_closed().fuse();
    loop {
        futures::select! {
            state = tun_port.watch_state() => {
                let fidl_fuchsia_net_tun::InternalState { mac: _, has_session, .. } =
                    state.expect("watch state");
                if has_session.expect("session") {
                    break;
                }
            },
            signals = on_closed => {
                let signals = signals.expect("closed signals");
                panic!("virtualization interface closed with {:?}", signals);
            }
        }
    }

    // Send a message to the server and expect it to be echoed back.

    let bus = open_bus(&ep_name).expect("open bus");
    let (success, absent) = bus
        .wait_for_clients(&mut vec![server_name.as_str()].drain(..), 0)
        .await
        .expect("wait for clients");
    assert!(success);
    assert_eq!(absent, None);

    let () = fake_ep.write(NETSTACK_STRING.as_bytes()).await.expect("write failed");

    let () = tun_device
        .write_frame(fidl_fuchsia_net_tun::Frame {
            frame_type: Some(fidl_fuchsia_hardware_network::FrameType::Ethernet),
            data: Some(VIRTUALIZATION_STRING.as_bytes().to_owned()),
            port: Some(PORT_ID),
            ..fidl_fuchsia_net_tun::Frame::EMPTY
        })
        .await
        .expect("write frame (transport)")
        .map_err(zx::Status::from_raw)
        .expect("write frame (application)");

    let netstack_fut = async {
        let mut netstack_seen = false;
        let mut virtualization_seen = false;
        while !(netstack_seen && virtualization_seen) {
            let (data, dropped_frames) = fake_ep.read().await.expect("read failed");
            assert_eq!(dropped_frames, 0);
            match str::from_utf8(&data).expect("invalid utf8") {
                NETSTACK_STRING => netstack_seen = true,
                VIRTUALIZATION_STRING => virtualization_seen = true,
                reply => panic!("Server reply ({}) did not match client messages", reply),
            }
        }
    };

    let virtualization_fut = async {
        let mut netstack_seen = false;
        let mut virtualization_seen = false;
        while !(netstack_seen && virtualization_seen) {
            let fidl_fuchsia_net_tun::Frame { frame_type, data, port, .. } = tun_device
                .read_frame()
                .await
                .expect("read frame (transport)")
                .map_err(zx::Status::from_raw)
                .expect("read frame (application)");
            assert_eq!(frame_type, Some(fidl_fuchsia_hardware_network::FrameType::Ethernet));
            assert_eq!(port, Some(PORT_ID));
            match str::from_utf8(data.unwrap().as_slice()).expect("invalid utf8") {
                NETSTACK_STRING => netstack_seen = true,
                VIRTUALIZATION_STRING => virtualization_seen = true,
                reply => panic!("Server reply ({}) did not match client messages", reply),
            }
        }
    };

    futures::select! {
        ((), ()) = futures::future::join(netstack_fut, virtualization_fut).fuse() => {},
        signals = on_closed => {
            let signals = signals.expect("closed signals");
            panic!("virtualization interface closed with {:?}", signals);
        }
    }
}

async fn run_echo_server_ethernet(
    ep_name: String,
    eth_dev: fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>,
) -> Result<(), Error> {
    // Create an EthernetClient to wrap around the Endpoint's ethernet device.
    let vmo = zx::Vmo::create(256 * ethernet::DEFAULT_BUFFER_SIZE as u64)?;

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

    // Before connecting to the message bus to notify the client of the server's existence, poll
    // for events.  Buffers will not be allocated until polling is performed so this ensures that
    // there will be buffers to receive the client's message.
    loop {
        match futures::poll!(eth_events.try_next()) {
            Poll::Pending => break,
            Poll::Ready(result) => match result {
                Ok(result) => match result {
                    Some(_) => continue,
                    None => panic!("event stream produced empty event"),
                },
                Err(e) => panic!("event stream returned an error: {}", e),
            },
        }
    }

    // get on bus to unlock mock_guest part of test
    let _bus = open_bus(&ep_name)?;

    // Start listening for the server's response to be
    // transmitted to the guest.
    let () = eth_client.tx_listen_start().await?;

    let mut netstack_echo_seen = false;
    let mut virtualization_echo_seen = false;
    while let Some(event) = eth_events.try_next().await? {
        match event {
            ethernet::Event::Receive(rx, flags) => {
                let mut data = [0u8; 100];
                let sz = rx.read(&mut data);
                if flags.contains(ethernet::EthernetQueueFlags::TX_ECHO) {
                    match str::from_utf8(&data[0..sz]).expect("failed to parse string") {
                        NETSTACK_STRING => {
                            if netstack_echo_seen {
                                continue;
                            }
                            netstack_echo_seen = true;
                        }
                        VIRTUALIZATION_STRING => {
                            if virtualization_echo_seen {
                                continue;
                            }
                            virtualization_echo_seen = true;
                        }
                        message => panic!(
                            "Client message ('{}' sz={}) did not match expectation",
                            message, sz
                        ),
                    }
                }

                let () = eth_client.send(&data[0..sz]);

                if netstack_echo_seen && virtualization_echo_seen {
                    // The mock guest will not send anything to the server
                    // beyond its initial requests. After the server has echoed
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

async fn run_echo_server(ep_name: String) -> Result<(), Error> {
    // Get the Endpoint that was created in the server's environment.
    let netctx = client::connect_to_protocol::<NetworkContextMarker>()?;
    let (epm, epm_server_end) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
    netctx.get_endpoint_manager(epm_server_end)?;

    let ep = epm.get_endpoint(&ep_name).await?;

    let ep = match ep {
        Some(ep) => ep.into_proxy()?,
        None => return Err(format_err!("Can't find endpoint {}", &ep_name)),
    };

    match ep.get_device().await? {
        fidl_fuchsia_netemul_network::DeviceConnection::Ethernet(e) => {
            run_echo_server_ethernet(ep_name, e).await
        }
        fidl_fuchsia_netemul_network::DeviceConnection::NetworkDevice(netdevice) => {
            panic!(
                "got unexpected NetworkDevice {:?}; expected to have been configured with Ethernet",
                netdevice
            );
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let () = fuchsia_syslog::init().context("cannot init logger")?;
    log::info!("starting...");

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
        .await;
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
