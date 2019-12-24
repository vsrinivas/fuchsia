// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::RequestStream,
    fidl_fuchsia_bluetooth_control::{
        PairingDelegateRequest, PairingDelegateRequestStream, PairingMethod,
    },
    fuchsia_async as fasync,
    fuchsia_bluetooth::error::Error as BtError,
    futures::{future, Future, TryFutureExt, TryStreamExt},
    std::io::{self, Read, Write},
};

fn print_and_flush(msg: &str) {
    print!("{}", msg);
    io::stdout().flush().unwrap();
}

macro_rules! print_and_flush {
    ($fmt:expr) => {
        print_and_flush($fmt);
    };
    ($fmt:expr, $($arg:tt),*) => {
        print_and_flush(&format!($fmt, $($arg),*));
    };
}

fn handle_confirm(val: char) -> bool {
    if val == 'y' {
        println!("Accepted pairing");
        true
    } else {
        println!("Rejected pairing");
        false
    }
}

// Prompt the user for a single character input with the given |msg|. Blocks
// until the user enters one of the characters in |allowed|.
fn prompt_for_char(msg: &str, allowed: &[char]) -> Result<char, Error> {
    print_and_flush!("{}: ", msg);
    while let Some(input) = io::stdin().bytes().next() {
        match input {
            Ok(input) => {
                let input = input as char;
                if allowed.contains(&input) {
                    println!("{}", input);
                    return Ok(input);
                }
            }
            Err(e) => {
                println!("Failed to read input: {:?}", e);
                return Err(e.into());
            }
        };
    }
    unreachable!();
}

fn prompt_for_consent() -> bool {
    let val = prompt_for_char("Accept? (y/n)", &['y', 'n']).unwrap_or_else(|err| {
        eprintln!("Failed to read input: {:#?}", err);
        'n'
    });
    handle_confirm(val)
}

fn prompt_for_comparison(passkey: Option<String>) -> bool {
    match passkey {
        None => {
            println!("No passkey provided for 'Passkey Comparison'!");
            false
        }
        Some(passkey) => {
            let msg = format!("Is the passkey '{}' displayed on device? (y/n)", passkey);
            let val = prompt_for_char(&msg, &['y', 'n']).unwrap_or_else(|err| {
                eprintln!("Failed to read input: {:#?}", err);
                'n'
            });
            handle_confirm(val)
        }
    }
}

fn prompt_for_remote_input(passkey: Option<String>) -> bool {
    match passkey {
        None => {
            println!("No passkey provided for 'Passkey Display'!");
            false
        }
        Some(passkey) => {
            println!("Enter the passkey '{}' on the peer.", passkey);
            true
        }
    }
}

fn prompt_for_local_input() -> Option<String> {
    print_and_flush!("Enter the passkey displayed on the peer (or nothing to reject): ");
    let mut input = String::new();
    match io::stdin().read_line(&mut input) {
        Ok(_) => {
            let input = input.trim().to_string();
            if input.len() == 0 {
                None
            } else {
                println!("Entered: {}", input);
                Some(input)
            }
        }
        Err(e) => {
            println!("Failed to receive passkey: {}", e);
            None
        }
    }
}

pub fn pairing_delegate(channel: fasync::Channel) -> impl Future<Output = Result<(), Error>> {
    let stream = PairingDelegateRequestStream::from_channel(channel);
    stream
        .try_for_each(move |evt| {
            match evt {
                PairingDelegateRequest::OnPairingComplete {
                    device_id,
                    status,
                    control_handle: _,
                } => {
                    println!(
                        "Pairing complete for peer (id: {}, status: {})",
                        device_id,
                        match status.error {
                            None => "success".to_string(),
                            Some(error) => format!("{}", BtError::from(*error)),
                        }
                    );
                }
                PairingDelegateRequest::OnPairingRequest {
                    device,
                    method,
                    displayed_passkey,
                    responder,
                } => {
                    println!(
                        "Pairing request from peer: {}",
                        match &device.name {
                            Some(name) => format!("{} ({})", name, &device.address),
                            None => device.address,
                        }
                    );

                    let (confirm, entered_passkey) = match method {
                        PairingMethod::Consent => (prompt_for_consent(), None),
                        PairingMethod::PasskeyComparison => {
                            (prompt_for_comparison(displayed_passkey), None)
                        }
                        PairingMethod::PasskeyDisplay => {
                            (prompt_for_remote_input(displayed_passkey), None)
                        }
                        PairingMethod::PasskeyEntry => match prompt_for_local_input() {
                            None => (false, None),
                            passkey => (true, passkey),
                        },
                    };
                    let _ = responder.send(confirm, entered_passkey.as_ref().map(String::as_ref));
                }
                PairingDelegateRequest::OnRemoteKeypress {
                    device_id,
                    keypress,
                    control_handle: _,
                } => {
                    eprintln!("Device: {} | {:?}", device_id, keypress);
                }
            };
            future::ready(Ok(()))
        })
        .map_err(|e| e.into())
}
