// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api)]

mod font_service;
mod manifest;

use self::font_service::FontService;
use failure::{Error, ResultExt, format_err};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_fonts::ProviderMarker as FontProviderMarker;
use fuchsia_app::server::ServicesServer;
use std::path::PathBuf;
use std::sync::Arc;
use getopts;

const FONT_MANIFEST_PATH: &str = "/pkg/data/manifest.json";
const VENDOR_FONT_MANIFEST_PATH: &str = "/system/data/vendor/fonts/manifest.json";

fn main() -> Result<(), Error> {
    let mut opts = getopts::Options::new();

    opts.optflag("h", "help", "")
    .optmulti(
        "m",
        "font-manifest",
        "Load fonts from the specified font manifest file.",
        "MANIFEST"
    )
    .optflag(
        "n",
        "no-default-fonts",
        "Don't load fonts from default location."
    );

    let args: Vec<String> = std::env::args().collect();
    let options = opts.parse(args)?;

    let mut manifests = Vec::new();

    if !options.opt_present("n") {
        manifests.push(PathBuf::from(FONT_MANIFEST_PATH));

        let vendor_font_manifest_path = PathBuf::from(VENDOR_FONT_MANIFEST_PATH);
        if vendor_font_manifest_path.exists() {
            manifests.push(vendor_font_manifest_path);
        }
    }

    for m in options.opt_strs("m") {
        manifests.push(PathBuf::from(m.as_str()));
    }

    if manifests.is_empty() {
        return Err(format_err!("At least manifest file is expected."));
    }

    let mut executor = fuchsia_async::Executor::new()
        .context("Creating async executor for Font service failed")?;

    let service = Arc::new(FontService::new(manifests)?);


    let fut = ServicesServer::new()
        .add_service((FontProviderMarker::NAME, move |channel| {
            font_service::spawn_server(service.clone(), channel)
        })).start()
        .context("Creating ServicesServer for Font service failed")?;
    executor
        .run_singlethreaded(fut)
        .context("Attempt to start up Font service on async::Executor failed")?;
    Ok(())
}
