// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(armansito): Remove this once a server channel can be killed using a Controller
#![allow(unreachable_code)]

use {
    crate::common::gatt::start_gatt_loop,
    failure::Error,
    fidl::endpoints2,
    fidl_fuchsia_bluetooth_le::{CentralEvent, CentralProxy, RemoteDevice},
    fuchsia_bluetooth::error::Error as BTError,
    futures::{
        prelude::*,
    },
    parking_lot::RwLock,
    std::{
        fmt,
        process::exit,
        sync::Arc,
    },
};

type CentralStatePtr = Arc<RwLock<CentralState>>;

pub struct CentralState {
    // If true, stop scanning and close the delegate handle after the first scan result.
    pub scan_once: bool,

    // If true, attempt to connect to the first scan result.
    pub connect: bool,

    // The proxy that we use to perform LE central requests.
    svc: CentralProxy,
}

impl CentralState {
    pub fn new(proxy: CentralProxy) -> CentralStatePtr {
        Arc::new(RwLock::new(CentralState {
            scan_once: false,
            connect: false,
            svc: proxy,
        }))
    }

    pub fn get_svc(&self) -> &CentralProxy {
        &self.svc
    }
}

pub async fn listen_central_events(
    state: CentralStatePtr,
) {
    const MAX_CONCURRENT: usize = 1000;
    let evt_stream = state.read().get_svc().take_event_stream();
    let state = &state;
    let for_each_fut = evt_stream
        .try_for_each_concurrent(MAX_CONCURRENT, async move |evt| {
            match evt {
                CentralEvent::OnScanStateChanged { scanning } => {
                    eprintln!("  scan state changed: {}", scanning);
                    Ok(())
                }
                CentralEvent::OnDeviceDiscovered { device } => {
                    let id = device.identifier.clone();
                    let connectable = device.connectable;

                    eprintln!(" {}", RemoteDeviceWrapper(device));

                    let central = state.read();
                    if !central.scan_once && !central.connect {
                        return Ok(());
                    }

                    // Stop scanning.
                    if let Err(e) = central.svc.stop_scan() {
                        eprintln!("request to stop scan failed: {}", e);
                        // TODO(armansito): kill the channel here instead
                        exit(0);
                        Ok(())
                    } else if central.connect && connectable {
                        // Drop lock so it isn't held during await!
                        drop(central);
                        match await!(connect_peripheral(state, id)) {
                            Ok(()) => Ok(()),
                            Err(_) =>
                                // TODO(armansito): kill the channel here instead
                                exit(0),
                        }
                    } else {
                        exit(0)
                    }
                }
                CentralEvent::OnPeripheralDisconnected { identifier } => {
                    eprintln!("  peer disconnected: {}", identifier);
                    // TODO(armansito): Close the channel here instead
                    exit(0)
                }
            }
        });

    if let Err(e) = await!(for_each_fut) {
        eprintln!("failed to subscribe to BLE Central events: {:?}", e);
    }
}

// Attempts to connect to the peripheral with the given |id| and begins the
// GATT REPL if this succeeds.
async fn connect_peripheral(
    state: &CentralStatePtr, mut id: String,
) -> Result<(), Error> {
    let (proxy, server) = endpoints2::create_endpoints()
        .map_err(|_| BTError::new("Failed to create Client pair"))?;

    let connect_peripheral_fut = state.read()
            .svc
            .connect_peripheral(&mut id, server);

    let status = await!(connect_peripheral_fut)
        .map_err(|e| BTError::new(&format!("failed to initiate connect request: {}", e)))?;

    match status.error {
        Some(e) => {
            println!(
                "  failed to connect to peripheral: {}",
                match &e.description {
                    None => "unknown error",
                    Some(msg) => msg,
                }
            );
            return Err(BTError::from(*e).into());
        }
        None => {
            println!("  device connected: {}", id);
        }
    }

    await!(start_gatt_loop(proxy))?;
    Ok(())
}

struct RemoteDeviceWrapper(RemoteDevice);

impl fmt::Display for RemoteDeviceWrapper {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let connectable = if self.0.connectable {
            "conn"
        } else {
            "non-conn"
        };

        write!(f, "[device({}), ", connectable)?;

        if let Some(ref rssi) = self.0.rssi {
            write!(f, "rssi: {}, ", rssi.value)?;
        }

        if let Some(ref ad) = self.0.advertising_data {
            if let Some(ref name) = ad.name {
                write!(f, "{}, ", name)?;
            }
        }

        write!(f, "id: {}]", self.0.identifier)
    }
}
