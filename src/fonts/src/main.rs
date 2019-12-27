// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Main entry point for the font service.

#![warn(missing_docs)]

mod font_service;

use {
    self::font_service::{FontServiceBuilder, ProviderRequestStream},
    anyhow::{format_err, Error},
    argh::FromArgs,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, *},
    std::path::PathBuf,
};

const FONT_MANIFEST_PATH: &str = "/config/data/all.font_manifest.json";

#[derive(FromArgs)]
/// Font Server
struct Args {
    /// load fonts from the specified font manifest file instead of the default
    #[argh(option, short = 'm')]
    font_manifest: Option<String>,
    /// no-op, deprecated
    #[argh(switch, short = 'n')]
    no_default_fonts: bool,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["fonts"])?;

    // We have to convert legacy uses of "--font-manifest=<PATH>" to "--font-manifest <PATH>".
    let arg_strings: Vec<String> = std::env::args()
        .collect::<Vec<String>>()
        .iter()
        .flat_map(|s| s.split("=").map(|s| s.to_owned()))
        .collect();
    let arg_strs: Vec<&str> = arg_strings.iter().map(|s| s.as_str()).collect();

    let args: Args = Args::from_args(&[arg_strs[0]], &arg_strs[1..])
        .map_err(|early_exit| format_err!("{}", early_exit.output))?;

    if args.no_default_fonts {
        fx_log_warn!("--no-default-fonts is deprecated and is treated as a no-op")
    }

    let mut service_builder = FontServiceBuilder::new();

    let font_manifest_path = match args.font_manifest {
        Some(path) => PathBuf::from(path),
        None => PathBuf::from(FONT_MANIFEST_PATH),
    };

    fx_vlog!(1, "Building service with manifest {:?}", &font_manifest_path);
    service_builder.add_manifest_from_file(&font_manifest_path);
    let service = service_builder.build().await.map_err(|err| {
        fx_log_err!("Failed to build service with manifest {:?}: {:#?}", &font_manifest_path, &err);
        err
    })?;
    fx_vlog!(1, "Built service with manifest {:?}", &font_manifest_path);

    fx_vlog!(1, "Adding FIDL services");
    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(ProviderRequestStream::Stable)
        .add_fidl_service(ProviderRequestStream::Experimental);
    fs.take_and_serve_directory_handle()?;
    let fs = fs;

    service.run(fs).await;

    Ok(())
}
