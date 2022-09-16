// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context, Error};
use fidl::{encoding::Decodable, endpoints};
use fidl_fuchsia_bluetooth;
use fidl_fuchsia_bluetooth_gatt2::{
    Characteristic as FidlCharacteristic, CharacteristicNotifierMarker,
    CharacteristicNotifierRequest, ClientProxy, Handle, LongReadOptions, ReadByTypeResult,
    ReadOptions, ReadValue, RemoteServiceMarker, RemoteServiceProxy, ServiceInfo, ShortReadOptions,
    WriteMode, WriteOptions,
};
use fuchsia_async as fasync;
use fuchsia_bluetooth::error::Error as BTError;
use fuchsia_bluetooth::types::Uuid;
use futures::StreamExt;
use num::Num;
use parking_lot::RwLock;
use std::{collections::HashMap, num::ParseIntError, str::FromStr, sync::Arc};

use self::{commands::Cmd, types::Service};

pub mod commands;
pub mod repl;
pub mod types;

struct ActiveService {
    proxy: RemoteServiceProxy,
    notifiers: HashMap<u64, fasync::Task<()>>,
}

impl ActiveService {
    fn new(proxy: RemoteServiceProxy) -> Self {
        ActiveService { proxy: proxy, notifiers: HashMap::new() }
    }
}

type GattClientPtr = Arc<RwLock<GattClient>>;

struct GattClient {
    proxy: ClientProxy,

    // Services discovered on this client.
    services: Vec<Service>,

    // The index of the currently connected service, if any.
    active_index: usize,

    // Proxy and associated state for the currently connected service, if any.
    active_service: Option<ActiveService>,
}

impl GattClient {
    fn new(proxy: ClientProxy) -> GattClientPtr {
        Arc::new(RwLock::new(GattClient {
            proxy: proxy,
            services: vec![],
            active_index: 0,
            active_service: None,
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

    fn try_clone_proxy(&self) -> Option<RemoteServiceProxy> {
        self.active_service.as_ref().map(|s| s.proxy.clone())
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
    let chrcs: Vec<FidlCharacteristic> = svc
        .discover_characteristics()
        .await
        .map_err(|_| format_err!("Failed to discover characteristics"))?;
    Ok(chrcs)
}

async fn read_characteristic(svc: &RemoteServiceProxy, id: u64) -> Result<(), Error> {
    let mut options = ReadOptions::ShortRead(ShortReadOptions {});
    let value: ReadValue = svc
        .read_characteristic(&mut Handle { value: id }, &mut options)
        .await
        .map_err(|e| format_err!("Failed to read characteristic: {}", BTError::from(e)))?
        .map_err(|e| format_err!("Failed to read characteristic: {:?}", e))?;

    if let Some(value) = value.value {
        println!("(id = {}) value: {:X?} {}", id, &value, decoded_string_value(&value[..]));
    }

    Ok(())
}

async fn read_long_characteristic(
    svc: &RemoteServiceProxy,
    id: u64,
    offset: u16,
    max_bytes: u16,
) -> Result<(), Error> {
    let mut options = ReadOptions::LongRead(LongReadOptions {
        offset: Some(offset),
        max_bytes: Some(max_bytes),
        ..LongReadOptions::EMPTY
    });

    let value: ReadValue = svc
        .read_characteristic(&mut Handle { value: id }, &mut options)
        .await
        .map_err(|e| format_err!("Failed to read characteristic: {}", BTError::from(e)))?
        .map_err(|e| format_err!("Failed to read characteristic: {:?}", e))?;

    if let Some(value) = value.value {
        println!(
            "(id = {}, offset = {}) value: {:X?} {}",
            id,
            offset,
            &value,
            decoded_string_value(&value)
        );
    }

    Ok(())
}

async fn write_characteristic(
    svc: &RemoteServiceProxy,
    id: u64,
    mode: WriteMode,
    offset: u16,
    value: Vec<u8>,
) -> Result<(), Error> {
    let options =
        WriteOptions { write_mode: Some(mode), offset: Some(offset), ..WriteOptions::EMPTY };
    svc.write_characteristic(&mut Handle { value: id }, &value, options)
        .await
        .context("Failed to write characteristic")?
        .map_err(|e| format_err!("Failed to write characteristic: {:?}", e))?;

    println!("(id = {}, offset = {}) done", id, offset);

    Ok(())
}

async fn read_descriptor(svc: &RemoteServiceProxy, id: u64) -> Result<(), Error> {
    let value: ReadValue = svc
        .read_descriptor(
            &mut Handle { value: id },
            &mut ReadOptions::ShortRead(ShortReadOptions::new_empty()),
        )
        .await
        .context("Failed to read descriptor")?
        .map_err(|e| format_err!("Failed to read descriptor: {:?}", e))?;

    if let Some(value) = value.value {
        println!("(id = {}) value: {:X?}", id, value);
    }

    Ok(())
}

async fn read_long_descriptor(
    svc: &RemoteServiceProxy,
    id: u64,
    offset: u16,
    max_bytes: u16,
) -> Result<(), Error> {
    let value: ReadValue = svc
        .read_descriptor(
            &mut Handle { value: id },
            &mut ReadOptions::LongRead(LongReadOptions {
                offset: Some(offset),
                max_bytes: Some(max_bytes),
                ..LongReadOptions::EMPTY
            }),
        )
        .await
        .context("Failed to read descriptor")?
        .map_err(|e| format_err!("Failed to read descriptor: {:?}", e))?;

    if let Some(value) = value.value {
        println!("(id = {}, offset = {}) value: {:X?}", id, offset, value);
    }

    Ok(())
}

async fn write_descriptor(
    svc: &RemoteServiceProxy,
    id: u64,
    mode: WriteMode,
    offset: u16,
    value: Vec<u8>,
) -> Result<(), Error> {
    svc.write_descriptor(
        &mut Handle { value: id },
        &value,
        WriteOptions { write_mode: Some(mode), offset: Some(offset), ..WriteOptions::EMPTY },
    )
    .await
    .context("Failed to write descriptor")?
    .map_err(|e| format_err!("Failed to write descriptor: {:?}", e))?;

    println!("(id = {}, offset = {}) done", id, offset);

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

    let index: usize = match parse_int(args[0]) {
        Err(_) => {
            println!("invalid index: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let mut svc_handle = match client.read().services.get(index) {
        None => {
            println!("index out of bounds! ({})", index);
            return Ok(());
        }
        Some(s) => s.info.handle.expect("service missing handle"),
    };

    // Initialize the remote service proxy.
    let (proxy, server) = endpoints::create_proxy::<RemoteServiceMarker>()
        .context("Failed to create RemoteService endpoints")?;

    // First close the connection to the currently active service.
    let _ = client.write().active_service.take();

    client.read().proxy.connect_to_service(&mut svc_handle, server)?;
    let chrcs = discover_characteristics(&proxy).await?;

    client.write().active_index = index;
    client.write().active_service = Some(ActiveService::new(proxy));
    client.write().on_discover_characteristics(chrcs);

    Ok(())
}

async fn do_read_chr<'a>(args: &'a [&'a str], client: &'a GattClientPtr) -> Result<(), Error> {
    if args.len() != 1 {
        println!("usage: {}", Cmd::ReadChr.cmd_help());
        return Ok(());
    }

    let id: u64 = match parse_int(args[0]) {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    match &client.read().active_service {
        Some(svc) => read_characteristic(&svc.proxy, id).await,
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

    let id: u64 = match parse_int(args[0]) {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let offset: u16 = match parse_int(args[1]) {
        Err(_) => {
            println!("invalid offset: {}", args[1]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let max_bytes: u16 = match parse_int(args[2]) {
        Err(_) => {
            println!("invalid max bytes: {}", args[2]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let proxy_opt = client.read().try_clone_proxy();
    match proxy_opt {
        Some(proxy) => read_long_characteristic(&proxy, id, offset, max_bytes).await,
        None => {
            println!("no service connected");
            Ok(())
        }
    }
}

async fn do_write_chr<'a>(mut args: Vec<&'a str>, client: &'a GattClientPtr) -> Result<(), Error> {
    if args.len() < 2 {
        println!("usage: {}", Cmd::WriteChr.cmd_help());
        return Ok(());
    }

    let mode = if args[0] == "-w" {
        let _ = args.remove(0);
        WriteMode::WithoutResponse
    } else {
        WriteMode::Default
    };

    let id: u64 = match parse_int(args[0]) {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let value: Result<Vec<u8>, _> = args[1..].iter().map(|arg| parse_int(arg)).collect();

    match value {
        Err(_) => {
            println!("invalid value");
            Ok(())
        }
        Ok(v) => match &client.read().active_service {
            Some(svc) => write_characteristic(&svc.proxy, id, mode, 0, v).await,
            None => {
                println!("no service connected");
                Ok(())
            }
        },
    }
}

async fn do_write_long_chr<'a>(
    mut args: Vec<&'a str>,
    client: &'a GattClientPtr,
) -> Result<(), Error> {
    if args.len() < 3 {
        println!("usage: {}", Cmd::WriteLongChr.cmd_help());
        return Ok(());
    }

    let mode = if args[0] == "-r" {
        let _ = args.remove(0);
        WriteMode::Reliable
    } else {
        WriteMode::Default
    };

    let id: u64 = match parse_int(args[0]) {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let offset: u16 = match parse_int(args[1]) {
        Err(_) => {
            println!("invalid offset: {}", args[1]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let value: Result<Vec<u8>, _> = args[2..].iter().map(|arg| parse_int(arg)).collect();

    match value {
        Err(_) => {
            println!("invalid value");
            Ok(())
        }
        Ok(v) => match &client.read().active_service {
            Some(svc) => write_characteristic(&svc.proxy, id, mode, offset, v).await,
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

    let id: u64 = match parse_int(args[0]) {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    match &client.read().active_service {
        Some(svc) => read_descriptor(&svc.proxy, id).await,
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

    let id: u64 = match parse_int(args[0]) {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let offset: u16 = match parse_int(args[1]) {
        Err(_) => {
            println!("invalid offset: {}", args[1]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let max_bytes: u16 = match parse_int(args[2]) {
        Err(_) => {
            println!("invalid max bytes: {}", args[2]);
            return Ok(());
        }
        Ok(i) => i,
    };

    match &client.read().active_service {
        Some(svc) => read_long_descriptor(&svc.proxy, id, offset, max_bytes).await,
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

    let id: u64 = match parse_int(args[0]) {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let value: Result<Vec<u8>, _> = args[1..].iter().map(|arg| parse_int(arg)).collect();

    match value {
        Err(_) => {
            println!("invalid value");
            Ok(())
        }
        Ok(v) => match &client.read().active_service {
            Some(svc) => write_descriptor(&svc.proxy, id, WriteMode::Default, 0, v).await,
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

    let id: u64 = match parse_int(args[0]) {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let offset: u16 = match parse_int(args[1]) {
        Err(_) => {
            println!("invalid offset: {}", args[1]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let value: Result<Vec<u8>, _> = args[2..].iter().map(|arg| parse_int(arg)).collect();

    match value {
        Err(_) => {
            println!("invalid value");
            Ok(())
        }
        Ok(v) => match &client.read().active_service {
            Some(svc) => write_descriptor(&svc.proxy, id, WriteMode::Default, offset, v).await,
            None => {
                println!("no service connected");
                Ok(())
            }
        },
    }
}

fn print_read_by_type_result(result: &ReadByTypeResult) {
    match (result.value.as_ref(), result.error.as_ref()) {
        (Some(value), None) => {
            let value = value.value.as_ref().expect("read by value response value");
            println!(
                "[id: {}, value: {:X?} {}]",
                result.handle.as_ref().expect("read by value response handle").value,
                value,
                decoded_string_value(value)
            );
        }
        (None, Some(error)) => {
            println!(
                "[id: {}, error: {:?}]",
                result.handle.as_ref().expect("read by value response id").value,
                error
            );
        }
        _ => {
            println!("Invalid FIDL response");
        }
    }
}

async fn do_read_by_type<'a>(args: &'a [&'a str], client: &'a GattClientPtr) -> Result<(), Error> {
    if args.len() != 1 {
        println!("usage: {}", Cmd::ReadByType.cmd_help());
        return Ok(());
    }

    let uuid = match Uuid::from_str(args[0]) {
        Err(_) => {
            println!("invalid uuid: {}", args[0]);
            return Ok(());
        }
        Ok(u) => u,
    };

    let mut fidl_uuid = fidl_fuchsia_bluetooth::Uuid::from(uuid);
    match &client.read().active_service {
        Some(svc) => match svc.proxy.read_by_type(&mut fidl_uuid).await {
            Ok(Ok(results)) => {
                if results.len() == 0 {
                    println!("No results received.");
                } else {
                    for res in results.into_iter() {
                        print_read_by_type_result(&res);
                    }
                }
            }
            Ok(Err(e)) => {
                println!("read by type error result: {:?}", e);
            }
            Err(e) => {
                println!("read by type FIDL error: {:?}", BTError::from(e));
            }
        },
        None => {
            println!("no service connected");
        }
    }
    Ok(())
}

async fn do_enable_notify<'a>(args: &'a [&'a str], client: &'a GattClientPtr) -> Result<(), Error> {
    if args.len() != 1 {
        println!("usage: {}", Cmd::EnableNotify.cmd_help());
        return Ok(());
    }

    let id: u64 = match parse_int(args[0]) {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let active_service_valid = || -> bool {
        match &client.read().active_service {
            None => {
                println!("no service connected");
                return false;
            }
            Some(svc) => {
                if svc.notifiers.contains_key(&id) {
                    println!("(id = {}) notifications already enabled", id);
                    return false;
                } else {
                    return true;
                }
            }
        };
    };
    if !active_service_valid() {
        return Ok(());
    }

    let (notifier_client, mut notifier_req_stream) =
        endpoints::create_request_stream::<CharacteristicNotifierMarker>()?;
    let proxy = client.read().try_clone_proxy().unwrap();
    proxy
        .register_characteristic_notifier(&mut Handle { value: id }, notifier_client)
        .await
        .context("Failed to register characteristic notifier")?
        .map_err(|e| format_err!("Failed to register characteristic notifier: {:?}", e))?;

    // The service could have closed during the async FIDL call above, or another notifier could have been registered (making this one redundant).
    if !active_service_valid() {
        return Ok(());
    }

    let task = fasync::Task::spawn(async move {
        while let Some(req) = notifier_req_stream.next().await {
            match req {
                Ok(event) => match event {
                    CharacteristicNotifierRequest::OnNotification { value, responder } => {
                        if let Some(value) = value.value {
                            print!("{}{}", repl::CLEAR_LINE, repl::CHA);
                            println!(
                                "(id = {}) value updated: {:X?} {}",
                                id,
                                value,
                                decoded_string_value(&value)
                            );
                        }
                        if let Err(e) = responder.send() {
                            println!("(id = {}) notifier closed due to responder error: {}", id, e);
                            return;
                        }
                    }
                },
                Err(e) => {
                    println!("(id = {}) notifier closed due to error: {}", id, e);
                    return;
                }
            }
        }
        println!("(id = {}) notifier closed", id);
    });

    let old_value = client.write().active_service.as_mut().unwrap().notifiers.insert(id, task);
    assert!(old_value.is_none());

    println!("(id = {}) done", id);
    Ok(())
}

async fn do_disable_notify<'a>(
    args: &'a [&'a str],
    client: &'a GattClientPtr,
) -> Result<(), Error> {
    if args.len() != 1 {
        println!("usage: {}", Cmd::DisableNotify.cmd_help());
        return Ok(());
    }

    let id: u64 = match parse_int(args[0]) {
        Err(_) => {
            println!("invalid id: {}", args[0]);
            return Ok(());
        }
        Ok(i) => i,
    };

    let task = match client.write().active_service.as_mut() {
        Some(svc) => svc.notifiers.remove(&id),
        None => {
            println!("no service connected");
            return Ok(());
        }
    };
    match task {
        Some(task) => {
            let _ = task.cancel().await;
            println!("(id = {}) done", id);
        }
        None => println!("(id = {}) notifications not enabled", id),
    };
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

/// Attempt to parse a string into an integer.  If the string begins with 0x, treat the rest
/// of the string as a hex value, otherwise treat it as decimal.
fn parse_int<N>(input: &str) -> Result<N, ParseIntError>
where
    N: Num<FromStrRadixErr = ParseIntError>,
{
    if input.starts_with("0x") {
        N::from_str_radix(&input[2..], 16)
    } else {
        N::from_str_radix(input, 10)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use bt_fidl_mocks::gatt2::{ClientMock, RemoteServiceMock};
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_gatt2::{
        ClientMarker, RemoteServiceReadByTypeResult, ServiceHandle,
    };
    use fuchsia_zircon::DurationNum;
    use futures::{future::FutureExt, join, pin_mut, select};
    use std::vec::Vec;

    #[fuchsia::test]
    async fn test_read_by_type() {
        let (client, _stream) = create_proxy_and_stream::<ClientMarker>().unwrap();
        let gatt_client = GattClient::new(client);

        let args = vec!["0000180d-0000-1000-8000-00805f9b34fb"];
        let read_fut = do_read_by_type(&args, &gatt_client);

        let (service_proxy, mut service_mock) =
            RemoteServiceMock::new(20.seconds()).expect("failed to create mock");

        gatt_client.write().active_service = Some(ActiveService::new(service_proxy));

        let expected_uuid = Uuid::new16(0x180d);
        let result: RemoteServiceReadByTypeResult = Ok(vec![]);
        let expect_fut = service_mock.expect_read_by_type(expected_uuid, result);

        let (read_result, expect_result) = join!(read_fut, expect_fut);
        let _ = read_result.expect("do read by type failed");
        let _ = expect_result.expect("read by type expectation not satisfied");
    }

    #[fuchsia::test]
    async fn test_connect_and_enable_notify() {
        let (client_proxy, mut client_mock) =
            ClientMock::new(20.seconds()).expect("failed to create mock");
        let gatt_client = GattClient::new(client_proxy);

        let services =
            vec![ServiceInfo { handle: Some(ServiceHandle { value: 1 }), ..ServiceInfo::EMPTY }];
        gatt_client.write().set_services(services);

        let args = vec!["0"]; // service index
        let connect_fut = do_connect(&args, &gatt_client).fuse();
        let expect_connect_fut =
            client_mock.expect_connect_to_service(ServiceHandle { value: 1 }).fuse();
        pin_mut!(connect_fut, expect_connect_fut);

        let (_, service_server) = select! {
            _ = connect_fut =>  panic!("connect_fut completed prematurely (characteristics haven't been discovered)"),
            result = expect_connect_fut => result.expect("expect connect failed"),
        };
        let service_stream =
            service_server.into_stream().expect("failed to turn server into stream");
        let mut service_mock = RemoteServiceMock::from_stream(service_stream, 20.seconds());

        let characteristics = Vec::new();
        let expect_discover_fut = service_mock.expect_discover_characteristics(&characteristics);

        let (connect_result, expect_discover_result) = join!(connect_fut, expect_discover_fut);
        connect_result.expect("failed to connect");
        expect_discover_result.expect("expect discover failed");

        let args = vec!["2"]; // characteristic handle
        let register_fut = do_enable_notify(&args, &gatt_client);
        let expect_register_fut =
            service_mock.expect_register_characteristic_notifier(Handle { value: 2 });

        let (register_result, expect_register_result) = join!(register_fut, expect_register_fut);
        register_result.expect("failed to register notifier");
        let notifier_client = expect_register_result.expect("expect register failed");
        let notifier = notifier_client.into_proxy().expect("failed to turn notifier into proxy");

        let notification_value = ReadValue {
            handle: Some(Handle { value: 2 }),
            value: Some(vec![0x00, 0x01, 0x02]),
            maybe_truncated: Some(false),
            ..ReadValue::EMPTY
        };

        // The notification should immediately receive a flow control response.
        notifier.on_notification(notification_value).await.expect("on_notification");
    }
}
