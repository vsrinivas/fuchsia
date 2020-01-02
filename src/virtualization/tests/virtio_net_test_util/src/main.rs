// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ethernet as eth;
use fidl_fuchsia_hardware_ethernet_ext::MacAddress;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use std::fs::{self, File};
use std::str::FromStr;
use structopt::StructOpt;

const ETH_DIRECTORY: &str = "/dev/class/ethernet";

#[derive(StructOpt, Debug)]
struct Config {
    send_byte: u8,
    receive_byte: u8,
    length: usize,
    mac: String,
}

async fn find_ethernet_device(mac: MacAddress) -> Result<eth::Client, anyhow::Error> {
    let eth_devices = fs::read_dir(ETH_DIRECTORY)?;

    for device in eth_devices {
        let dev = File::open(device?.path().to_str().unwrap())?;
        let vmo = zx::Vmo::create(256 * eth::DEFAULT_BUFFER_SIZE as u64)?;

        let eth_client =
            eth::Client::from_file(dev, vmo, eth::DEFAULT_BUFFER_SIZE as u64, "test").await?;

        eth_client.start().await?;

        let eth_info = eth_client.info().await?;
        if eth_info.mac == mac {
            return Ok(eth_client);
        }
    }

    return Err(anyhow::format_err!("Could not find {}", mac));
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init()?;
    fuchsia_syslog::set_severity(-1);

    let config = Config::from_args();
    let mut eth_client = find_ethernet_device(MacAddress::from_str(&config.mac)?).await?;

    let buf = vec![config.send_byte; config.length];
    eth_client.send(&buf);
    let mut events = eth_client.get_stream();

    // Wait for reply.
    let mut buf = vec![0; config.length];
    while let Some(evt) = events.try_next().await? {
        match evt {
            eth::Event::Receive(rx, flags) => {
                if flags == eth::EthernetQueueFlags::RX_OK && rx.len() == config.length as u64 {
                    rx.read(&mut buf);
                    break;
                }
            }
            _ => (),
        }
    }

    if buf.iter().all(|b| *b == config.receive_byte) {
        println!("PASS");
    }
    Ok(())
}
