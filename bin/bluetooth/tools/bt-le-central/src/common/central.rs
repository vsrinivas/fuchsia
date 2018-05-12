// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(armansito): Remove this once a server channel can be killed using a Controller
#![allow(unreachable_code)]

use async;

use bt::error::Error as BTError;
use common::gatt::{create_client_pair, start_gatt_loop};
use failure::Error;
use fidl_ble::{CentralDelegate, CentralDelegateImpl, CentralProxy, RemoteDevice};
use futures::future;
use futures::future::Either::{Left, Right};
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

pub fn make_central_delegate(state: CentralStatePtr, channel: async::Channel)
    -> impl Future<Item = (), Error = Never> + Send {
    CentralDelegateImpl {
        state: state,
        on_open: |_, _| future::ok(()),

        on_scan_state_changed: |_, scanning: bool, _| {
            println!("  scan state changed: {}", scanning);
            future::ok(())
        },

        on_device_discovered: |ref mut state, device: RemoteDevice, _| {
            let id = device.identifier.clone();
            let connectable = device.connectable;
            println!(" {}", RemoteDeviceWrapper(device));

            // TODO(armansito): async/await plz
            let central = state.read();
            if central.scan_once || central.connect {
                // Stop scanning.
                if let Err(e) = central.svc.stop_scan() {
                    eprintln!("request to stop scan failed: {}", e);

                    // TODO(armansito): kill the channel here instead
                    exit(0);
                    Left(future::ok(()))
                } else if central.connect && connectable {
                    Right(connect_peripheral(Arc::clone(state), id).recover(|_| {
                        // TODO(armansito): kill the channel here instead
                        exit(0);
                        ()
                    }))
                } else {
                    // TODO(armansito): kill the channel here instead
                    exit(0);
                    Left(future::ok(()))
                }
            } else {
                Left(future::ok(()))
            }
        },

        on_peripheral_disconnected: |_, id: String, _| {
            println!("  peer disconnected: {}", id);
            // TODO(armansito): Close the channel here instead
            exit(0);
            future::ok(())
        },
    }.serve(channel)
        .recover(|e| eprintln!("error running BLE Central delegate {:?}", e))
}

// Attempts to connect to the peripheral with the given |id| and begins the
// GATT REPL if this succeeds.
fn connect_peripheral(state: CentralStatePtr, mut id: String)
    -> impl Future<Item = (), Error = Error> {
    let (proxy, server) = match create_client_pair() {
        Err(_) => {
            return Left(future::err(
                BTError::new("Failed to create Client pair").into(),
            ));
        }
        Ok(res) => res,
    };

    Right(
        state
            .read()
            .svc
            .connect_peripheral(&mut id, server)
            .map_err(|e| {
                BTError::new(&format!("failed to initiaate connect request: {}", e)).into()
            })
            .and_then(move |status| match status.error {
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
            })
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
