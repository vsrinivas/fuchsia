// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context as _,
    fuchsia_syslog::{fx_log_err, fx_log_info, init},
    futures::stream::{StreamExt as _, TryStreamExt as _},
};

static TUF_REPO_CONFIG_ARG_KEY: &'static str = "tuf_repo_config";
static PKGFS_BOOT_ARG_KEY: &'static str = "zircon.system.pkgfs.cmd";
static PKGFS_BOOT_ARG_VALUE_PREFIX: &'static str = "bin/pkgsvr+";

/// Flags for fake_boot_arguments.
#[derive(argh::FromArgs, Debug, PartialEq)]
pub struct Args {
    /// absolute path to system_image package file.
    #[argh(option)]
    system_image_path: String,
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    init().unwrap();
    fx_log_info!("Starting fake_boot_arguments...");
    let args @ Args { system_image_path } = &argh::from_env();
    fx_log_info!("Initalizing fake_boot_arguments with {:?}", args);

    let system_image = io_util::file::read(
        &io_util::file::open_in_namespace(
            system_image_path.as_str(),
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        )
        .unwrap(),
    )
    .await
    .unwrap();

    let system_image_merkle =
        fuchsia_merkle::MerkleTree::from_reader(system_image.as_slice()).unwrap().root();
    let pkgfs_boot_arg_value = format!("{}{}", PKGFS_BOOT_ARG_VALUE_PREFIX, system_image_merkle);

    let mut fs = fuchsia_component::server::ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream: fidl_fuchsia_boot::ArgumentsRequestStream| stream);
    fs.take_and_serve_directory_handle().unwrap();
    fs.for_each_concurrent(None, |stream| async {
        let () = serve(stream, pkgfs_boot_arg_value.as_str()).await.unwrap_or_else(|err| {
            fx_log_err!("error handling fuchsia.boot/Arguments stream: {:#}", err)
        });
    })
    .await;
}

async fn serve(
    mut stream: fidl_fuchsia_boot::ArgumentsRequestStream,
    pkgfs_boot_arg_value: &str,
) -> anyhow::Result<()> {
    while let Some(request) = stream.try_next().await.context("getting next request")? {
        match request {
            fidl_fuchsia_boot::ArgumentsRequest::GetString { key, responder } => {
                let value = if key == PKGFS_BOOT_ARG_KEY {
                    Some(pkgfs_boot_arg_value)
                } else if key == TUF_REPO_CONFIG_ARG_KEY {
                    None
                } else {
                    anyhow::bail!("unsupported boot argument key {}", key);
                };
                responder.send(value).unwrap();
            }
            req => anyhow::bail!("unexpected request {:?}", req),
        }
    }
    Ok(())
}
