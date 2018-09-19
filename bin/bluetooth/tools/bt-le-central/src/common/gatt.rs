// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::common::gatt_types::Service,
    failure::Error,
    fidl::endpoints,
    fidl_fuchsia_bluetooth_gatt::{
        Characteristic as FidlCharacteristic,
        ClientProxy, RemoteServiceEvent,
        RemoteServiceProxy, ServiceInfo,
    },
    fuchsia_async as fasync,
    fuchsia_bluetooth::error::Error as BTError,
    futures::{
        channel::mpsc::channel,
        TryFutureExt, Stream, StreamExt, TryStreamExt,
    },
    parking_lot::RwLock,
    std::{
        io::{self, Read, Write},
        sync::Arc,
        thread,
    },
};

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
pub async fn start_gatt_loop<'a>(proxy: ClientProxy) -> Result<(), Error> {
    let client = GattClient::new(proxy);

    println!("  discovering services...");
    let list_services = client
        .read()
        .proxy
        .list_services(None);

    let (status, services) = await!(list_services)
        .map_err(|e| {
            let err = BTError::new(&format!("failed to list services: {}", e));
            println!("{}", e);
            err
        })?;

    match status.error {
        None => client.write().set_services(services),
        Some(e) => {
            let err = BTError::from(*e).into();
            println!("failed to list services: {}", err);
            return Err(err)
        }
    }

    let mut stdin_stream = stdin_stream();
    while let Some(cmd) = await!(stdin_stream.next()) {
        if cmd == "exit" {
            return Err(BTError::new("exited").into());
        } else {
            await!(handle_cmd(cmd, &client))
                .map_err(|e| {
                    println!("Error: {}", e);
                    e
                })?;
            print!("> ");
            io::stdout().flush().unwrap();
        }
    }

    Ok(())
}

// Discover the characteristics of |client|'s currently connected service and
// cache them. |client.service_proxy| MUST be valid.
async fn discover_characteristics(client: &GattClientPtr) -> Result<(), Error> {
    let discover_characteristics = client
        .read()
        .active_proxy
        .as_ref()
        .unwrap()
        .discover_characteristics();

    let (status, chrcs) = await!(discover_characteristics)
        .map_err(|_| BTError::new("Failed to send message"))?;

    if let Some(e) = status.error {
        println!("Failed to read characteristics: {}", BTError::from(*e));
        return Ok(());
    }

    let mut event_stream = client
        .read()
        .active_proxy
        .as_ref()
        .unwrap()
        .take_event_stream();

    fasync::spawn(async move {
        while let Some(evt) = await!(event_stream.try_next())? {
            match evt {
                RemoteServiceEvent::OnCharacteristicValueUpdated { id, value }
                    => println!("(id = {}) value updated: {:X?}", id, value),
            }
        }
        Ok::<(), fidl::Error>(())
    }.unwrap_or_else(|e| eprintln!("Failed to listen for RemoteService events {:?}", e)));

    client.write().on_discover_characteristics(chrcs);

    Ok(())
}

async fn read_characteristic(client: &GattClientPtr, id: u64)
    -> Result<(), Error>
{
    let read_characteristic = client
        .read()
        .active_proxy
        .as_ref()
        .unwrap()
        .read_characteristic(id);

    let (status, value) = await!(read_characteristic)
        .map_err(|_| BTError::new("Failed to send message"))?;

    match status.error {
        Some(e) => println!("Failed to read characteristic: {}", BTError::from(*e)),
        None => println!("(id = {}) value: {:X?}", id, value),
    }

    Ok(())
}

async fn read_long_characteristic(client: &GattClientPtr, id: u64, offset: u16, max_bytes: u16)
    -> Result<(), Error>
{
    let read_long_characteristic = client
        .read()
        .active_proxy
        .as_ref()
        .unwrap()
        .read_long_characteristic(id, offset, max_bytes);

    let (status, value) = await!(read_long_characteristic)
        .map_err(|_| BTError::new("Failed to send message"))?;

    match status.error {
        Some(e) => println!("Failed to read characteristic: {}", BTError::from(*e)),
        None => println!("(id = {}, offset = {}) value: {:X?}", id, offset, value),
    }

    Ok(())
}

async fn write_characteristic(
    client: &GattClientPtr, id: u64, value: Vec<u8>,
) -> Result<(), Error> {
    let write_characteristic = client
        .read()
        .active_proxy
        .as_ref()
        .unwrap()
        .write_characteristic(id, 0, &mut value.into_iter());

    let status = await!(write_characteristic)
        .map_err(|_| BTError::new("Failed to send message"))?;

    match status.error {
        Some(e) => println!("Failed to write to characteristic: {}", BTError::from(*e)),
        None => println!("(id = {}]) done", id),
    }

    Ok(())
}

fn write_without_response(client: &GattClientPtr, id: u64, value: Vec<u8>)
    -> Result<(), Error>
{
    client
        .read()
        .active_proxy
        .as_ref()
        .unwrap()
        .write_characteristic_without_response(id, &mut value.into_iter())
        .map_err(|_| BTError::new("Failed to send message").into())
}

// ===== REPL =====

fn do_help() {
    println!("Commands:");
    println!("    help                             Print this help message");
    println!("    list                             List discovered services");
    println!("    connect <index>                  Connect to a service");
    println!("    read-chr <id>                    Read a characteristic");
    println!("    read-long <id> <offset> <max>    Read a long characteristic");
    println!("    write-chr <id> <value>           Write to a characteristic");
    println!("    enable-notify <id>               Enable characteristic notifications");
    println!("    disable-notify <id>              Disable characteristic notifications");
    println!("    exit                             Quit and disconnect the peripheral");
}

fn do_list(args: &[&str], client: &GattClientPtr) {
    if !args.is_empty() {
        println!("list: expected 0 arguments");
    } else {
        client.read().display_services();
    }
}

async fn do_connect<'a>(args: &'a [&'a str], client: &'a GattClientPtr)
    -> Result<(), Error>
{
    if args.len() != 1 {
        println!("usage: connect <index>");
        return Ok(());
    }

    let index: usize = match args[0].parse() {
        Err(_) => {
            println!("invalid index: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let svc_id = match client.read().services.get(index) {
        None => {
            println!("index out of bounds! ({})", index);
            return Ok(());
        }
        Some(s) => s.info.id,
    };

    // Initialize the remote service proxy.
    let (proxy, server) = endpoints::create_endpoints()?;

    // First close the connection to the currently active service.
    if client.read().active_proxy.is_some() {
        client.write().active_proxy = None;
    }

    client.read().proxy.connect_to_service(svc_id, server)?;
    client.write().active_index = index;
    client.write().active_proxy = Some(proxy);
    await!(discover_characteristics(client))
}

async fn do_read_chr<'a>(args: &'a [&'a str], client: &'a GattClientPtr)
    -> Result<(), Error>
{
    if args.len() != 1 {
        println!("usage: read-chr <id>");
        return Ok(());
    }

    if client.read().active_proxy.is_none() {
        println!("no service connected");
        return Ok(());
    }

    let id: u64 = match args[0].parse() {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    await!(read_characteristic(client, id))
}

async fn do_read_long<'a>(args: &'a [&'a str], client: &'a GattClientPtr)
    -> Result<(), Error>
{
    if args.len() != 3 {
        println!("usage: read-long <id> <offset> <max bytes>");
        return Ok(());
    }

    if client.read().active_proxy.is_none() {
        println!("no service connected");
        return Ok(());
    }

    let id: u64 = match args[0].parse() {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let offset: u16 = match args[1].parse() {
        Err(_) => {
            println!("invalid offset: {}", args[1]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let max_bytes: u16 = match args[2].parse() {
        Err(_) => {
            println!("invalid max bytes: {}", args[2]);
            return Ok(());
        }
        Ok(i) => i,
    };

    await!(read_long_characteristic(client, id, offset, max_bytes))
}

async fn do_write_chr<'a>(mut args: Vec<&'a str>, client: &'a GattClientPtr)
    -> Result<(), Error>
{
    if args.len() < 1 {
        println!("usage: write-chr [-w] <id> <value>");
        return Ok(());
    }

    if client.read().active_proxy.is_none() {
        println!("no service connected");
        return Ok(());
    }

    let without_response: bool = args[0] == "-w";
    if without_response {
        args.remove(0);
    }

    let id: u64 = match args[0].parse() {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let value: Result<Vec<u8>, _> = args[1..].iter().map(|arg| arg.parse()).collect();

    match value {
        Err(_) => {
            println!("invalid value");
            Ok(())
        }
        Ok(v) => {
            if without_response {
                write_without_response(client, id, v)
            } else {
                await!(write_characteristic(client, id, v))
            }
        },
    }
}

async fn do_enable_notify<'a>(
    args: &'a [&'a str], client: &'a GattClientPtr,
) -> Result<(), Error> {
    if args.len() != 1 {
        println!("usage: enable-notify <id>");
        return Ok(());
    }

    if client.read().active_proxy.is_none() {
        println!("no service connected");
        return Ok(());
    }

    let id: u64 = match args[0].parse() {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let notify_characteristic = client
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .notify_characteristic(id, true);

    let status = await!(notify_characteristic)
        .map_err(|_| BTError::new("Failed to send message"))?;

    match status.error {
        Some(e) => println!("Failed to enable notifications: {}", BTError::from(*e)),
        None => println!("(id = {}]) done", id),
    }

    Ok(())
}

async fn do_disable_notify<'a>(
    args: &'a [&'a str], client: &'a GattClientPtr,
) -> Result<(), Error> {
    if args.len() != 1 {
        println!("usage: disable-notify <id>");
        return Ok(());
    }

    if client.read().active_proxy.is_none() {
        println!("no service connected");
        return Ok(());
    }

    let id: u64 = match args[0].parse() {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let notify_characteristic =
        client
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .notify_characteristic(id, false);

    let status = await!(notify_characteristic)
            .map_err(|_| BTError::new("Failed to send message"))?;

    match status.error {
        Some(e) => println!("Failed to disable notifications: {}", BTError::from(*e)),
        None => println!("(id = {}]) done", id),
    }

    Ok(())
}

// Processes |cmd| and returns its result.
// TODO(armansito): Use clap for fancier command processing.
async fn handle_cmd<'a>(line: String, client: &'a GattClientPtr)
    -> Result<(), Error>
{
    let mut components = line.trim().split_whitespace();
    let cmd = components.next();
    let args: Vec<&str> = components.collect();

    match cmd {
        Some("help") => {
            do_help();
            Ok(())
        },
        Some("list") => {
            do_list(&args, client);
            Ok(())
        },
        Some("connect") => await!(do_connect(&args, client)),
        Some("read-chr") => await!(do_read_chr(&args, client)),
        Some("read-long") => await!(do_read_long(&args, client)),
        Some("write-chr") => await!(do_write_chr(args, client)),
        Some("enable-notify") => await!(do_enable_notify(&args, client)),
        Some("disable-notify") => await!(do_disable_notify(&args, client)),
        Some(cmd) => {
            eprintln!("Unknown command: {}", cmd);
            Ok(())
        }
        None => Ok(()),
    }
}

fn stdin_stream() -> impl Stream<Item = String> {
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
    receiver
}
