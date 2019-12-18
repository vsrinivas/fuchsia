// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl::endpoints,
    fidl_fuchsia_bluetooth_gatt::{
        Characteristic as FidlCharacteristic, ClientProxy, RemoteServiceEvent, RemoteServiceProxy,
        ServiceInfo,
    },
    fuchsia_async as fasync,
    fuchsia_bluetooth::error::Error as BTError,
    futures::{TryFutureExt, TryStreamExt},
    parking_lot::RwLock,
    std::sync::Arc,
};

use self::{commands::Cmd, types::Service};

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

        self.services = services.into_iter().map(|info| Service::new(info)).collect();
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
async fn discover_characteristics(
    svc: &RemoteServiceProxy,
) -> Result<Vec<FidlCharacteristic>, Error> {
    let (status, chrcs) =
        svc.discover_characteristics().await.map_err(|_| BTError::new("Failed to send message"))?;

    if let Some(e) = status.error {
        let e = BTError::from(*e);
        println!("Failed to read characteristics: {}", e);
        return Err(e.into());
    }

    let mut event_stream = svc.take_event_stream();

    fasync::spawn(
        async move {
            while let Some(evt) = event_stream.try_next().await? {
                match evt {
                    RemoteServiceEvent::OnCharacteristicValueUpdated { id, value } => {
                        print!("{}{}", repl::CLEAR_LINE, repl::CHA);
                        println!(
                            "(id = {}) value updated: {:X?} {}",
                            id,
                            value,
                            decoded_string_value(&value)
                        );
                    }
                }
            }
            Ok::<(), fidl::Error>(())
        }
        .unwrap_or_else(|e| eprintln!("Failed to listen for RemoteService events {:?}", e)),
    );

    Ok(chrcs)
}

async fn read_characteristic(svc: &RemoteServiceProxy, id: u64) -> Result<(), Error> {
    let (status, value) =
        svc.read_characteristic(id).await.map_err(|_| BTError::new("Failed to send message"))?;

    match status.error {
        Some(e) => println!("Failed to read characteristic: {}", BTError::from(*e)),
        None => println!("(id = {}) value: {:X?} {}", id, value, decoded_string_value(&value)),
    }

    Ok(())
}

async fn read_long_characteristic(
    svc: &RemoteServiceProxy,
    id: u64,
    offset: u16,
    max_bytes: u16,
) -> Result<(), Error> {
    let (status, value) = svc
        .read_long_characteristic(id, offset, max_bytes)
        .await
        .map_err(|_| BTError::new("Failed to send message"))?;

    match status.error {
        Some(e) => println!("Failed to read characteristic: {}", BTError::from(*e)),
        None => println!("(id = {}, offset = {}) value: {:X?}", id, offset, value),
    }

    Ok(())
}

async fn write_characteristic(
    svc: &RemoteServiceProxy,
    id: u64,
    value: Vec<u8>,
) -> Result<(), Error> {
    let status = svc
        .write_characteristic(id, &mut value.into_iter())
        .await
        .map_err(|_| BTError::new("Failed to send message"))?;

    match status.error {
        Some(e) => println!("Failed to write to characteristic: {}", BTError::from(*e)),
        None => println!("(id = {}) done", id),
    }

    Ok(())
}

async fn write_long_characteristic(
    svc: &RemoteServiceProxy,
    id: u64,
    offset: u16,
    value: Vec<u8>,
) -> Result<(), Error> {
    let status = svc
        .write_long_characteristic(id, offset, &mut value.into_iter())
        .await
        .map_err(|_| BTError::new("Failed to send message"))?;

    match status.error {
        Some(e) => println!("Failed to write long characteristic: {}", BTError::from(*e)),
        None => println!("(id = {}, offset = {}) done", id, offset),
    }

    Ok(())
}

fn write_without_response(svc: &RemoteServiceProxy, id: u64, value: Vec<u8>) -> Result<(), Error> {
    svc.write_characteristic_without_response(id, &mut value.into_iter())
        .map_err(|_| BTError::new("Failed to send message").into())
}

async fn read_descriptor(svc: &RemoteServiceProxy, id: u64) -> Result<(), Error> {
    let (status, value) =
        svc.read_descriptor(id).await.map_err(|_| BTError::new("Failed to send message"))?;

    match status.error {
        Some(e) => println!("Failed to read descriptor: {}", BTError::from(*e)),
        None => println!("(id = {}) value: {:X?}", id, value),
    }

    Ok(())
}

async fn read_long_descriptor(
    svc: &RemoteServiceProxy,
    id: u64,
    offset: u16,
    max_bytes: u16,
) -> Result<(), Error> {
    let (status, value) = svc
        .read_long_descriptor(id, offset, max_bytes)
        .await
        .map_err(|_| BTError::new("Failed to send message"))?;

    match status.error {
        Some(e) => println!("Failed to read long descriptor: {}", BTError::from(*e)),
        None => println!("(id = {}, offset = {}) value: {:X?}", id, offset, value),
    }

    Ok(())
}

async fn write_descriptor(svc: &RemoteServiceProxy, id: u64, value: Vec<u8>) -> Result<(), Error> {
    let status = svc
        .write_descriptor(id, &mut value.into_iter())
        .await
        .map_err(|_| BTError::new("Failed to send message"))?;

    match status.error {
        Some(e) => println!("Failed to write to descriptor: {}", BTError::from(*e)),
        None => println!("(id = {}) done", id),
    }

    Ok(())
}

async fn write_long_descriptor(
    svc: &RemoteServiceProxy,
    id: u64,
    offset: u16,
    value: Vec<u8>,
) -> Result<(), Error> {
    let status = svc
        .write_long_descriptor(id, offset, &mut value.into_iter())
        .await
        .map_err(|_| BTError::new("Failed to send message"))?;

    match status.error {
        Some(e) => println!("Failed to write long descriptor: {}", BTError::from(*e)),
        None => println!("(id = {}, offset = {}) done", id, offset),
    }

    Ok(())
}

// ===== REPL =====

fn do_list(args: &[&str], client: &GattClientPtr) {
    if !args.is_empty() {
        println!("list: expected 0 arguments");
    } else {
        client.read().display_services();
    }
}

async fn do_connect<'a>(args: &'a [&'a str], client: &'a GattClientPtr) -> Result<(), Error> {
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
    let chrcs = discover_characteristics(&proxy).await?;

    client.write().active_index = index;
    client.write().active_proxy = Some(proxy);
    client.write().on_discover_characteristics(chrcs);

    Ok(())
}

async fn do_read_chr<'a>(args: &'a [&'a str], client: &'a GattClientPtr) -> Result<(), Error> {
    if args.len() != 1 {
        println!("usage: {}", Cmd::ReadChr.cmd_help());
        return Ok(());
    }

    let id: u64 = match args[0].parse() {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    match &client.read().active_proxy {
        Some(svc) => read_characteristic(svc, id).await,
        None => {
            println!("no service connected");
            Ok(())
        }
    }
}

async fn do_read_long_chr<'a>(args: &'a [&'a str], client: &'a GattClientPtr) -> Result<(), Error> {
    if args.len() != 3 {
        println!("usage: {}", Cmd::ReadLongChr.cmd_help());
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

    match &client.read().active_proxy {
        Some(svc) => read_long_characteristic(svc, id, offset, max_bytes).await,
        None => {
            println!("no service connected");
            Ok(())
        }
    }
}

async fn do_write_chr<'a>(mut args: Vec<&'a str>, client: &'a GattClientPtr) -> Result<(), Error> {
    if args.len() < 2 {
        println!("usage: write-chr [-w] <id> <value>");
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
        Ok(v) => match &client.read().active_proxy {
            Some(svc) => {
                if without_response {
                    write_without_response(svc, id, v)
                } else {
                    write_characteristic(svc, id, v).await
                }
            }
            None => {
                println!("no service connected");
                Ok(())
            }
        },
    }
}

async fn do_write_long_chr<'a>(
    args: &'a [&'a str],
    client: &'a GattClientPtr,
) -> Result<(), Error> {
    if args.len() < 3 {
        println!("usage: {}", Cmd::WriteLongDesc.cmd_help());
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

    let value: Result<Vec<u8>, _> = args[2..].iter().map(|arg| arg.parse()).collect();

    match value {
        Err(_) => {
            println!("invalid value");
            Ok(())
        }
        Ok(v) => match &client.read().active_proxy {
            Some(svc) => write_long_characteristic(svc, id, offset, v).await,
            None => {
                println!("no service connected");
                Ok(())
            }
        },
    }
}

async fn do_read_desc<'a>(args: &'a [&'a str], client: &'a GattClientPtr) -> Result<(), Error> {
    if args.len() != 1 {
        println!("usage: {}", Cmd::ReadDesc.cmd_help());
        return Ok(());
    }

    let id: u64 = match args[0].parse() {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    match &client.read().active_proxy {
        Some(svc) => read_descriptor(svc, id).await,
        None => {
            println!("no service connected");
            Ok(())
        }
    }
}

async fn do_read_long_desc<'a>(
    args: &'a [&'a str],
    client: &'a GattClientPtr,
) -> Result<(), Error> {
    if args.len() != 3 {
        println!("usage: {}", Cmd::ReadLongDesc.cmd_help());
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

    match &client.read().active_proxy {
        Some(svc) => read_long_descriptor(svc, id, offset, max_bytes).await,
        None => {
            println!("no service connected");
            Ok(())
        }
    }
}

async fn do_write_desc<'a>(args: Vec<&'a str>, client: &'a GattClientPtr) -> Result<(), Error> {
    if args.len() < 2 {
        println!("usage: {}", Cmd::WriteDesc.cmd_help());
        return Ok(());
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
        Ok(v) => match &client.read().active_proxy {
            Some(svc) => write_descriptor(svc, id, v).await,
            None => {
                println!("no service connected");
                Ok(())
            }
        },
    }
}

async fn do_write_long_desc<'a>(
    args: &'a [&'a str],
    client: &'a GattClientPtr,
) -> Result<(), Error> {
    if args.len() < 3 {
        println!("usage: {}", Cmd::WriteLongDesc.cmd_help());
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

    let value: Result<Vec<u8>, _> = args[2..].iter().map(|arg| arg.parse()).collect();

    match value {
        Err(_) => {
            println!("invalid value");
            Ok(())
        }
        Ok(v) => match &client.read().active_proxy {
            Some(svc) => write_long_descriptor(svc, id, offset, v).await,
            None => {
                println!("no service connected");
                Ok(())
            }
        },
    }
}

async fn do_enable_notify<'a>(args: &'a [&'a str], client: &'a GattClientPtr) -> Result<(), Error> {
    if args.len() != 1 {
        println!("usage: {}", Cmd::EnableNotify.cmd_help());
        return Ok(());
    }

    let id: u64 = match args[0].parse() {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    match &client.read().active_proxy {
        Some(svc) => {
            let status = svc
                .notify_characteristic(id, true)
                .await
                .map_err(|_| BTError::new("Failed to send message"))?;
            match status.error {
                Some(e) => println!("Failed to enable notifications: {}", BTError::from(*e)),
                None => println!("(id = {}) done", id),
            }
            Ok(())
        }
        None => {
            println!("no service connected");
            Ok(())
        }
    }
}

async fn do_disable_notify<'a>(
    args: &'a [&'a str],
    client: &'a GattClientPtr,
) -> Result<(), Error> {
    if args.len() != 1 {
        println!("usage: {}", Cmd::DisableNotify.cmd_help());
        return Ok(());
    }

    let id: u64 = match args[0].parse() {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    match &client.read().active_proxy {
        Some(svc) => {
            let status = svc
                .notify_characteristic(id, false)
                .await
                .map_err(|_| BTError::new("Failed to send message"))?;
            match status.error {
                Some(e) => println!("Failed to disable notifications: {}", BTError::from(*e)),
                None => println!("(id = {}) done", id),
            }
            Ok(())
        }
        None => {
            println!("no service connected");
            Ok(())
        }
    }
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
