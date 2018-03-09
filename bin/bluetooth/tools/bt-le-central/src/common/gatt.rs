// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


use async;

use common::error::{BluetoothError, BluetoothFidlError};
use common::gatt_types::Service;
use failure::Error;
use fidl::endpoints2::ServerEnd;
use futures::{Future, FutureExt, Never, Stream, StreamExt, future};
use futures::channel::mpsc::channel;
use futures::future::Either::{Left, Right};
use futures::future::FutureResult;
use gatt::{Characteristic as FidlCharacteristic, ClientMarker, ClientProxy, RemoteServiceMarker,
           RemoteServiceProxy, ServiceInfo};

use parking_lot::RwLock;
use std::io::{self, Read, Write};
use std::string::String;
use std::sync::Arc;
use std::thread;
use zx;

type GattClientPtr = Arc<RwLock<GattClient>>;

struct GattClient {
    proxy: ClientProxy,

    // Services discovered on this client.
    services: Vec<Service>,

    // The index of the currently connected service, if any.
    active_index: usize,

    // FIDL proxy to the currently connected service, if any.
    active_proxy: Option<RemoteServiceProxy>,
}

impl GattClient {
    fn new(proxy: ClientProxy) -> GattClientPtr {
        Arc::new(RwLock::new(GattClient {
            proxy: proxy,
            services: vec![],
            active_index: 0,
            active_proxy: None,
        }))
    }

    fn set_services(&mut self, services: Vec<ServiceInfo>) {
        self.services.clear();
        self.services.reserve(services.len());

        self.services = services
            .into_iter()
            .map(|info| Service::new(info))
            .collect();
        self.display_services();
    }

    fn active_service(&mut self) -> Option<&mut Service> {
        self.services.get_mut(self.active_index)
    }

    fn on_discover_characteristics(&mut self, chrcs: Vec<FidlCharacteristic>) {
        if let Some(ref mut svc) = self.active_service() {
            svc.set_characteristics(chrcs);
            println!("{}", svc);
        }
    }

    fn display_services(&self) {
        let mut i: i32 = 0;
        for svc in &self.services {
            println!("  {}: {}", i, &svc);
            i += 1;
        }
    }
}

// Starts the GATT REPL. This first requests a list of remote services and resolves the
// returned future with an error if no services are found.
pub fn start_gatt_loop(proxy: ClientProxy) -> impl Future<Item = (), Error = Error> {
    let client = GattClient::new(proxy);

    // |client| is moved into the AndThen closure while |client2| is borrowed. |client2|
    // is later moved into the ForEach closure below.
    let client2 = client.clone();

    println!("  discovering services...");
    let get_services = client2
        .read()
        .proxy
        .list_services(None)
        .map_err(|e| {
            println!("failed to list services: {}", e);
            BluetoothError::new().into()
        })
        .and_then(move |(status, services)| match status.error {
            None => {
                client.write().set_services(services);
                Ok(())
            }
            Some(e) => {
                let err = BluetoothFidlError::new(*e).into();
                println!("failed to list services: {}", err);
                Err(err)
            }
        });

    get_services.and_then(|_| {
        stdin_stream()
            .map_err(|e| {
                println!("stream error: {:?}", e);
                BluetoothError::new().into()
            })
            .for_each(move |cmd| if cmd == "exit" {
                Left(future::err(BluetoothError::new().into()))
            } else {
                Right(handle_cmd(cmd, client2.clone()).and_then(|_| {
                    print!("> ");
                    io::stdout().flush().unwrap();
                    Ok(())
                }))
            })
            .and_then(|_| Ok(()))
    })
}

// Discover the characteristics of |client|'s currently connected service and
// cache them. |client.service_proxy| MUST be valid.
fn discover_characteristics(client: GattClientPtr) -> impl Future<Item = (), Error = Error> {
    let client2 = client.clone();
    client
        .read()
        .active_proxy
        .as_ref()
        .unwrap()
        .discover_characteristics()
        .map_err(|_| {
            println!("Failed to send message");
            BluetoothError::new().into()
        })
        .and_then(move |(status, chrcs)| match status.error {
            Some(e) => {
                println!(
                    "Failed to read characteristics: {}",
                    BluetoothFidlError::new(*e)
                );
                Ok(())
            }
            None => {
                client2.write().on_discover_characteristics(chrcs);
                Ok(())
            }
        })
}

// ===== REPL =====

fn do_help() -> FutureResult<(), Error> {
    println!("Commands:");
    println!("    help                       Print this help message");
    println!("    list                       List discovered services");
    println!("    connect <index>            Connect to a service");
    println!("    exit                       Quit and disconnect the peripheral");

    future::ok(())
}

fn do_list(args: Vec<&str>, client: GattClientPtr) -> FutureResult<(), Error> {
    if !args.is_empty() {
        println!("list: expected 0 arguments");
    } else {
        client.read().display_services();
    }

    future::ok(())
}

fn create_remote_service_pair()
    -> Result<(RemoteServiceProxy, ServerEnd<RemoteServiceMarker>), Error>
{
    let (chan_local, chan_remote) = zx::Channel::create()?;
    let local = async::Channel::from_channel(chan_local)?;
    let server_end = ServerEnd::<RemoteServiceMarker>::new(chan_remote);
    let proxy = RemoteServiceProxy::new(local);

    Ok((proxy, server_end))
}

pub fn create_client_pair() -> Result<(ClientProxy, ServerEnd<ClientMarker>), Error> {
    let (chan_local, chan_remote) = zx::Channel::create()?;
    let local = async::Channel::from_channel(chan_local)?;
    let server_end = ServerEnd::<ClientMarker>::new(chan_remote);
    let proxy = ClientProxy::new(local);

    Ok((proxy, server_end))
}

fn do_connect(args: Vec<&str>, client: GattClientPtr) -> impl Future<Item = (), Error = Error> {
    if args.len() != 1 {
        println!("usage: connect <index>");
        return Left(future::ok(()));
    }

    let index: usize = match args[0].parse() {
        Err(_) => {
            println!("invalid index: {}", args[0]);
            return Left(future::ok(()));
        }
        Ok(i) => i,
    };

    let svc_id = match client.read().services.get(index) {
        None => {
            println!("index out of bounds! ({})", index);
            return Left(future::ok(()));
        }
        Some(s) => s.info.id,
    };

    // Initilize the remote service proxy.
    match create_remote_service_pair() {
        Err(_) => {
            println!("Failed to connect to remote service");
            Left(future::err(BluetoothError::new().into()))
        }
        Ok((proxy, mut server)) => {
            if let Err(_) = client.read().proxy.connect_to_service(svc_id, &mut server) {
                println!("Failed to connect to remote service");
                return Left(future::err(BluetoothError::new().into()));
            }
            client.write().active_index = index;
            client.write().active_proxy = Some(proxy);
            Right(discover_characteristics(client))
        }
    }

}

// Processes |cmd| and returns its result.
// TODO(armansito): Use clap for fancier command processing.
fn handle_cmd(line: String, client: GattClientPtr) -> impl Future<Item = (), Error = Error> {
    let mut components = line.trim().split_whitespace();
    let cmd = components.next();
    let args = components.collect();

    match cmd {
        Some("help") => Left(do_help()),
        Some("list") => Left(do_list(args, client)),
        Some("connect") => Right(do_connect(args, client)),
        Some(cmd) => {
            eprintln!("Unknown command: {}", cmd);
            Left(future::ok(()))
        }
        None => Left(future::ok(())),
    }
}

fn stdin_stream() -> Box<Stream<Item = String, Error = Never> + Send> {
    let (mut sender, receiver) = channel(512);
    thread::spawn(move || -> Result<(), Error> {
        print!("> ");
        io::stdout().flush()?;
        let input = io::stdin();

        // TODO(armansito): TODO: support UTF-8 chars.
        let mut buf: Vec<u8> = vec![];
        for b in input.bytes() {
            if let Ok(byte) = b {
                let c = byte as char;

                // Display the typed character
                print!("{}", c);
                io::stdout().flush()?;

                if c == '\n' {
                    let line = String::from_utf8(buf).unwrap();
                    buf = vec![];
                    sender.try_send(line)?;
                } else {
                    buf.push(byte);
                }
            }
            io::stdout().flush()?;
        }

        Ok(())
    });
    Box::new(receiver)
}
