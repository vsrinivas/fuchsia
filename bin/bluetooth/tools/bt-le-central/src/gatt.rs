// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
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
        TryFutureExt, TryStreamExt,
    },
    parking_lot::RwLock,
    self::{
        commands::Cmd,
        types::Service,
    },
    std::{
        sync::Arc,
    },
};

pub mod commands;
pub mod repl;
pub mod types;

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

// Discover the characteristics of `client`'s currently connected service and
// cache them. `client.service_proxy` MUST be valid.
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
                RemoteServiceEvent::OnCharacteristicValueUpdated { id, value } => {
                    print!("{}{}", repl::CLEAR_LINE, repl::CHA);
                    println!("(id = {}) value updated: {:X?} {}", id, value,
                        decoded_string_value(&value));
                }
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
        None => println!("(id = {}) value: {:X?} {}", id, value, decoded_string_value(&value)),
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
        println!("usage: {}", Cmd::Connect.cmd_help());
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
    let (proxy, server) = endpoints::create_proxy()?;

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
        println!("usage: {}", Cmd::ReadChr.cmd_help());
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
        println!("usage: {}", Cmd::ReadLong.cmd_help());
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
        println!("usage: {}", Cmd::EnableNotify.cmd_help());
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
        println!("usage: {}", Cmd::DisableNotify.cmd_help());
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

/// Attempt to decode the value as a utf-8 string, replacing any invalid byte sequences with '.'
/// characters. Returns an empty string if there are not any valid utf-8 characters.
fn decoded_string_value(value: &[u8]) -> String {
    let decoded_value = String::from_utf8_lossy(value);
    if decoded_value.chars().any(|c| c != '�') {
        decoded_value.replace("�", ".")
    } else {
        String::new()
    }
}
