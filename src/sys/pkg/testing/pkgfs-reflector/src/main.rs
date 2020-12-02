// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_io::{DirectoryMarker, NodeMarker},
    fidl_test_pkg_reflector::{ReflectorRequest, ReflectorRequestStream},
    fuchsia_async as fasync,
    fuchsia_async::futures::StreamExt,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::prelude::*,
};

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    main_inner().await.map_err(|e| {
        fx_log_err!("error running reflector: {:#}", e);
        e
    })
}

async fn main_inner() -> Result<(), Error> {
    fx_log_info!("starting pkgfs reflector service");

    let mut fs = ServiceFs::new_local();

    let (dir_proxy, dir_server_end) =
        fidl::endpoints::create_proxy::<DirectoryMarker>().context("creating pkgfs channel")?;

    fs.add_remote("pkgfs", dir_proxy);

    let mut dir_server_end = Some(ServerEnd::new(dir_server_end.into_channel()));

    fs.dir("svc").add_fidl_service(move |stream| {
        let dir_server_end = dir_server_end.take();
        fasync::Task::local(
            async move { serve_reflector(stream, dir_server_end).await }
                .unwrap_or_else(|e| fx_log_err!("error serving reflector: {:#}", e)),
        )
        .detach()
    });

    fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;
    fs.collect::<()>().await;

    Ok(())
}

async fn serve_reflector(
    mut stream: ReflectorRequestStream,
    mut dir_server_end: Option<ServerEnd<NodeMarker>>,
) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("getting request from stream")? {
        match request {
            ReflectorRequest::Reflect { pkgfs, responder } => {
                fx_log_info!("registering fake pkgfs");
                let dir_server_end = dir_server_end.take().expect("server end to not be taken");

                let pkgfs = ClientEnd::<NodeMarker>::new(pkgfs.into_channel())
                    .into_proxy()
                    .expect("converting client end to proxy");
                pkgfs.clone(fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS, dir_server_end).unwrap();

                responder.send().context("sending reflect result")?;
            }
        }
    }

    Ok(())
}
