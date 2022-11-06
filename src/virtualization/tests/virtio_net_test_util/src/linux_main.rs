// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use std::fs;
use std::io::{Error, ErrorKind};
use std::net::UdpSocket;
use std::path::Path;
use structopt::StructOpt;

const NET_DIR: &str = "/sys/class/net";

#[derive(StructOpt, Debug)]
enum Command {
    /// Find the name of network interface with the given MAC address.
    Find { mac_address: String },
    /// Transfer a buffer of <length> containing <send_byte> and wait for a response containing
    /// <receive_byte>.
    Transfer { send_byte: u8, receive_byte: u8, length: usize },
    /// Transfer a buffer of <length> initially containing <start_byte>, and wait for the same
    /// byte to be echoed in response. The target byte is then incremented, and the transfer is
    /// repeated for <iterations>.
    RotatingTransfer { start_byte: u8, iterations: u32, length: usize },
}

fn get_interface(mac_address: String) -> std::io::Result<String> {
    let dir = Path::new(NET_DIR);
    if !dir.is_dir() {
        return Err(Error::new(ErrorKind::Other, format!("{} is not a directory", NET_DIR)));
    }
    for entry in fs::read_dir(dir)?.collect::<std::io::Result<Vec<_>>>()? {
        let path = entry.path().join("address");
        let address = fs::read_to_string(path)?;
        if address.trim().to_lowercase() == mac_address.trim().to_lowercase() {
            return entry
                .file_name()
                .into_string()
                .or(Err(Error::new(ErrorKind::Other, "Failed to read device name")));
        }
    }
    Err(Error::new(ErrorKind::NotFound, "Could not find interface"))
}

// Generate all 255 packets which will be used in this test. The test will loop through these
// packets many times, making reallocation inefficient.
fn generate_all_possible_packets(length: usize) -> Vec<Vec<u8>> {
    let mut packets = Vec::with_capacity(u8::MAX.into());
    for i in 0..=u8::MAX {
        packets.push(vec![i; length]);
    }

    packets
}

fn main() -> std::io::Result<()> {
    match Command::from_args() {
        Command::Find { mac_address } => {
            let interface = get_interface(mac_address)?;
            println!("{:}", interface);
        }
        Command::Transfer { send_byte, receive_byte, length } => {
            // Bind the socket to all addresses, on port 4242.
            let socket = UdpSocket::bind(("0.0.0.0", 4242))?;

            // Send to 192.168.0.1, the IPv4 address of the host.
            let send_buf = vec![send_byte; length];
            socket.send_to(&send_buf, ("192.168.0.1", 4242))?;

            let mut recv_buf = vec![0; length];
            let actual = socket.recv(&mut recv_buf)?;

            if actual == length && recv_buf.iter().all(|b| *b == receive_byte) {
                println!("PASS");
            }
        }
        Command::RotatingTransfer { mut start_byte, mut iterations, length } => {
            // Bind the socket to all addresses, on port 4242.
            let socket = UdpSocket::bind(("0.0.0.0", 4242))?;

            let mut recv_buf = vec![0; length];
            let send_packets = generate_all_possible_packets(length);
            while iterations > 0 {
                socket.send_to(&send_packets[start_byte as usize], ("192.168.0.1", 4242))?;
                let actual = socket.recv(&mut recv_buf)?;
                if actual != length || recv_buf.iter().any(|b| *b != start_byte) {
                    println!("FAIL");
                    return Ok(());
                }

                let (new_byte, _) = start_byte.overflowing_add(1);
                start_byte = new_byte;
                iterations -= 1;
            }

            println!("PASS");
        }
    }
    Ok(())
}
