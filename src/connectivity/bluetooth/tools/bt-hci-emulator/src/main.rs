// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_bluetooth::hci_emulator::Emulator,
    futures::future::pending,
    rand::{self, Rng},
};

fn usage(appname: &str) {
    eprintln!("usage: {}", appname);
    eprintln!("       {} DEVICE_NAME", appname);
    eprintln!("       {} --help", appname);
    eprintln!("");
    eprintln!("Instantiate and manipulate a new bt-hci device emulator");
    eprintln!(
        "examples: {}                - Instantiates a new emulator device with a random ID",
        appname
    );
    eprintln!(
        "examples: {} my-device-name - Instantiates a new emulator device named \"my-device-name\"",
        appname
    );
}

// TODO(armansito): Add ways to pass controller settings.
#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: Vec<_> = std::env::args().collect();
    let appname = &args[0];
    let device_name = match args.len() {
        1 => {
            let mut rng = rand::thread_rng();
            format!("bt-hci-emulator-{:X}", rng.gen::<u32>())
        }
        2 => {
            let arg = &args[1];
            if arg == "--help" {
                usage(appname);
                return Ok(());
            }
            arg.clone()
        }
        _ => {
            usage(appname);
            return Ok(());
        }
    };

    let _emulator = Emulator::create_and_publish(&device_name).await?;
    eprintln!("Instantiated emulator named {}", device_name);

    // TODO(armansito): Instantiate a REPL here. For now we await forever to make sure that the
    // emulator device remains alive until the user terminates this program (it will be removed when
    // `emulator` drops).
    pending().await
}
