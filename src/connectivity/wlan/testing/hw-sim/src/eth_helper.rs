// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fdio, files_async::readdir, fuchsia_async, fuchsia_zircon as zx, std::path::Path,
    wlan_dev::IsolatedDeviceEnv,
};

pub async fn create_eth_client(mac: &[u8; 6]) -> Result<Option<ethernet::Client>, failure::Error> {
    const ETH_PATH: &str = "class/ethernet";
    let eth_dir = IsolatedDeviceEnv::open_dir(ETH_PATH).expect("opening ethernet dir");
    let directory_proxy = fidl_fuchsia_io::DirectoryProxy::new(
        fuchsia_async::Channel::from_channel(fdio::clone_channel(&eth_dir)?)?,
    );
    let files = readdir(&directory_proxy).await?;
    for file in files {
        let vmo = zx::Vmo::create(256 * ethernet::DEFAULT_BUFFER_SIZE as u64)?;

        let path = Path::new(ETH_PATH).join(file.name);
        let dev = IsolatedDeviceEnv::open_file(path)?;
        if let Ok(client) = ethernet::Client::from_file(
            dev,
            vmo,
            ethernet::DEFAULT_BUFFER_SIZE,
            "wlan-hw-sim"
        ).await {
            if let Ok(info) = client.info().await {
                if &info.mac.octets == mac {
                    println!("ethernet client created: {:?}", client);
                    client.start().await.expect("error starting ethernet device");
                    // must call get_status() after start() to clear zx::Signals::USER_0 otherwise
                    // there will be a stream of infinite StatusChanged events that blocks
                    // fasync::Interval
                    println!(
                        "info: {:?} status: {:?}",
                        client.info().await.expect("calling client.info()"),
                        client.get_status().await.expect("getting client status()")
                    );
                    return Ok(Some(client));
                }
            }
        }
    }
    Ok(None)
}
