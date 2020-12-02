// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_test_pkg_thinger::{ThingerRequest, ThingerRequestStream},
    fuchsia_async as fasync,
    fuchsia_async::futures::StreamExt,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::prelude::*,
};

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["reflector", "thinger"]).expect("syslog init should not fail");
    main_inner().await.unwrap_or_else(|e| fx_log_err!("error running thinger: {:#}", e));
}

async fn main_inner() -> Result<(), Error> {
    fx_log_info!("starting thinger service");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(
            do_thing(stream).unwrap_or_else(|e| panic!("failed to process things: {:?}", e)),
        )
        .detach()
    });

    fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;

    fs.collect::<()>().await;

    Ok(())
}

async fn do_thing(mut stream: ThingerRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("getting request from stream")? {
        match request {
            ThingerRequest::DoThing { responder } => {
                fx_log_info!("listing /pkgfs");

                let dir = io_util::open_directory_in_namespace(
                    "/pkgfs",
                    fidl_fuchsia_io::OPEN_RIGHT_READABLE,
                )
                .context("failed to open /pkgfs")?;

                let mut paths = vec![];
                let mut entries = files_async::readdir_recursive(&dir, None);
                while let Some(entry) = entries.try_next().await.context("entries")? {
                    fx_log_info!("/pkgfs files: {:?}", entry);
                    paths.push(entry.name);
                }
                fx_log_info!("done listing /pkgfs");

                responder
                    .send(&mut paths.iter().map(|p| p.as_str()))
                    .context("sending reflect result")?;
            }
        }
    }

    Ok(())
}
