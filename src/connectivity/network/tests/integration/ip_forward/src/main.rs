// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_net_stack::StackMarker,
    fidl_fuchsia_netemul_sync::{BusMarker, BusProxy, Event, SyncManagerMarker},
    fuchsia_async::{self as fasync},
    fuchsia_component::client,
    futures::TryStreamExt,
    std::io::{Read, Write},
    std::net::{TcpListener, TcpStream},
    structopt::StructOpt,
};

const BUS_NAME: &str = "test-bus";
const SERVER_NAME: &str = "server";
const ROUTER_NAME: &str = "router";
const CLIENT_NAME: &str = "client";
const HELLO_MSG_REQ: &str = "Hello World from Client!";
const HELLO_MSG_RSP: &str = "Hello World from Server!";
const SERVER_DONE: i32 = 1;

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

    pub async fn wait_for_event(&self, code: i32) -> Result<(), Error> {
        let mut stream = self.bus.take_event_stream().try_filter_map(|event| match event {
            fidl_fuchsia_netemul_sync::BusEvent::OnBusData { data } => match data.code {
                Some(rcv_code) => {
                    if rcv_code == code {
                        futures::future::ok(Some(()))
                    } else {
                        futures::future::ok(None)
                    }
                }
                None => futures::future::ok(None),
            },
            _ => futures::future::ok(None),
        });
        stream.try_next().await?;
        Ok(())
    }
}

async fn run_router() -> Result<(), Error> {
    let stack =
        client::connect_to_service::<StackMarker>().context("failed to connect to netstack")?;
    let () = stack.enable_ip_forwarding().await.context("failed to enable ip forwarding")?;

    let bus = BusConnection::new(ROUTER_NAME)?;
    log::info!("Waiting for server to finish...");
    let () = bus.wait_for_event(SERVER_DONE).await?;
    Ok(())
}

async fn run_server(listen_addr: String) -> Result<(), Error> {
    let listener = TcpListener::bind(listen_addr).context("Can't bind to address")?;
    log::info!("Waiting for connections...");
    let bus = BusConnection::new(SERVER_NAME)?;

    let (mut stream, remote) = listener.accept().context("Accept failed")?;
    log::info!("Accepted connection from {}", remote);
    let mut buffer = [0; 512];
    let rd = stream.read(&mut buffer).context("read failed")?;

    let req = String::from_utf8(buffer[0..rd].to_vec()).context("not a valid utf8")?;
    if req != HELLO_MSG_REQ {
        return Err(format_err!("Got unexpected request from client: {}", req));
    }
    log::info!("Got request {}", req);
    stream.write(HELLO_MSG_RSP.as_bytes()).context("write failed")?;
    stream.flush().context("flush failed")?;

    let () = bus.publish_code(SERVER_DONE)?;
    Ok(())
}

async fn run_client(connect_addr: String) -> Result<(), Error> {
    let mut bus = BusConnection::new(CLIENT_NAME)?;
    log::info!("Waiting for router to start...");
    let () = bus.wait_for_client(ROUTER_NAME).await?;
    log::info!("Waiting for server to start...");
    let () = bus.wait_for_client(SERVER_NAME).await?;

    log::info!("Connecting to server...");
    let mut stream = TcpStream::connect(connect_addr).context("Tcp connection failed")?;
    let request = HELLO_MSG_REQ.as_bytes();
    stream.write(request)?;
    stream.flush()?;

    let mut buffer = [0; 512];
    let rd = stream.read(&mut buffer)?;
    let rsp = String::from_utf8(buffer[0..rd].to_vec()).context("not a valid utf8")?;
    log::info!("Got response {}", rsp);
    if rsp != HELLO_MSG_RSP {
        return Err(format_err!("Got unexpected echo from server: {}", rsp));
    }
    Ok(())
}

#[derive(StructOpt, Debug)]
enum Opt {
    #[structopt(name = "server")]
    Server { listen_addr: String },
    #[structopt(name = "router")]
    Router,
    #[structopt(name = "client")]
    Client { connect_addr: String },
}

fn main() -> Result<(), Error> {
    let () = fuchsia_syslog::init().context("cannot init logger")?;

    let opt = Opt::from_args();
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    executor.run_singlethreaded(async {
        match opt {
            Opt::Server { listen_addr } => run_server(listen_addr).await,
            Opt::Router => run_router().await,
            Opt::Client { connect_addr } => run_client(connect_addr).await,
        }
    })
}
