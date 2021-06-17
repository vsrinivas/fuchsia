// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_audio_dai as dai;

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let mut devices = dai::find_devices().await?;

    println!("devices found: {:?}", devices);

    for device in devices.iter_mut() {
        device.connect().expect("connection to device");
        match device.properties().await {
            Ok(properties) => println!("Device properties: {:?}", properties),
            Err(e) => println!("Couldn't get properties: {:?}", e),
        };
        println!("Supported DAI Formats:");
        match device.dai_formats().await {
            Ok(formats) => {
                for format in formats.iter() {
                    println!("{:?}", format);
                }
            }
            Err(e) => println!("Couldn't list DAI formats: {:?}", e),
        }
        println!("Supported RingBuffer Formats:");
        match device.ring_buffer_formats().await {
            Ok(formats) => {
                for format in formats.iter() {
                    println!("{:?}", format);
                }
            }
            Err(e) => println!("Couldn't list ring buffer formats: {:?}", e),
        }
    }
    Ok(())
}
