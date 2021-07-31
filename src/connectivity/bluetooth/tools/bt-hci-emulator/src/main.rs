// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, futures::future::pending, hci_emulator_client::Emulator};

fn usage(appname: &str) {
    eprintln!("usage: {}", appname);
    eprintln!("       {} --help", appname);
    eprintln!("");
    eprintln!("Instantiate and manipulate a new bt-hci device emulator");
    eprintln!(
        "examples: {}                - Instantiates a new emulator device with a random ID",
        appname
    );
}

// TODO(armansito): Add ways to pass controller settings.
#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: Vec<_> = std::env::args().collect();
    let appname = &args[0];
    match args.len() {
        1 => (),
        _ => {
            usage(appname);
            return Ok(());
        }
    };

    let emulator = Emulator::create_and_publish().await?;

    let topo_path = fdio::device_get_topo_path(&emulator.file())?;
    eprintln!("Instantiated emulator at path: {}", topo_path);

    // TODO(armansito): Instantiate a REPL here. For now we await forever to make sure that the
    // emulator device remains alive until the user terminates this program (it will be removed when
    // `emulator` drops).
    pending().await
}
