// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Error},
    fidl_fuchsia_fs::{AdminMarker, AdminProxy},
    fidl_fuchsia_io::{DirectoryMarker, OPEN_RIGHT_READABLE},
    fidl_fuchsia_pkg::LocalMirrorRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_info, fx_log_warn},
    futures::StreamExt as _,
    pkg_local_mirror::PkgLocalMirror,
    std::path::Path,
};

const USB_DIR_PATH: &str = "fuchsia_pkg";

async fn make_local_mirror(
    admin: AdminProxy,
    stream: LocalMirrorRequestStream,
) -> Result<(), Error> {
    let (proxy, remote) =
        fidl::endpoints::create_proxy::<DirectoryMarker>().context("creating root proxy")?;
    admin.get_root(remote).context("Getting root")?;
    let dir_proxy = io_util::open_directory(&proxy, Path::new(USB_DIR_PATH), OPEN_RIGHT_READABLE)
        .context("opening fuchsia_pkg dir")?;
    let pkg_local_mirror =
        PkgLocalMirror::new(&dir_proxy).await.context("creating local mirror")?;
    pkg_local_mirror.handle_request_stream(stream).await
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init_with_tags(&["pkg-local-mirror"]).expect("can't init logger");
    fx_log_info!("starting pkg-local-mirror");

    let fat_proxy = fuchsia_component::client::connect_to_service_at_path::<AdminMarker>(
        "/svc/fuchsia.fat.Admin",
    )
    .context("connecting to fatfs")?;

    let mut fs = ServiceFs::new_local();
    let _ = fs.take_and_serve_directory_handle().context("serving directory handle")?;

    fs.dir("svc").add_fidl_service(IncomingService::LocalMirror);

    let () = fs
        .for_each_concurrent(None, |incoming_service| match incoming_service {
            IncomingService::LocalMirror(stream) => {
                let clone = fat_proxy.clone();
                async move {
                    make_local_mirror(clone, stream).await.unwrap_or_else(|e| {
                        fx_log_warn!(
                            "error handling fuchsia.pkg/LocalMirror request stream {:#}",
                            anyhow!(e)
                        )
                    })
                }
            }
        })
        .await;

    Ok(())
}

enum IncomingService {
    LocalMirror(LocalMirrorRequestStream),
}
