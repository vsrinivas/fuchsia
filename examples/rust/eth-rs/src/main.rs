// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::prelude::*;
use std::env;
use std::fs::File;

fn main() -> Result<(), Error> {
    let vmo = zx::Vmo::create_with_opts(zx::VmoOptions::NON_RESIZABLE, 256 * ethernet::DEFAULT_BUFFER_SIZE as u64)?;

    let mut executor = fasync::Executor::new().context("could not create executor")?;

    let path = env::args().nth(1).expect("missing device argument");
    let dev = File::open(path)?;
    let client = ethernet::Client::new(dev, vmo, ethernet::DEFAULT_BUFFER_SIZE, "eth-rs")?;
    println!("created client {:?}", client);
    println!("info: {:?}", client.info()?);
    println!("status: {:?}", client.get_status()?);
    client.start()?;
    client.tx_listen_start()?;

    let stream = client.get_stream().try_for_each(|evt| {
        println!("{:?}", evt);
        match evt {
            ethernet::Event::Receive(rx) => {
                let mut buf = [0; 64];
                let r = rx.read(&mut buf);
                println!("first {} bytes: {:02x?}", r, &buf[0..r]);
            },
            _ => (),
        }
        futures::future::ready(Ok(()))
    });
    executor.run_singlethreaded(stream).map(|_| ())?;
    Ok(())
}
