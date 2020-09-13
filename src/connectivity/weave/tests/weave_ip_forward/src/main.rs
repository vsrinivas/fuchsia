// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{format_err, Context, Error},
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_net_stack::StackMarker,
    fidl_fuchsia_netemul_sync::{BusMarker, BusProxy, Event, SyncManagerMarker},
    fidl_fuchsia_netstack::{NetstackMarker, RouteTableEntry2, RouteTableTransactionMarker},
    fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::TryStreamExt,
    net_declare::fidl_ip,
    prettytable::{cell, format, row, Table},
    std::convert::TryFrom,
    std::io::{Read, Write},
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
        let busm = client::connect_to_service::<SyncManagerMarker>()
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
        self.bus.publish(Event { code: Some(code), message: None, arguments: None })?;
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
    name: &str,
    intf: &Vec<fidl_fuchsia_net_stack::InterfaceInfo>,
) -> Result<u64, Error> {
    let res = intf
        .iter()
        .find_map(
            |interface| if interface.properties.name == name { Some(interface.id) } else { None },
        )
        .ok_or(anyhow::format_err!("failed to find {}", name))?;
    Ok(res)
}

async fn add_route_table_entry(
    dest: fidl_fuchsia_net::IpAddress,
    netmask: fidl_fuchsia_net::IpAddress,
    nicid: u64,
    route_proxy: &fidl_fuchsia_netstack::RouteTableTransactionProxy,
) -> Result<(), Error> {
    let mut entry = RouteTableEntry2 {
        destination: dest,
        netmask: netmask,
        gateway: None,
        nicid: u32::try_from(nicid)?,
        metric: ENTRY_METRICS,
    };
    let zx_status = route_proxy
        .add_route(&mut entry)
        .await
        .with_context(|| format!("error in route_proxy.add_route {:?}", entry))?;
    if zx_status != 0 {
        return Err(format_err!("error in route_proxy.add_route, zx_status {}", zx_status));
    }
    Ok(())
}

async fn run_fuchsia_node() -> Result<(), Error> {
    let stack =
        client::connect_to_service::<StackMarker>().context("failed to connect to netstack")?;
    let netstack =
        connect_to_service::<NetstackMarker>().context("failed to connect to netstack")?;

    let intf = stack.list_interfaces().await.context("getting interfaces")?;
    let wlan_if_id = get_interface_id("wlan-f-ep", &intf)?;
    let wpan_if_id = get_interface_id("wpan-f-ep", &intf)?;
    let weave_if_id = get_interface_id("weave-f-ep", &intf)?;

    fx_log_info!("wlan intf: {:?}", wlan_if_id);
    fx_log_info!("wpan intf: {:?}", wpan_if_id);
    fx_log_info!("weave intf: {:?}", weave_if_id);

    let (client_end, server_end) =
        create_endpoints::<RouteTableTransactionMarker>().context("error creating endpoint")?;
    let zx_status = netstack
        .start_route_table_transaction(server_end)
        .await
        .context("error start_route_table_transaction")?;
    if zx_status != 0 {
        return Err(format_err!(
            "error in netstack.start_route_table_transaction, zx_status {}",
            zx_status
        ));
    }

    let route_proxy = client_end.into_proxy().context("error route_proxy.into_proxy")?;

    // routing rules for weave tun
    let () = add_route_table_entry(
        fidl_ip!(fdce:da10:7616:6:6616:6600:4734:b051),
        fidl_ip!(ffff: ffff: ffff: ffff: ffff: ffff: ffff: ffff),
        weave_if_id,
        &route_proxy,
    )
    .await
    .context("adding routing table entry for weave tun")?;
    let () = add_route_table_entry(
        fidl_ip!(fdce:da10:7616::),
        fidl_ip!(ffff:ffff:ffff::),
        weave_if_id,
        &route_proxy,
    )
    .await
    .context("adding routing table entry for weave tun")?;

    // routing rules for wpan
    let () = add_route_table_entry(
        fidl_ip!(fdce:da10:7616:6::),
        fidl_ip!(ffff:ffff:ffff:ffff::),
        wpan_if_id,
        &route_proxy,
    )
    .await
    .context("adding routing table entry for wpan")?;
    let () = add_route_table_entry(
        fidl_ip!(fdd3:b786:54dc::),
        fidl_ip!(ffff:ffff:ffff:ffff::),
        wpan_if_id,
        &route_proxy,
    )
    .await
    .context("adding routing table entry for wpan")?;

    // routing rules for wlan
    let () = add_route_table_entry(
        fidl_ip!(fdce:da10:7616:1::),
        fidl_ip!(ffff:ffff:ffff:ffff::),
        wlan_if_id,
        &route_proxy,
    )
    .await
    .context("adding routing table entry for wlan")?;

    fx_log_info!("successfully added entries to route table");

    let route_table =
        netstack.get_route_table2().await.context("error retrieving routing table")?;

    let mut t = Table::new();
    t.set_format(format::FormatBuilder::new().padding(2, 2).build());

    t.set_titles(row!["Destination", "Netmask", "Gateway", "NICID", "Metric"]);
    for entry in route_table {
        let route = fidl_fuchsia_netstack_ext::RouteTableEntry2::from(entry);
        let gateway_str = match route.gateway {
            None => "-".to_string(),
            Some(g) => format!("{}", g),
        };
        t.add_row(row![route.destination, route.netmask, gateway_str, route.nicid, route.metric]);
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

    let mut fut_vec = Vec::new();

    for connect_addr in connect_addrs {
        fx_log_info!("connecting to {}...", connect_addr);
        fut_vec.push(get_test_fut_client(connect_addr));
        fx_log_info!("succeed");
    }

    for fut in fut_vec {
        fut.await?;
    }

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
