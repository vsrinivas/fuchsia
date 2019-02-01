// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::common::{BusConnection, SERVER_READY},
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_netemul_network::{EndpointManagerMarker, NetworkContextMarker},
    fidl_fuchsia_netstack::{InterfaceConfig, NetstackMarker},
    fuchsia_app::client,
    futures::TryStreamExt,
    std::io::{Read, Write},
    std::net::{SocketAddr, TcpListener, TcpStream},
    std::str::FromStr,
};

const PORT: i32 = 8080;
const HELLO_MSG_REQ: &str = "Hello World from Client!";
const HELLO_MSG_RSP: &str = "Hello World from Server!";

pub struct ChildOptions {
    pub endpoint: String,
    pub ip: String,
    pub connect_ip: Option<String>,
}

fn publish_server_ready() -> Result<(), Error> {
    let bc = BusConnection::new("server")?;
    bc.publish_code(SERVER_READY)
}

fn run_server(ip: &str) -> Result<(), Error> {
    let listener =
        TcpListener::bind(&format!("{}:{}", ip, PORT)).context("Can't bind to address")?;
    println!("Waiting for connections...");
    let () = publish_server_ready()?;
    let (mut stream, remote) = listener.accept().context("Accept failed")?;
    println!("Accepted connection from {}", remote);
    let mut buffer = [0; 512];
    let rd = stream.read(&mut buffer).context("read failed")?;

    let req = String::from_utf8_lossy(&buffer[0..rd]);
    if req != HELLO_MSG_REQ {
        return Err(format_err!("Got unexpected request from client: {}", req));
    }
    println!("Got request {}", req);
    stream
        .write(HELLO_MSG_RSP.as_bytes())
        .context("write failed")?;
    stream.flush().context("flush failed")?;
    Ok(())
}

fn run_client(server_ip: &str) -> Result<(), Error> {
    println!("Connecting to server...");
    let addr = SocketAddr::from_str(&format!("{}:{}", server_ip, PORT))?;
    let mut stream = TcpStream::connect(&addr).context("Tcp connection failed")?;
    let request = HELLO_MSG_REQ.as_bytes();
    stream.write(request)?;
    stream.flush()?;

    let mut buffer = [0; 512];
    let rd = stream.read(&mut buffer)?;
    let rsp = String::from_utf8_lossy(&buffer[0..rd]);
    println!("Got response {}", rsp);
    if rsp != HELLO_MSG_RSP {
        return Err(format_err!("Got unexpected echo from server: {}", rsp));
    }
    Ok(())
}

pub async fn run_child(opt: ChildOptions) -> Result<(), Error> {
    println!("Running child with endpoint '{}'", opt.endpoint);

    // get the network context service:
    let netctx = client::connect_to_service::<NetworkContextMarker>()?;
    // get the endpoint manager
    let (epm, epmch) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
    netctx.get_endpoint_manager(epmch)?;

    // retrieve the created endpoint:
    let ep = await!(epm.get_endpoint(&opt.endpoint))?;
    let ep = ep
        .ok_or_else(|| format_err!("can't find endpoint {}", opt.endpoint))?
        .into_proxy()?;
    // and the ethernet device:
    let eth = await!(ep.get_ethernet_device())?;

    let if_name = format!("eth-{}", opt.endpoint);
    // connect to netstack:
    let netstack = client::connect_to_service::<NetstackMarker>()?;
    let mut cfg = InterfaceConfig {
        name: if_name.to_string(),
        ip_address_config: fidl_fuchsia_netstack_ext::IpAddressConfig::StaticIp(
            opt.ip
                .parse::<fidl_fuchsia_net_ext::Subnet>()
                .expect("must be able to parse ip"),
        )
        .into(),
    };

    let mut if_changed = netstack.take_event_stream().try_filter_map(
        |fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
            let iface = interfaces
                .iter()
                .filter(|iface| iface.name == if_name)
                .next();
            match iface {
                None => futures::future::ok(None),
                Some(a) => futures::future::ok(Some((a.id, a.hwaddr.clone()))),
            }
        },
    );
    let _nicid =
        await!(netstack.add_ethernet_device(&format!("/vdev/{}", opt.endpoint), &mut cfg, eth))
            .context("can't add ethernet device")?;

    let (if_id, hwaddr) = await!(if_changed.try_next())
        .context("wait for interfaces")?
        .ok_or_else(|| format_err!("interface added"))?;

    println!("Found ethernet with id {} {:?}", if_id, hwaddr);

    if let Some(remote) = opt.connect_ip {
        run_client(&remote)
    } else {
        run_server(&opt.ip)
    }
}
