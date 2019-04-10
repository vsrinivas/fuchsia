// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api)]

mod collection;
mod font_info;
mod font_service;
mod freetype_ffi;
mod manifest;

use self::font_service::FontService;
use failure::{Error, ResultExt};
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use getopts;
use std::path::PathBuf;
use std::sync::Arc;

const FONT_MANIFEST_PATH: &str = "/pkg/data/manifest.json";
const VENDOR_FONT_MANIFEST_PATH: &str = "/system/data/vendor/fonts/manifest.json";

fn main() -> Result<(), Error> {
    let mut opts = getopts::Options::new();

    opts.optflag("h", "help", "")
        .optmulti(
            "m",
            "font-manifest",
            "Load fonts from the specified font manifest file.",
            "MANIFEST",
        )
        .optflag(
            "n",
            "no-default-fonts",
            "Don't load fonts from default location.",
        );

    let args: Vec<String> = std::env::args().collect();
    let options = opts.parse(args)?;

    let mut service = FontService::new();

    if !options.opt_present("n") {
        service.load_manifest(&PathBuf::from(FONT_MANIFEST_PATH))?;

        let vendor_font_manifest_path = PathBuf::from(VENDOR_FONT_MANIFEST_PATH);
        if vendor_font_manifest_path.exists() {
            service.load_manifest(&vendor_font_manifest_path)?;
        }
    }

    for m in options.opt_strs("m") {
        service.load_manifest(&PathBuf::from(m.as_str()))?;
    }

    service.check_can_start()?;

    let service = Arc::new(service);

    let mut executor = fuchsia_async::Executor::new()
        .context("Creating async executor for Font service failed")?;

    let mut fs = ServiceFs::new();
    fs.dir("public")
        .add_fidl_service(move |stream| font_service::spawn_server(service.clone(), stream));
    fs.take_and_serve_directory_handle()?;
    let () = executor.run_singlethreaded(fs.collect());
    Ok(())
}
