// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_sys::{
        PairingDelegateRequest, PairingDelegateRequestStream, PairingMethod,
    },
    fuchsia_bluetooth::types::{Address, PeerId},
    futures::{channel::mpsc::Sender, StreamExt},
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
    let mut passphrase = String::new();
    while let Some(input) = io::stdin().bytes().next() {
        match input {
            Ok(input) => {
                if input != 13 {
                    // Keep reading user's input until enter key is pressed.
                    print_and_flush!("{}", (input as char));
                    passphrase.push(input as char);
                    continue;
                }
                println!("");
                match passphrase.parse::<u32>() {
                    Ok(passkey) => return Some(passkey),
                    Err(_) => {
                        eprintln!("Error: passkey not an integer.");
                        return None;
                    }
                }
            }
            Err(e) => {
                println!("Failed to receive passkey: {:?}", e);
                return None;
            }
        }
    }
    unreachable!();
}

/// Handles requests from the `PairingDelegateRequestStream`, prompting for
/// user input when necessary. Signals the status of an `OnPairingComplete`
/// event using the provided  sig_channel.
pub async fn handle_requests(
    mut stream: PairingDelegateRequestStream,
    mut sig_channel: Sender<(PeerId, bool)>,
) -> Result<(), Error> {
    while let Some(req) = stream.next().await {
        match req {
            Ok(event) => match event {
                PairingDelegateRequest::OnPairingComplete { id, success, control_handle: _ } => {
                    println!(
                        "Pairing complete for peer (id: {}, status: {})",
                        PeerId::from(id),
                        match success {
                            true => "success".to_string(),
                            false => "failure".to_string(),
                        }
                    );
                    sig_channel.try_send((id.into(), success))?;
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
            },
            Err(e) => return Err(format_err!("error encountered {:?}", e)),
        };
    }
    Err(format_err!("PairingDelegate channel closed (likely due to pre-existing PairingDelegate)"))
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_bluetooth_sys::PairingDelegateMarker, fuchsia_async as fasync,
        futures::channel::mpsc::channel,
    };

    #[fuchsia::test]
    async fn test_pairing_delegate() {
        let (pairing_delegate_client, pairing_delegate_server_stream) =
            fidl::endpoints::create_request_stream::<PairingDelegateMarker>().unwrap();
        let (sig_sender, _sig_receiver) = channel(0);
        let pairing_server = handle_requests(pairing_delegate_server_stream, sig_sender);
        let delegate_server_task = fasync::Task::spawn(async move { pairing_server.await });

        std::mem::drop(pairing_delegate_client); // drop client to make channel close

        // Closing the client causes the server to exit.
        let _ = delegate_server_task.await.expect_err("should have returned error");
    }
}
