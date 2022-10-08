// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    config::Config,
    fidl_test_exampletester::{SimpleMarker, SimpleSynchronousProxy},
    fuchsia_component::client::connect_channel_to_protocol,
    fuchsia_zircon as zx,
    std::{thread, time},
};

fn main() -> Result<(), Error> {
    println!("Started");
    println!("trim me (Rust)");

    let config = Config::take_from_startup_handle();

    // Only try to contact the server if instructed - if not, do the calculation locally instead.
    if config.do_in_process {
        println!("Response: {:?}", config.augend + config.addend);
    } else {
        let (client_end, server_end) = zx::Channel::create()?;
        connect_channel_to_protocol::<SimpleMarker>(server_end)
            .context("Failed to connect to simple service")?;
        println!("Outgoing connection enabled");

        let simple = SimpleSynchronousProxy::new(client_end);
        let res = simple.add(config.augend, config.addend, zx::Time::INFINITE)?;
        println!("Response: {:?}", res);
    }

    // TODO(fxbug.dev/76579): We need to sleep here to make sure all logs get drained. Once the
    // referenced bug has been resolved, we can remove the sleep.
    thread::sleep(time::Duration::from_secs(2));
    Ok(())
}
