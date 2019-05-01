// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use ethernet as eth;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use std::fs::File;
use structopt::StructOpt;

const DEFAULT_ETH: &str = "/dev/class/ethernet/000";

#[derive(StructOpt, Debug)]
struct Config {
    send_byte: u8,
    receive_byte: u8,
    length: usize,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), failure::Error> {
    fuchsia_syslog::init()?;
    fuchsia_syslog::set_severity(-1);

    let config = Config::from_args();

    let dev = File::open(DEFAULT_ETH)?;
    let vmo = zx::Vmo::create_with_opts(
        zx::VmoOptions::NON_RESIZABLE,
        256 * eth::DEFAULT_BUFFER_SIZE as u64,
    )?;

    let mut eth_client =
        await!(eth::Client::from_file(dev, vmo, eth::DEFAULT_BUFFER_SIZE, "test"))?;

    await!(eth_client.start())?;

    let buf = vec![config.send_byte; config.length];
    eth_client.send(&buf);
    let mut events = eth_client.get_stream();

    // Wait for reply.
    let mut buf = vec![0; config.length];
    while let Some(evt) = await!(events.try_next())? {
        match evt {
            eth::Event::Receive(rx, flags) => {
                if flags == eth::EthernetQueueFlags::RX_OK && rx.len() == config.length {
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
