// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use fidl_fuchsia_bluetooth_control::{PairingMethod, PairingDelegateRequest, PairingDelegateRequestStream};
use fidl::endpoints2::RequestStream;
use futures::prelude::*;
use failure::Error;
use futures::{Future};
use std::io::BufRead;
use std::io;

pub fn pairing_delegate(channel: fasync::Channel) -> impl Future<Output = Result<(), Error>> {
    let stream = PairingDelegateRequestStream::from_channel(channel);
    stream.try_for_each(move |evt| {
        match evt {
            PairingDelegateRequest::OnPairingComplete { device_id, status, control_handle: _ } => {
                eprintln!("Pairing complete for {}: {:?}", device_id, status);
            },
            PairingDelegateRequest::OnPairingRequest { device, method, displayed_passkey, responder } => {
                let mut resp_str = None;

                eprintln!("Pairing Request received from remote device: {}", device.identifier);

                let mut input = String::new();
                let stdin = io::stdin();
                let mut handle = stdin.lock();

                eprintln!("Accept? (y,N)");
                let mut resp_bool = match handle.read_line(&mut input) {
                    Ok(_) => {
                        let input = input.trim();
                        if input == "Y" || input == "y" {
                            eprintln!("Initializing pairing");
                            true
                        } else {
                            eprintln!("Pairing Rejected");
                            false
                        }
                    }
                    Err(e) => {
                        eprintln!("Declining pairing: {}", e);
                        false
                    }
                };
                match method {
                    PairingMethod::PasskeyComparison=> {
                        if let Some(remote_key) = displayed_passkey {
                            eprintln!("Remote Key: {}", remote_key);
                        }
                    },
                    PairingMethod::PasskeyDisplay => {
                        if let Some(remote_key) = displayed_passkey {
                            eprintln!("Please input code {} on peer", remote_key);
                        }
                    },
                    PairingMethod::Consent => { /* Handled in the general case */ },
                    PairingMethod::PasskeyEntry => {
                        eprintln!("Input Remote passkey:");
                        match handle.read_line(&mut input) {
                            Ok(_) => {
                                let s = input.trim().to_string();
                                resp_str = Some(s);
                            }
                            Err(_) => {
                                resp_bool = false;
                            }
                        };
                    },
                };
                let _ = responder.send(resp_bool, resp_str.as_ref().map(String::as_str));
            },
            PairingDelegateRequest::OnRemoteKeypress { device_id, keypress, control_handle: _ } => {
                eprintln!("Device: {} | {:?}", device_id, keypress);
            }
        };
        future::ready(Ok(()))
    })
    .map_err(|e| e.into())
}
