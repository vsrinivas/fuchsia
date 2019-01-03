// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use {
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_netemul_bus::{BusManagerMarker, BusMarker, BusProxy},
    fuchsia_app::client,
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_zircon::DurationNum,
    futures::TryStreamExt,
    std::io::{Read, Write},
    std::net::{SocketAddr, TcpListener, TcpStream},
    structopt::StructOpt,
};

const BUS_NAME: &str = "test-bus";
const SERVER_NAME: &str = "server";
const CLIENT_NAME: &str = "client";
const HELLO_MSG_REQ: &str = "Hello World from Client!";
const HELLO_MSG_RSP: &str = "Hello World from Server!";
const SERVER_IP: &str = "192.168.0.1";
const PORT: i32 = 8080;
const TIMEOUT_SECS: i64 = 5;

pub struct BusConnection {
    bus: BusProxy,
}

impl BusConnection {
    pub fn new(client: &str) -> Result<BusConnection, Error> {
        let busm =
            client::connect_to_service::<BusManagerMarker>().context("BusManager not available")?;
        let (bus, busch) = fidl::endpoints::create_proxy::<BusMarker>()?;
        busm.subscribe(BUS_NAME, client, busch)?;
        Ok(BusConnection { bus })
    }

    pub async fn wait_for_client(&mut self, expect: &'static str) -> Result<(), Error> {
        let clients = await!(self.bus.get_clients())?;
        if clients.contains(&String::from(expect)) {
            return Ok(());
        }

        let mut stream = self
            .bus
            .take_event_stream()
            .try_filter_map(|event| match event {
                fidl_fuchsia_netemul_bus::BusEvent::OnClientAttached { client } => {
                    if client == expect {
                        futures::future::ok(Some(()))
                    } else {
                        futures::future::ok(None)
                    }
                }
                _ => futures::future::ok(None),
            });

        await!(stream.try_next())?;
        Ok(())
    }
}

async fn run_server() -> Result<(), Error> {
    let listener =
        TcpListener::bind(&format!("{}:{}", SERVER_IP, PORT)).context("Can't bind to address")?;
    println!("Waiting for connections...");

    let _bus = BusConnection::new(SERVER_NAME)?;

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

async fn run_client() -> Result<(), Error> {
    println!("Waiting for server...");
    let mut bus = BusConnection::new(CLIENT_NAME)?;
    let () = await!(bus.wait_for_client(SERVER_NAME))?;
    println!("Connecting to server...");
    let addr: SocketAddr = format!("{}:{}", SERVER_IP, PORT).parse()?;
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

#[derive(StructOpt, Debug)]
struct Opt {
    #[structopt(short = "c")]
    is_child: bool,
}

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    executor.run_singlethreaded(
        async {
            if opt.is_child {
                await!(
                    run_client().on_timeout(TIMEOUT_SECS.seconds().after_now(), || Err(
                        format_err!("client timed out")
                    ))
                )
            } else {
                await!(
                    run_server().on_timeout(TIMEOUT_SECS.seconds().after_now(), || Err(
                        format_err!("server timed out")
                    ))
                )
            }
        },
    )
}
