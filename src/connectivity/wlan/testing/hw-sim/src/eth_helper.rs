// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_zircon as zx,
    std::fs::{self, File},
};

pub async fn create_eth_client(mac: &[u8; 6]) -> Result<Option<ethernet::Client>, failure::Error> {
    const ETH_PATH: &str = "/dev/class/ethernet";
    let files = fs::read_dir(ETH_PATH)?;
    for file in files {
        let vmo = zx::Vmo::create(256 * ethernet::DEFAULT_BUFFER_SIZE as u64)?;

        let path = file?.path();
        let dev = File::open(path)?;
        if let Ok(client) = await!(ethernet::Client::from_file(
            dev,
            vmo,
            ethernet::DEFAULT_BUFFER_SIZE,
            "wlan-hw-sim"
        )) {
            if let Ok(info) = await!(client.info()) {
                if &info.mac.octets == mac {
                    println!("ethernet client created: {:?}", client);
                    await!(client.start()).expect("error starting ethernet device");
                    // must call get_status() after start() to clear
                    // zx::Signals::USER_0 otherwise there will be a stream
                    // of infinite StatusChanged events that blocks
                    // fasync::Interval
                    println!(
                        "info: {:?} status: {:?}",
                        await!(client.info()).expect("calling client.info()"),
                        await!(client.get_status()).expect("getting client status()")
                    );
                    return Ok(Some(client));
                }
            }
        }
    }
    Ok(None)
}
