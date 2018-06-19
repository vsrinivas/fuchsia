// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;

use bt::error::Error as BTError;
use common::gatt_types::Service;
use failure::Error;
use fidl::endpoints2;
use fidl_gatt::{Characteristic as FidlCharacteristic, ClientProxy, RemoteServiceEvent,
                RemoteServiceProxy, ServiceInfo};
use futures::channel::mpsc::channel;
use futures::future::Either::{Left, Right};
use futures::future::FutureResult;
use futures::{future, Future, FutureExt, Never, Stream, StreamExt};

use parking_lot::RwLock;
use std::io::{self, Read, Write};
use std::string::String;
use std::sync::Arc;
use std::thread;

macro_rules! left_ok {
    () => {
        Left(future::ok(()))
    };
}

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
            println!("  {}: {}\n", i, &svc);
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
            let err = BTError::new(&format!("failed to list services: {}", e));
            println!("{}", e);
            err.into()
        })
        .and_then(move |(status, services)| match status.error {
            None => {
                client.write().set_services(services);
                Ok(())
            }
            Some(e) => {
                let err = BTError::from(*e).into();
                println!("failed to list services: {}", err);
                Err(err)
            }
        });

    get_services.and_then(|_| {
        stdin_stream()
            .map_err(|e| BTError::new(&format!("stream error: {:?}", e)).into())
            .for_each(move |cmd| {
                if cmd == "exit" {
                    Left(future::err(BTError::new("exited").into()))
                } else {
                    Right(
                        handle_cmd(cmd, client2.clone())
                            .map_err(|e| {
                                println!("Error: {}", e);
                                e
                            })
                            .and_then(|_| {
                                print!("> ");
                                io::stdout().flush().unwrap();
                                Ok(())
                            }),
                    )
                }
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
        .map_err(|_| BTError::new("Failed to send message").into())
        .and_then(move |(status, chrcs)| match status.error {
            Some(e) => {
                println!("Failed to read characteristics: {}", BTError::from(*e));
                Ok(())
            }
            None => {
                let event_stream = client2
                    .read()
                    .active_proxy
                    .as_ref()
                    .unwrap()
                    .take_event_stream();
                async::spawn(
                    event_stream
                        .for_each(move |evt| {
                            match evt {
                                RemoteServiceEvent::OnCharacteristicValueUpdated { id, value } => {
                                    println!("(id = {}) value updated: {:X?}", id, value);
                                }
                            };
                            future::ok(())
                        })
                        .and_then(|_| Ok(()))
                        .recover(|e| {
                            eprintln!("Failed to listen for RemoteService events {:?}", e)
                        }),
                );

                client2.write().on_discover_characteristics(chrcs);
                Ok(())
            }
        })
}

// Read from a characteristic
fn read_characteristic(client: GattClientPtr, id: u64) -> impl Future<Item = (), Error = Error> {
    client
        .read()
        .active_proxy
        .as_ref()
        .unwrap()
        .read_characteristic(id, 0)
        .map_err(|_| BTError::new("Failed to send message").into())
        .and_then(move |(status, value)| match status.error {
            Some(e) => {
                println!("Failed to read characteristic: {}", BTError::from(*e));
                Ok(())
            }
            None => {
                println!("(id = {}) value: {:X?}", id, value);
                Ok(())
            }
        })
}

// Write to a characteristic.
fn write_characteristic(
    client: GattClientPtr, id: u64, value: Vec<u8>,
) -> impl Future<Item = (), Error = Error> {
    client
        .read()
        .active_proxy
        .as_ref()
        .unwrap()
        .write_characteristic(id, 0, &mut value.into_iter())
        .map_err(|_| BTError::new("Failed to send message").into())
        .and_then(move |status| match status.error {
            Some(e) => {
                println!("Failed to write to characteristic: {}", BTError::from(*e));
                Ok(())
            }
            None => {
                println!("(id = {}]) done", id);
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
    println!("    read-chr <id>              Read a characteristic");
    println!("    write-chr <id> <value>     Write to a characteristic");
    println!("    enable-notify <id>         Enable characteristic notifications");
    println!("    disable-notify <id>        Disable characteristic notifications");
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

fn do_connect(args: Vec<&str>, client: GattClientPtr) -> impl Future<Item = (), Error = Error> {
    if args.len() != 1 {
        println!("usage: connect <index>");
        return left_ok!();
    }

    let index: usize = match args[0].parse() {
        Err(_) => {
            println!("invalid index: {}", args[0]);
            return left_ok!();
        }
        Ok(i) => i,
    };

    let svc_id = match client.read().services.get(index) {
        None => {
            println!("index out of bounds! ({})", index);
            return left_ok!();
        }
        Some(s) => s.info.id,
    };

    // Initialize the remote service proxy.
    match endpoints2::create_endpoints() {
        Err(e) => Left(future::err(e.into())),
        Ok((proxy, server)) => {
            // First close the connection to the currently active service.
            if client.read().active_proxy.is_some() {
                client.write().active_proxy = None;
            }

            if let Err(e) = client.read().proxy.connect_to_service(svc_id, server) {
                return Left(future::err(e.into()));
            }
            client.write().active_index = index;
            client.write().active_proxy = Some(proxy);
            Right(discover_characteristics(client))
        }
    }
}

fn do_read_chr(args: Vec<&str>, client: GattClientPtr) -> impl Future<Item = (), Error = Error> {
    if args.len() != 1 {
        println!("usage: read-chr <id>");
        return left_ok!();
    }

    if client.read().active_proxy.is_none() {
        println!("no service connected");
        return left_ok!();
    }

    let id: u64 = match args[0].parse() {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return left_ok!();
        }
        Ok(i) => i,
    };

    Right(read_characteristic(client, id))
}

fn do_write_chr(args: Vec<&str>, client: GattClientPtr) -> impl Future<Item = (), Error = Error> {
    if args.len() < 1 {
        println!("usage: write-chr <id> <value>");
        return left_ok!();
    }

    if client.read().active_proxy.is_none() {
        println!("no service connected");
        return left_ok!();
    }

    let id: u64 = match args[0].parse() {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return left_ok!();
        }
        Ok(i) => i,
    };

    let value: Result<Vec<u8>, _> = args[1..].iter().map(|arg| arg.parse()).collect();

    match value {
        Err(_) => {
            println!("invalid value");
            left_ok!()
        }
        Ok(v) => Right(write_characteristic(client, id, v)),
    }
}

fn do_enable_notify(
    args: Vec<&str>, client: GattClientPtr,
) -> impl Future<Item = (), Error = Error> {
    if args.len() != 1 {
        println!("usage: enable-notify <id>");
        return left_ok!();
    }

    if client.read().active_proxy.is_none() {
        println!("no service connected");
        return left_ok!();
    }

    let id: u64 = match args[0].parse() {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return left_ok!();
        }
        Ok(i) => i,
    };

    Right(
        client
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .notify_characteristic(id, true)
            .map_err(|_| BTError::new("Failed to send message").into())
            .and_then(move |status| match status.error {
                Some(e) => {
                    println!("Failed to enable notifications: {}", BTError::from(*e));
                    Ok(())
                }
                None => {
                    println!("(id = {}]) done", id);
                    Ok(())
                }
            }),
    )
}

fn do_disable_notify(
    args: Vec<&str>, client: GattClientPtr,
) -> impl Future<Item = (), Error = Error> {
    if args.len() != 1 {
        println!("usage: disable-notify <id>");
        return left_ok!();
    }

    if client.read().active_proxy.is_none() {
        println!("no service connected");
        return left_ok!();
    }

    let id: u64 = match args[0].parse() {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return left_ok!();
        }
        Ok(i) => i,
    };

    Right(
        client
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .notify_characteristic(id, false)
            .map_err(|_| BTError::new("Failed to send message").into())
            .and_then(move |status| match status.error {
                Some(e) => {
                    println!("Failed to disable notifications: {}", BTError::from(*e));
                    Ok(())
                }
                None => {
                    println!("(id = {}]) done", id);
                    Ok(())
                }
            }),
    )
}

// Helper macro for boxing and casting the impl Future results of command handlers below. This is
// because the handlers potentially return different concrete types which can't be returned in the
// same Either branch.
macro_rules! right_cmd {
    ($cmd:expr) => {
        Right(Box::new($cmd) as Box<Future<Item = (), Error = Error> + Send>)
    };
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
        Some("connect") => right_cmd!(do_connect(args, client)),
        Some("read-chr") => right_cmd!(do_read_chr(args, client)),
        Some("write-chr") => right_cmd!(do_write_chr(args, client)),
        Some("enable-notify") => right_cmd!(do_enable_notify(args, client)),
        Some("disable-notify") => right_cmd!(do_disable_notify(args, client)),
        Some(cmd) => {
            eprintln!("Unknown command: {}", cmd);
            left_ok!()
        }
        None => left_ok!(),
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
