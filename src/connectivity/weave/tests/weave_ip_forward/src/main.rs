// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_net_interfaces as fnet_interfaces,
    fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext,
    fidl_fuchsia_net_stack::StackMarker,
    fidl_fuchsia_netemul_sync::{BusMarker, BusProxy, Event, SyncManagerMarker},
    fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::TryStreamExt as _,
    net_declare::fidl_subnet,
    prettytable::{cell, format, row, Table},
    std::collections::HashMap,
    std::io::{Read as _, Write as _},
    std::net::{SocketAddr, TcpListener, TcpStream},
    structopt::StructOpt,
};

const BUS_NAME: &str = "test-bus";
const WEAVE_NODE_NAME: &str = "weave-node";
const FUCHSIA_NODE_NAME: &str = "fuchsia-node";
const WLAN_NODE_NAME: &str = "wlan-node";
const WPAN_NODE_NAME: &str = "wpan-node";
const WLAN_NODE_1_NAME: &str = "wlan-node-1";
const WPAN_SERVER_NODE_NAME: &str = "wpan-server-node";
const HELLO_MSG_REQ: &str = "Hello World from TCP Client!";
const HELLO_MSG_RSP: &str = "Hello World from TCP Server!";
const WEAVE_SERVER_NODE_DONE: i32 = 1;
const WPAN_SERVER_NODE_DONE: i32 = 2;
const ENTRY_METRICS: u32 = 256;

pub struct BusConnection {
    bus: BusProxy,
}

impl BusConnection {
    pub fn new(client: &str) -> Result<BusConnection, Error> {
        let busm = client::connect_to_protocol::<SyncManagerMarker>()
            .context("SyncManager not available")?;
        let (bus, busch) = fidl::endpoints::create_proxy::<BusMarker>()?;
        busm.bus_subscribe(BUS_NAME, client, busch)?;
        Ok(BusConnection { bus })
    }

    pub async fn wait_for_client(&mut self, expect: &'static str) -> Result<(), Error> {
        let _ = self.bus.wait_for_clients(&mut vec![expect].drain(..), 0).await?;
        Ok(())
    }

    pub fn publish_code(&self, code: i32) -> Result<(), Error> {
        self.bus.publish(Event {
            code: Some(code),
            message: None,
            arguments: None,
            ..Event::EMPTY
        })?;
        Ok(())
    }

    pub async fn wait_for_event(&self, mut code_vec: Vec<i32>) -> Result<(), Error> {
        let mut stream = self.bus.take_event_stream();
        loop {
            match stream.try_next().await? {
                Some(fidl_fuchsia_netemul_sync::BusEvent::OnBusData { data }) => match data.code {
                    Some(rcv_code) => {
                        if code_vec.contains(&rcv_code) {
                            code_vec.retain(|&x| x != rcv_code);
                        } else {
                            fx_log_err!("unexpected rcv_code: {:?}", rcv_code);
                            return Err(format_err!("unexpected rcv_code in wait_for_event"));
                        }
                    }
                    None => {
                        return Err(format_err!("data.code contains no event"));
                    }
                },
                _ => {}
            };
            if code_vec.is_empty() {
                break;
            }
        }
        Ok(())
    }
}

fn get_interface_id(
    want_name: &str,
    intf: &HashMap<u64, fnet_interfaces_ext::Properties>,
) -> Result<u64, Error> {
    intf.values()
        .find_map(
            |fidl_fuchsia_net_interfaces_ext::Properties {
                 id,
                 name,
                 device_class: _,
                 online: _,
                 addresses: _,
                 has_default_ipv4_route: _,
                 has_default_ipv6_route: _,
             }| if name == want_name { Some(*id) } else { None },
        )
        .ok_or(anyhow::format_err!("failed to find {}", want_name))
}

async fn add_route_table_entry(
    stack_proxy: &fidl_fuchsia_net_stack::StackProxy,
    subnet: fidl_fuchsia_net::Subnet,
    nicid: u64,
) -> Result<(), Error> {
    let mut entry = fidl_fuchsia_net_stack::ForwardingEntry {
        subnet,
        device_id: nicid,
        next_hop: None,
        metric: ENTRY_METRICS,
    };
    stack_proxy
        .add_forwarding_entry(&mut entry)
        .await
        .with_context(|| format!("failed to send add fowrarding entry {:?}", entry))?
        .map_err(|e: fidl_fuchsia_net_stack::Error| {
            format_err!("failed to add fowrarding entry {:?}: {:?}", entry, e)
        })
}

async fn run_fuchsia_node() -> Result<(), Error> {
    let interface_state =
        fuchsia_component::client::connect_to_protocol::<fnet_interfaces::StateMarker>()
            .context("failed to connect to interfaces/State")?;
    let stack =
        client::connect_to_protocol::<StackMarker>().context("failed to connect to netstack")?;

    let stream = fnet_interfaces_ext::event_stream_from_state(&interface_state)
        .context("failed to get interface stream")?;
    let intf = fnet_interfaces_ext::existing(stream, HashMap::new())
        .await
        .context("failed to get existing interfaces")?;
    let wlan_if_id = get_interface_id("wlan-f-ep", &intf)?;
    let wpan_if_id = get_interface_id("wpan-f-ep", &intf)?;
    let weave_if_id = get_interface_id("weave-f-ep", &intf)?;

    fx_log_info!("wlan intf: {:?}", wlan_if_id);
    fx_log_info!("wpan intf: {:?}", wpan_if_id);
    fx_log_info!("weave intf: {:?}", weave_if_id);

    // routing rules for weave tun
    let () = add_route_table_entry(
        &stack,
        fidl_subnet!("fdce:da10:7616:6:6616:6600:4734:b051/128"),
        weave_if_id,
    )
    .await
    .context("adding routing table entry for weave tun")?;
    let () = add_route_table_entry(&stack, fidl_subnet!("fdce:da10:7616::/48"), weave_if_id)
        .await
        .context("adding routing table entry for weave tun")?;

    // routing rules for wpan
    let () = add_route_table_entry(&stack, fidl_subnet!("fdce:da10:7616:6::/64"), wpan_if_id)
        .await
        .context("adding routing table entry for wpan")?;
    let () = add_route_table_entry(&stack, fidl_subnet!("fdd3:b786:54dc::/64"), wpan_if_id)
        .await
        .context("adding routing table entry for wpan")?;

    // routing rules for wlan
    let () = add_route_table_entry(&stack, fidl_subnet!("fdce:da10:7616:1::/64"), wlan_if_id)
        .await
        .context("adding routing table entry for wlan")?;

    fx_log_info!("successfully added entries to route table");

    let route_table =
        stack.get_forwarding_table().await.context("error retrieving routing table")?;

    let mut t = Table::new();
    t.set_format(format::FormatBuilder::new().padding(2, 2).build());

    t.set_titles(row!["Destination", "Gateway", "NICID", "Metric"]);
    for entry in route_table {
        let fidl_fuchsia_net_stack_ext::ForwardingEntry { subnet, device_id, next_hop, metric } =
            entry.into();
        let next_hop = next_hop.map(|next_hop| next_hop.to_string());
        let next_hop = next_hop.as_ref().map_or("-", |s| s.as_str());
        t.add_row(row![subnet, next_hop, device_id, metric]);
    }

    fx_log_info!("{}", t.printstd());

    let () = stack.enable_ip_forwarding().await.context("failed to enable ip forwarding")?;

    let bus = BusConnection::new(FUCHSIA_NODE_NAME)?;
    fx_log_info!("waiting for server to finish...");
    let () = bus.wait_for_event(vec![WEAVE_SERVER_NODE_DONE, WPAN_SERVER_NODE_DONE]).await?;
    fx_log_info!("fuchsia node exited");
    Ok(())
}

async fn handle_request(mut stream: TcpStream, remote: SocketAddr) -> Result<(), Error> {
    fx_log_info!("accepted connection from {}", remote);

    let mut buffer = [0; 512];
    let rd = stream.read(&mut buffer).context("read failed")?;

    let req = String::from_utf8(buffer[0..rd].to_vec()).context("not a valid utf8")?;
    if req != HELLO_MSG_REQ {
        return Err(format_err!("Got unexpected request from client: {}", req));
    }
    fx_log_info!("Got request {}", req);
    let bytes_written = stream.write(HELLO_MSG_RSP.as_bytes()).context("write failed")?;
    if bytes_written != HELLO_MSG_RSP.len() {
        return Err(format_err!("response not fully written to TCP stream: {}", bytes_written));
    }
    stream.flush().context("flush failed")
}

async fn run_server_node(
    listen_addrs: Vec<String>,
    conn_nums: Vec<u32>,
    node_name: &str,
    node_code: i32,
) -> Result<(), Error> {
    let mut listener_vec = Vec::new();
    for listen_addr in listen_addrs {
        listener_vec.push(TcpListener::bind(listen_addr).context("Can't bind to address")?);
    }
    fx_log_info!("server {} for connections...", node_name);
    let bus = BusConnection::new(node_name)?;

    for listener_idx in 0..listener_vec.len() {
        let mut handler_futs = Vec::new();
        for _ in 0..conn_nums[listener_idx] {
            let (stream, remote) = listener_vec[listener_idx].accept().unwrap();
            handler_futs.push(handle_request(stream, remote));
        }
        for handler_fut in handler_futs {
            let () = handler_fut.await?;
        }
    }

    let () = bus.publish_code(node_code)?;

    fx_log_info!("server {} exited successfully", node_name);

    Ok(())
}

async fn get_test_fut_client(connect_addr: String) -> Result<(), Error> {
    let mut stream = TcpStream::connect(connect_addr.clone()).context("Tcp connection failed")?;
    let request = HELLO_MSG_REQ.as_bytes();
    stream.write(request)?;
    stream.flush()?;

    let mut buffer = [0; 512];
    let rd = stream.read(&mut buffer)?;
    let rsp = String::from_utf8(buffer[0..rd].to_vec()).context("not a valid utf8")?;
    fx_log_info!("got response {} from {}", rsp, connect_addr);
    if rsp != HELLO_MSG_RSP {
        return Err(format_err!("Got unexpected echo from server: {}", rsp));
    }
    Ok(())
}

async fn run_client_node(
    connect_addrs: Vec<String>,
    node_name: &str,
    server_node_names: Vec<&'static str>,
) -> Result<(), Error> {
    let mut bus = BusConnection::new(node_name)?;
    fx_log_info!("client {} is up and for fuchsia node to start", node_name);
    let () = bus.wait_for_client(FUCHSIA_NODE_NAME).await?;
    for server_node_name in server_node_names {
        fx_log_info!("waiting for server node {} to start...", server_node_name);
        let () = bus.wait_for_client(server_node_name).await?;
    }

    let futs = connect_addrs.into_iter().map(|connect_addr| async move {
        fx_log_info!("connecting to {}...", connect_addr);
        let result = get_test_fut_client(connect_addr.clone()).await;
        match result {
            Ok(()) => fx_log_info!("connected to {}", connect_addr),
            Err(ref e) => fx_log_info!("failed to connect to {}: {}", connect_addr, e),
        };
        result
    });

    let _: Vec<()> = futures::future::try_join_all(futs).await?;

    fx_log_info!("client {} exited", node_name);
    Ok(())
}

#[derive(StructOpt, Debug)]
enum Opt {
    #[structopt(name = "weave-node")]
    WeaveNode { listen_addr_0: String, listen_addr_1: String },
    #[structopt(name = "fuchsia-node")]
    FuchsiaNode,
    #[structopt(name = "wpan-node")]
    WpanNode { connect_addr_0: String, connect_addr_1: String, listen_addr_0: String },
    #[structopt(name = "wlan-node")]
    WlanNode { connect_addr_0: String, connect_addr_1: String, connect_addr_2: String },
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opt = Opt::from_args();

    let node_name_str = match opt {
        Opt::WeaveNode { .. } => "weave_node",
        Opt::FuchsiaNode => "fuchsia_node",
        Opt::WlanNode { .. } => "wlan_node",
        Opt::WpanNode { .. } => "wpan_node",
    };
    fuchsia_syslog::init_with_tags(&[node_name_str])?;

    match opt {
        Opt::WeaveNode { listen_addr_0, listen_addr_1 } => {
            run_server_node(
                vec![listen_addr_0, listen_addr_1],
                vec![2, 2],
                WEAVE_NODE_NAME,
                WEAVE_SERVER_NODE_DONE,
            )
            .await
            .context("Error running weave-node server")?;
            ()
        }
        Opt::FuchsiaNode => {
            run_fuchsia_node().await.context("Error running fuchsia-node")?;
        }
        Opt::WlanNode { connect_addr_0, connect_addr_1, connect_addr_2 } => {
            run_client_node(
                vec![connect_addr_0, connect_addr_1],
                WLAN_NODE_NAME,
                vec![WEAVE_NODE_NAME],
            )
            .await
            .context("Error running wlan-node client")?;
            run_client_node(vec![connect_addr_2], WLAN_NODE_1_NAME, vec![WPAN_SERVER_NODE_NAME])
                .await
                .context("Error running wlan-node client 1")?;
        }
        Opt::WpanNode { connect_addr_0, connect_addr_1, listen_addr_0 } => {
            run_client_node(
                vec![connect_addr_0, connect_addr_1],
                WPAN_NODE_NAME,
                vec![WEAVE_NODE_NAME],
            )
            .await
            .context("Error running wpan-node client")?;
            run_server_node(
                vec![listen_addr_0],
                vec![1],
                WPAN_SERVER_NODE_NAME,
                WPAN_SERVER_NODE_DONE,
            )
            .await
            .context("Error running wpan-node server")?;
        }
    };
    Ok(())
}
