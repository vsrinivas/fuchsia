// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(armansito): Remove this once a server channel can be killed using a Controller
#![allow(unreachable_code)]

use {
    anyhow::Error,
    fidl::endpoints,
    fidl_fuchsia_bluetooth_le::{CentralEvent, CentralProxy},
    fuchsia_bluetooth::{error::Error as BTError, types::le::RemoteDevice},
    futures::prelude::*,
    parking_lot::RwLock,
    std::{convert::TryFrom, process::exit, sync::Arc},
};

use crate::gatt::repl::start_gatt_loop;

pub type CentralStatePtr = Arc<RwLock<CentralState>>;

pub struct CentralState {
    // If `Some(n)`, stop scanning and close the delegate handle after n more scan results.
    pub remaining_scan_results: Option<u64>,

    // If true, attempt to connect to the first scan result.
    pub connect: bool,

    // The proxy that we use to perform LE central requests.
    svc: CentralProxy,
}

impl CentralState {
    pub fn new(proxy: CentralProxy) -> CentralStatePtr {
        Arc::new(RwLock::new(CentralState {
            remaining_scan_results: None,
            connect: false,
            svc: proxy,
        }))
    }

    pub fn get_svc(&self) -> &CentralProxy {
        &self.svc
    }

    /// If the remaining_scan_results is specified, decrement until it reaches 0.
    /// Scanning should continue if the user is not attempted to connect to a device
    /// and there are remaining scans left to run.
    ///
    /// return: true if scanning should continue
    ///         false if scanning should stop
    pub fn decrement_scan_count(&mut self) -> bool {
        self.remaining_scan_results = self.remaining_scan_results.map(
            // decrement until n is 0
            |n| if n > 0 { n - 1 } else { n },
        );
        // scanning should continue if connection will not be attempted and
        // there are remaining scan results
        !(self.connect || self.remaining_scan_results.iter().any(|&n| n == 0))
    }
}

pub async fn listen_central_events(state: CentralStatePtr) {
    const MAX_CONCURRENT: usize = 1000;
    let evt_stream = state.read().get_svc().take_event_stream();
    let state = &state;
    let for_each_fut = evt_stream.try_for_each_concurrent(MAX_CONCURRENT, move |evt| {
        async move {
            match evt {
                CentralEvent::OnScanStateChanged { scanning } => {
                    eprintln!("  scan state changed: {}", scanning);
                    Ok(())
                }
                CentralEvent::OnDeviceDiscovered { device } => {
                    let device = match RemoteDevice::try_from(device) {
                        Ok(d) => d,
                        Err(e) => {
                            eprintln!("received malformed scan result: {}", e);
                            exit(0);
                        }
                    };
                    let id = device.identifier.clone();

                    eprintln!(" {}", device);

                    let mut central = state.write();
                    if central.decrement_scan_count() {
                        // Continue scanning
                        return Ok(());
                    }

                    // Stop scanning.
                    if let Err(e) = central.svc.stop_scan() {
                        eprintln!("request to stop scan failed: {}", e);
                        // TODO(armansito): kill the channel here instead
                        exit(0);
                        Ok(())
                    } else if central.connect && device.connectable {
                        // Drop lock so it isn't held during .await
                        drop(central);
                        match connect_peripheral(state, id).await {
                            Ok(()) => Ok(()),
                            Err(_) => Ok(()),
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
        }
    });

    if let Err(e) = for_each_fut.await {
        eprintln!("failed to subscribe to BLE Central events: {:?}", e);
    }
}

// Attempts to connect to the peripheral with the given |id| and begins the
// GATT REPL if this succeeds.
pub async fn connect_peripheral(state: &CentralStatePtr, mut id: String) -> Result<(), Error> {
    let (proxy, server) =
        endpoints::create_proxy().map_err(|_| BTError::new("Failed to create Client pair"))?;

    let connect_peripheral_fut = state.read().svc.connect_peripheral(&mut id, server);

    let status = connect_peripheral_fut
        .await
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

    start_gatt_loop(proxy).await?;
    Ok(())
}
