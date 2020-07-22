// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::RequestStream,
    fidl_fuchsia_bluetooth_sys::{
        PairingDelegateRequest, PairingDelegateRequestStream, PairingMethod,
    },
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::{Address, PeerId},
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

fn prompt_for_comparison(passkey: u32) -> bool {
    let msg = format!("Is the passkey '{}' displayed on device? (y/n)", passkey);
    let val = prompt_for_char(&msg, &['y', 'n']).unwrap_or_else(|err| {
        eprintln!("Failed to read input: {:#?}", err);
        'n'
    });
    handle_confirm(val)
}

fn prompt_for_remote_input(passkey: u32) -> bool {
    println!("Enter the passkey '{}' on the peer.", passkey);
    true
}

fn prompt_for_local_input() -> Option<u32> {
    print_and_flush!("Enter the passkey displayed on the peer (or nothing to reject): ");
    let mut input = String::new();
    match io::stdin().read_line(&mut input) {
        Ok(_) => {
            let input = input.trim().to_string();
            if input.len() == 0 {
                None
            } else {
                println!("Entered: {}", input);
                match input.parse::<u32>() {
                    Ok(passkey) => Some(passkey),
                    Err(_) => {
                        eprintln!("Error: passkey not an integer.");
                        None
                    }
                }
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
                PairingDelegateRequest::OnPairingComplete { id, success, control_handle: _ } => {
                    println!(
                        "Pairing complete for peer (id: {}, status: {})",
                        PeerId::from(id),
                        match success {
                            true => "success".to_string(),
                            false => "failure".to_string(),
                        }
                    );
                }
                PairingDelegateRequest::OnPairingRequest {
                    peer,
                    method,
                    displayed_passkey,
                    responder,
                } => {
                    let address = match &peer.address {
                        Some(address) => Address::from(address).to_string(),
                        None => "Unknown Address".to_string(),
                    };

                    println!(
                        "Pairing request from peer: {}",
                        match &peer.name {
                            Some(name) => format!("{} ({})", name, address),
                            None => address,
                        }
                    );

                    let (accept, entered_passkey) = match method {
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
                    let _ = responder.send(
                        accept,
                        match entered_passkey {
                            Some(passkey) => passkey,
                            // Passkey value ignored if the method is not PasskeyEntry or PasskeyEntry failed.
                            None => 0u32,
                        },
                    );
                }
                PairingDelegateRequest::OnRemoteKeypress { id, keypress, control_handle: _ } => {
                    eprintln!("Peer: {} | {:?}", PeerId::from(id), keypress);
                }
            };
            future::ready(Ok(()))
        })
        .map_err(|e| e.into())
}
