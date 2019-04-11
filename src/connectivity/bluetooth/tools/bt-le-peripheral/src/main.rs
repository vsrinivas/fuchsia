// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api, async_await, await_macro)]

use {
    base64,
    failure::{bail, Error, ResultExt},
    fidl_fuchsia_bluetooth::UInt16,
    fidl_fuchsia_bluetooth_le::{
        AdvertisingData, ManufacturerSpecificDataEntry, PeripheralMarker, PeripheralProxy,
        ServiceDataEntry,
    },
    fuchsia_async as fasync,
    fuchsia_bluetooth::assigned_numbers::{find_service_uuid, AssignedNumber},
    fuchsia_component::client::connect_to_service,
    futures::TryStreamExt,
    std::fmt,
    structopt::StructOpt,
};

const ADVERTISING_INTERVAL_MS_MINIMUM: u32 = 20;
const ADVERTISING_INTERVAL_MS_MAXIMUM: u32 = 10_000_000;

// Defines all the command line arguments accepted by the tool.
#[derive(StructOpt, Debug)]
#[structopt()]
struct Opt {
    #[structopt(short = "n", long = "name", help = "Advertised Device Name")]
    device_name: Option<String>,
    #[structopt(
        short = "s",
        long = "service",
        help = "Advertised Service UUIDs. Multiple instances of the flag allowed."
    )]
    service_uuids: Vec<String>,
    #[structopt(
        short = "a",
        long = "anonymous",
        help = "Do not include device address in advertising data"
    )]
    anonymous: bool,
    #[structopt(short = "c", long = "connectable", help = "Advertise as connectable")]
    connectable: bool,
    #[structopt(
        short = "i",
        long = "interval",
        default_value = "1000",
        parse(try_from_str = "parse_interval"),
        help = "Advertising interval in milliseconds"
    )]
    interval: u32,
    #[structopt(long = "appearance", help = "Advertised appearance as integer value")]
    appearance: Option<u16>,
    #[structopt(
        short = "u",
        help = "URIs included in the advertising packet. Multiple instances of the flag allowed."
    )]
    uris: Vec<String>,
    #[structopt(
        long = "service-data",
        parse(try_from_str = "parse_service_data"),
        help = "Service data in the format '<service_uuid>:<string_data>'. \
                Multiple instances of the flag allowed."
    )]
    service_data: Vec<ServiceDataEntry>,
    #[structopt(
        long = "binary-service-data",
        parse(try_from_str = "parse_binary_service_data"),
        help = "Service data in the format '<service_uuid>:<base64_data>'. \
                Multiple instances of the flag allowed."
    )]
    binary_service_data: Vec<ServiceDataEntry>,
    #[structopt(
        long = "manufacturer-data",
        parse(try_from_str = "parse_manufacturer_data"),
        help = "Manufacturer specific data in the format '<company_id>:<string_data>'. \
                Multiple instances of the flag allowed."
    )]
    manufacturer_data: Vec<ManufacturerSpecificDataEntry>,
    #[structopt(
        long = "binary-manufacturer-data",
        parse(try_from_str = "parse_binary_manufacturer_data"),
        help = "Manufacturer specific data in the format '<company_id>:<base64_data>'. \
                Multiple instances of the flag allowed."
    )]
    binary_manufacturer_data: Vec<ManufacturerSpecificDataEntry>,
}

/// Represents a Bluetooth ID
enum BluetoothUUID {
    /// Bluetooth SIG assigned identifier
    Assigned(AssignedNumber),
    /// Custom identifier
    Custom(String),
}

impl BluetoothUUID {
    /// returns the identifier of a `BluetoothUUID`
    fn to_uuid(&self) -> &str {
        match self {
            BluetoothUUID::Assigned(id) => id.number,
            BluetoothUUID::Custom(ref id) => id,
        }
    }
}

impl fmt::Display for BluetoothUUID {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let name = match self {
            BluetoothUUID::Assigned(id) => id.name,
            BluetoothUUID::Custom(ref id) => id,
        };
        write!(f, "{}", name)
    }
}

/// Parse a vector of `BluetoothUUID` values from a vector of `String` values
fn parse_service_uuids(raw: Vec<String>) -> Vec<BluetoothUUID> {
    raw.into_iter()
        .map(|id| {
            find_service_uuid(&id).map(BluetoothUUID::Assigned).unwrap_or(BluetoothUUID::Custom(id))
        })
        .collect()
}

/// Parse a raw string as a millisecond interval checking that it lies within the allowed range.
fn parse_interval(raw: &str) -> Result<u32, Error> {
    let interval: u32 = raw.parse()?;
    if ADVERTISING_INTERVAL_MS_MINIMUM > interval || ADVERTISING_INTERVAL_MS_MAXIMUM < interval {
        bail!(
            "interval must be within the range [{}, {}]",
            ADVERTISING_INTERVAL_MS_MINIMUM,
            ADVERTISING_INTERVAL_MS_MAXIMUM,
        );
    }
    Ok(interval)
}

/// Parse ":" delimited pair from a string into a tuple pair of (id, utf8 encoded payload)
fn parse_data(raw: &str) -> Result<(&str, Vec<u8>), Error> {
    let elements: Vec<_> = raw.split(":").collect();
    if elements.len() != 2 {
        bail!("Argument must be a ':' delimited pair.")
    }
    let id = elements[0];
    let payload = elements[1].to_string().into_bytes();
    Ok((id, payload))
}

/// Parse ":" delimited pair from a string into a tuple pair of (id, raw byte payload) where the
/// payload was base64 encoded in the raw string.
fn parse_binary_data(raw: &str) -> Result<(&str, Vec<u8>), Error> {
    let elements: Vec<_> = raw.split(":").collect();
    if elements.len() != 2 {
        bail!("Argument must be a ':' delimited pair.")
    }
    let id = elements[0];
    let payload = base64::decode(elements[1])?;
    Ok((id, payload))
}

fn parse_service_data(raw: &str) -> Result<ServiceDataEntry, Error> {
    let (raw_id, data) = parse_data(raw)?;
    let uuid = find_service_uuid(raw_id).map(|id| id.number).unwrap_or(raw_id).to_string();
    Ok(ServiceDataEntry { uuid, data })
}

fn parse_binary_service_data(raw: &str) -> Result<ServiceDataEntry, Error> {
    let (raw_id, data) = parse_binary_data(raw)?;
    let uuid = find_service_uuid(raw_id).map(|id| id.number).unwrap_or(raw_id).to_string();
    Ok(ServiceDataEntry { uuid, data })
}

fn parse_manufacturer_data(raw: &str) -> Result<ManufacturerSpecificDataEntry, Error> {
    let (raw_id, data) = parse_data(raw)?;
    let raw_id = if raw_id.starts_with("0x") { &raw_id[2..] } else { raw_id };
    let company_id = u16::from_str_radix(raw_id, 16)?;
    Ok(ManufacturerSpecificDataEntry { company_id, data })
}

fn parse_binary_manufacturer_data(raw: &str) -> Result<ManufacturerSpecificDataEntry, Error> {
    let (raw_id, data) = parse_binary_data(raw)?;
    let raw_id = if raw_id.starts_with("0x") { &raw_id[2..] } else { raw_id };
    let company_id = u16::from_str_radix(raw_id, 16)?;
    Ok(ManufacturerSpecificDataEntry { company_id, data })
}

/// Wrap a `Vec<T>` in an `Option`, returning `None` if the vector is empty and `Some(vec)` if the
/// vector is not empty.
fn optionalize<T>(vec: Vec<T>) -> Option<Vec<T>> {
    if vec.is_empty() {
        None
    } else {
        Some(vec)
    }
}

/// Start advertising and print status on success or construct error on failure
async fn start_advertising<'a>(
    peripheral: &'a PeripheralProxy,
    adv: &'a mut AdvertisingData,
    service_names: &'a [String],
    connectable: bool,
    interval_ms: u32,
    anonymous: bool,
) -> Result<(), Error> {
    let (status, adv_id) =
        await!(peripheral.start_advertising(adv, None, connectable, interval_ms, anonymous))?;
    if let Some(err) = status.error {
        bail!("Failed to initiate advertisement: {:?}", err);
    }
    eprintln!(
        "Advertising {} as {:?} with services: {}",
        adv_id.unwrap_or(String::new()),
        adv.name.as_ref().unwrap_or(&String::new()),
        service_names.join(", "),
    );
    Ok(())
}

fn main() -> Result<(), Error> {
    let mut exec = fasync::Executor::new().unwrap();

    // Extract arguments and perform additional transformation of incoming arguments
    let Opt {
        device_name,
        service_uuids,
        anonymous: anon,
        connectable,
        interval,
        appearance,
        uris,
        mut service_data,
        binary_service_data,
        mut manufacturer_data,
        binary_manufacturer_data,
    } = Opt::from_args();

    let service_uuids = parse_service_uuids(service_uuids);
    let service_names: Vec<_> = service_uuids.iter().map(ToString::to_string).collect();
    let service_uuids: Vec<_> =
        service_uuids.iter().map(BluetoothUUID::to_uuid).map(ToString::to_string).collect();
    service_data.extend(binary_service_data);
    manufacturer_data.extend(binary_manufacturer_data);

    // unchanging advertising data used for the lifetime of the program
    let mut adv = AdvertisingData {
        name: device_name,
        tx_power_level: None,
        appearance: appearance.map(|value| Box::new(UInt16 { value })),
        service_uuids: optionalize(service_uuids),
        service_data: optionalize(service_data),
        manufacturer_specific_data: optionalize(manufacturer_data),
        solicited_service_uuids: None,
        uris: optionalize(uris),
    };

    // connect to the peripheral service
    let peripheral = connect_to_service::<PeripheralMarker>()
        .context("failed to connect to bluetooth peripheral service")?;
    let mut events_stream = peripheral.take_event_stream();

    let main_fut = async move {
        await!(start_advertising(
            &peripheral,
            &mut adv,
            &service_names,
            connectable,
            interval,
            anon
        ))?;

        // handle central connect and disconnect events
        while let Some(evt) = await!(events_stream.try_next())? {
            use fidl_fuchsia_bluetooth_le::PeripheralEvent::*;
            match evt {
                OnCentralConnected { advertisement_id, central } => {
                    eprintln!(
                        "Connected to {} with adv_id {}",
                        central.identifier, advertisement_id
                    );
                }
                OnCentralDisconnected { device_id } => {
                    eprintln!("Disconnected from {}", device_id);
                    // start advertising again once device is disconnected
                    await!(start_advertising(
                        &peripheral,
                        &mut adv,
                        &service_names,
                        connectable,
                        interval,
                        anon
                    ))?;
                }
            };
        }
        Ok(())
    };
    exec.run_singlethreaded(main_fut)
}
