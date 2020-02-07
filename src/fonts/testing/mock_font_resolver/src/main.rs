// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        DirectoryMarker, MODE_TYPE_DIRECTORY, OPEN_FLAG_DIRECTORY, OPEN_RIGHT_READABLE,
    },
    fidl_fuchsia_pkg::{FontResolverRequest, FontResolverRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, fx_vlog, macros::*},
    fuchsia_url::pkg_url::PkgUrl,
    fuchsia_vfs_pseudo_fs::{
        directory::entry::DirectoryEntry, file::simple::read_only, pseudo_directory,
    },
    fuchsia_zircon::Status,
    futures::{StreamExt, TryStreamExt},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["mock_font_resolver"])?;
    fx_log_info!("Starting mock FontResolver service.");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn_local(async move {
            run_resolver_service(stream).await.expect("Failed to run mock FontResolver.")
        });
    });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}

async fn run_resolver_service(mut stream: FontResolverRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await? {
        fx_vlog!(1, "FontResolver got request {:?}", request);
        let FontResolverRequest::Resolve {
            package_url,
            update_policy: _,
            directory_request,
            responder,
        } = request;
        let status = resolve(package_url, directory_request).await;
        responder.send(Status::from(status).into_raw())?;
    }
    Ok(())
}

async fn resolve(
    package_url: String,
    directory_request: ServerEnd<DirectoryMarker>,
) -> Result<(), Status> {
    PkgUrl::parse(&package_url).map_err(|_| Err(Status::INVALID_ARGS))?;

    // Serve fake directories with single font files, with the selection depending on the package
    // URL. These correspond to the fake fonts declared in ../tests/*.font_manifest.json.
    let mut root = match package_url.as_ref() {
        // From ephemeral.font_manifest.json
        "fuchsia-pkg://fuchsia.com/font-package-ephemeral-ttf" => pseudo_directory! {
            "Ephemeral.ttf" => read_only(|| Ok(b"not actually a font".to_vec())),
        },

        // From aliases.font_manifest.json
        "fuchsia-pkg://fuchsia.com/font-package-alphasans-regular-ttf" => pseudo_directory! {
            "AlphaSans-Regular.ttf" => read_only(|| Ok(b"alpha".to_vec())),
        },
        "fuchsia-pkg://fuchsia.com/font-package-alphasans-condensed-ttf" => pseudo_directory! {
            "AlphaSans-Condensed.ttf" => read_only(|| Ok(b"alpha".to_vec())),
        },
        "fuchsia-pkg://fuchsia.com/font-package-alphasanshebrew-regular-ttf" => pseudo_directory! {
            "AlphaSansHebrew-Regular.ttf" => read_only(|| Ok(b"alpha".to_vec())),
        },
        _ => {
            return Err(Status::NOT_FOUND);
        }
    };

    let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DIRECTORY;
    let mode = MODE_TYPE_DIRECTORY;
    let mut path = std::iter::empty();
    let node = ServerEnd::from(directory_request.into_channel());

    root.open(flags, mode, &mut path, node);

    fasync::spawn(async move {
        root.await;
    });

    Ok(())
}
