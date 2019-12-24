// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use structopt::StructOpt;

use fuchsia_syslog::fx_log_info;

#[derive(StructOpt)]
struct Opt {
    #[structopt(name = "FILE", parse(from_os_str))]
    file: std::path::PathBuf,
}

fn main() -> Result<(), anyhow::Error> {
    let opt = Opt::from_args();

    fuchsia_syslog::init_with_tags(&["tcp_file_sender"])?;
    fx_log_info!("Starting tcp file sender.");

    let metadata = std::fs::metadata(&opt.file)?;
    if !metadata.is_file() {
        return Err(anyhow::format_err!("cannot serve non-file {}", opt.file.display()));
    }

    let address = "[::]:80";
    let listener = std::net::TcpListener::bind(&address)?;

    fx_log_info!("Listening on: {}", address);

    for stream in listener.incoming() {
        let mut file = std::fs::File::open(&opt.file)
            .with_context(|| format!("could not open {}", opt.file.display()))?;

        std::io::copy(&mut file, &mut stream?)?;
    }

    Ok(())
}
