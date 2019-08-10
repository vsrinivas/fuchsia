// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

mod font_service;

use {
    self::font_service::{FontService, ProviderRequestStream},
    failure::Error,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    getopts,
    std::path::PathBuf,
};

#[macro_use]
extern crate fuchsia_syslog as syslog;

const FONT_MANIFEST_PATH: &str = "/pkg/data/manifest.json";
const VENDOR_FONT_MANIFEST_PATH: &str = "/config/data/fonts/manifest.json";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["fonts"])?;

    let mut opts = getopts::Options::new();

    opts.optflag("h", "help", "")
        .optmulti(
            "m",
            "font-manifest",
            "Load fonts from the specified font manifest file.",
            "MANIFEST",
        )
        .optflag("n", "no-default-fonts", "Don't load fonts from default location.");

    let args: Vec<String> = std::env::args().collect();
    let options = opts.parse(args)?;

    let mut service = FontService::new();

    if !options.opt_present("n") {
        service.load_manifest(&PathBuf::from(FONT_MANIFEST_PATH))?;

        let font_manifest_path = PathBuf::from(VENDOR_FONT_MANIFEST_PATH);
        if font_manifest_path.exists() {
            service.load_manifest(&font_manifest_path)?;
        }
    } else {
        fx_vlog!(1, "no-default-fonts set, not loading fonts from default location");
    }

    for m in options.opt_strs("m") {
        service.load_manifest(&PathBuf::from(m.as_str()))?;
    }

    service.check_can_start()?;

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
