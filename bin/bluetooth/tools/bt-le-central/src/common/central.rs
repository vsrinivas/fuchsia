// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(armansito): Remove this once a server channel can be killed using a Controller
#![allow(unreachable_code)]

use async::temp::Either::{Left, Right};
use bt::error::Error as BTError;
use common::gatt::start_gatt_loop;
use failure::Error;
use fidl::endpoints2;
use fidl_ble::{CentralEvent, CentralProxy, RemoteDevice};
use futures::future;
use futures::prelude::*;
use parking_lot::RwLock;
use std::fmt;
use std::process::exit;
use std::string::String;
use std::sync::Arc;

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

pub fn listen_central_events(
    state: CentralStatePtr,
) -> impl Future<Output = ()> {
    const MAX_CONCURRENT: usize = 1000;
    let evt_stream = state.read().get_svc().take_event_stream();
    evt_stream
        .try_for_each_concurrent(MAX_CONCURRENT, move |evt| {
            match evt {
                CentralEvent::OnScanStateChanged { scanning } => {
                    eprintln!("  scan state changed: {}", scanning);
                    Left(future::ready(Ok(())))
                }
                CentralEvent::OnDeviceDiscovered { device } => {
                    let id = device.identifier.clone();
                    let connectable = device.connectable;

                    eprintln!(" {}", RemoteDeviceWrapper(device));

                    let central = state.read();
                    if !central.scan_once && !central.connect {
                        return Left(future::ready(Ok(())));
                    }

                    // Stop scanning.
                    if let Err(e) = central.svc.stop_scan() {
                        eprintln!("request to stop scan failed: {}", e);
                        // TODO(armansito): kill the channel here instead
                        exit(0);
                        Left(future::ready(Ok(())))
                    } else if central.connect && connectable {
                        Right(connect_peripheral(state.clone(), id).unwrap_or_else(|_| {
                            // TODO(armansito): kill the channel here instead
                            exit(0);
                            ()
                        }).map(Ok))
                    } else {
                        // TODO(armansito): kill the channel here instead
                        exit(0);
                        Left(future::ready(Ok(())))
                    }
                }
                CentralEvent::OnPeripheralDisconnected { identifier } => {
                    eprintln!("  peer disconnected: {}", identifier);
                    // TODO(armansito): Close the channel here instead
                    exit(0);
                    Left(future::ready(Ok(())))
                }
            }
        })
        .unwrap_or_else(|e| eprintln!("failed to subscribe to BLE Central events: {:?}", e))
}

// Attempts to connect to the peripheral with the given |id| and begins the
// GATT REPL if this succeeds.
fn connect_peripheral(
    state: CentralStatePtr, mut id: String,
) -> impl Future<Output = Result<(), Error>> {
    let (proxy, server) = match endpoints2::create_endpoints() {
        Err(_) => {
            return Left(future::ready(Err(
                BTError::new("Failed to create Client pair").into(),
            )));
        }
        Ok(res) => res,
    };

    Right(
        state
            .read()
            .svc
            .connect_peripheral(&mut id, server)
            .map_err(|e| {
                BTError::new(&format!("failed to initiate connect request: {}", e)).into()
            })
            .and_then(move |status| future::ready(match status.error {
                Some(e) => {
                    println!(
                        "  failed to connect to peripheral: {}",
                        match e.description {
                            None => "unknown error",
                            Some(ref msg) => &msg,
                        }
                    );
                    Err(BTError::from(*e).into())
                }
                None => {
                    println!("  device connected: {}", id);
                    Ok(proxy)
                }
            }))
            .and_then(|proxy| start_gatt_loop(proxy)),
    )
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
