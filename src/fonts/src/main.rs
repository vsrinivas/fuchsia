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
    fuchsia_inspect::component::inspector,
    fuchsia_syslog::{self as syslog, *},
    fuchsia_trace as trace, fuchsia_trace_provider as trace_provider,
    std::path::PathBuf,
};

const FONT_MANIFEST_PATH: &str = "/config/data/all.font_manifest.json";
/// TODO(fxbug.dev/43936): Remove after Chromium tests are made hermetic.
const TEST_COMPATIBILITY_FONT_MANIFEST_PATH: &str =
    "/config/data/downstream_test_fonts.font_manifest.json";

/// Default capacity of the in-memory font cache when not specified in manifest.
///
/// 4 MB is enough to fit several smaller fonts; large fonts will never be cached.
///
/// TODO(fxbug.dev/48654): Listen for memory pressure events and trim cache.
const DEFAULT_CACHE_CAPACITY_BYTES: u64 = 4_000_000;

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
    trace_provider::trace_provider_create_with_fdio();
    trace::instant!("fonts", "startup", trace::Scope::Process);

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

    let font_manifest_paths = select_manifests(&args)?;

    let mut service_builder = FontServiceBuilder::with_default_asset_loader(
        DEFAULT_CACHE_CAPACITY_BYTES,
        inspector().root(),
    );
    fx_vlog!(1, "Building service with manifest(s) {:?}", &font_manifest_paths);
    for path in &font_manifest_paths {
        service_builder.add_manifest_from_file(path);
    }
    let service = service_builder.build().await.map_err(|err| {
        fx_log_err!(
            "Failed to build service with manifest(s) {:?}: {:#?}",
            &font_manifest_paths,
            &err
        );
        err
    })?;
    fx_vlog!(1, "Built service with manifest(s) {:?}", &font_manifest_paths);

    fx_vlog!(1, "Adding FIDL services");
    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(ProviderRequestStream::Stable)
        .add_fidl_service(ProviderRequestStream::Experimental);
    fs.take_and_serve_directory_handle()?;

    inspector().serve(&mut fs)?;

    let fs = fs;
    service.run(fs).await;

    Ok(())
}

/// Negotiate which manifest(s) to load.
/// TODO(fxbug.dev/43936): Remove compatibility manifest after Chromium tests are made hermetic.
fn select_manifests(args: &Args) -> Result<Vec<PathBuf>, Error> {
    let mut manifest_paths: Vec<PathBuf> = vec![];
    let main_manifest_path = match &args.font_manifest {
        Some(path) => PathBuf::from(path),
        None => PathBuf::from(FONT_MANIFEST_PATH),
    };
    if main_manifest_path.is_file() {
        manifest_paths.push(main_manifest_path);
    } else {
        fx_log_warn!(
            concat!(
                "Specified manifest file {:?} does not exist. ",
                "Looking for test compatibility manifest instead."
            ),
            main_manifest_path
        );
    }
    // Support legacy non-hermetic tests (e.g. Chromium) that expect some minimum set of fonts but
    // don't specify what it should be.
    if args.font_manifest.is_none() {
        let compatibility_manifest_path = PathBuf::from(TEST_COMPATIBILITY_FONT_MANIFEST_PATH);
        if compatibility_manifest_path.is_file() {
            manifest_paths.push(compatibility_manifest_path);
        }
    }

    if manifest_paths.is_empty() {
        Err(format_err!("Either no font manifests were specified, or they do not exist"))
    } else {
        Ok(manifest_paths)
    }
}
