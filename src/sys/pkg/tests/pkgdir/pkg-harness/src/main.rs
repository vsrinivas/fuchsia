// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    blobfs_ramdisk::BlobfsRamdisk,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_test_fidl_pkg::{Backing, ConnectError, HarnessRequest, HarnessRequestStream},
    fuchsia_async::Task,
    fuchsia_component::server::ServiceFs,
    fuchsia_pkg_testing::SystemImageBuilder,
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    futures::prelude::*,
    pkgfs_ramdisk::PkgfsRamdisk,
};

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg-harness"]).expect("can't init logger");

    main_inner().await.map_err(|err| {
        // Use anyhow to print the error chain.
        let err = anyhow!(err);
        fx_log_err!("error running pkg-harness: {:#}", err);
        err
    })
}

async fn main_inner() -> Result<(), Error> {
    fx_log_info!("starting pkg-harness");

    // Spin up a blobfs and pkgfs.
    let blobfs = BlobfsRamdisk::start().expect("started blobfs");

    let system_image_package = SystemImageBuilder::new().build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().expect("getting root dir"));

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .expect("started pkgfs");

    // For now, we open the system_image package. In a future CL, we'll create a test package with
    // all the elements described in fxbug.dev/75208, and then open that test package.
    let pkgfs_backed_package = io_util::directory::open_directory(
        &pkgfs.root_dir_proxy().unwrap(),
        "packages/system_image/0",
        fidl_fuchsia_io::OPEN_FLAG_DIRECTORY,
    )
    .await
    .unwrap();

    // Set up serving FIDL to expose the test package.
    enum IncomingService {
        Harness(HarnessRequestStream),
    }
    let mut fs = ServiceFs::new();
    fs.take_and_serve_directory_handle().context("while serving directory handle")?;
    fs.dir("svc").add_fidl_service(IncomingService::Harness);
    let () = fs
        .for_each_concurrent(None, move |svc| {
            match svc {
                IncomingService::Harness(stream) => Task::spawn(
                    serve_harness(stream, Clone::clone(&pkgfs_backed_package))
                        .map(|res| res.context("while serving test.fidl.pkg.Harness")),
                ),
            }
            .unwrap_or_else(|e| {
                fx_log_err!("error handling fidl connection: {:#}", anyhow!(e));
            })
        })
        .await;

    Ok(())
}

/// Serve test.fidl.pkg.Harness.
async fn serve_harness(
    mut stream: HarnessRequestStream,
    pkgfs_backed_package: DirectoryProxy,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await.context("while pulling next event")? {
        let HarnessRequest::ConnectPackage { backing, dir, responder } = event;
        let pkg = match backing {
            Backing::Pkgfs => &pkgfs_backed_package,
            // TODO(fxbug.dev/75481): support pkgdir-backed packages.
            Backing::Pkgdir => {
                fx_log_warn!(
                    "pkgdir-backed packages are not yet supported. See fxbug.dev/75481 for tracking."
                );
                responder
                    .send(&mut Err(ConnectError::UnsupportedBacking))
                    .context("while sending failure response")?;
                continue;
            }
        };

        let () = pkg
            .clone(fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS, ServerEnd::new(dir.into_channel()))
            .expect("clone to succeed");

        responder.send(&mut Ok(())).context("while sending success response")?;
    }
    Ok(())
}
