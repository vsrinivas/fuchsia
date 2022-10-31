// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_recovery_ui::{
    ProgressRendererMarker, ProgressRendererProxyInterface, ProgressRendererRender2Request, Status,
};
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_runtime::{take_startup_handle, HandleType};
use futures::future::Future;
use ota_lib::{ota::run_wellknown_ota, storage::wipe_storage};
use std::sync::Arc;
use vfs::directory::{entry::DirectoryEntry, mutable::simple::Simple};

fn to_render2_error(err: fidl::Error) -> Error {
    anyhow::format_err!("Error encountered while calling render2: {:?}", err)
}

async fn main_internal<S, P, T, Fut, Fut2>(
    wipe_storage_fn: S,
    ota_progress_proxy: &P,
    out_dir: ServerEnd<fio::NodeMarker>,
    do_ota_fn: T,
) -> Result<(), Error>
where
    S: FnOnce() -> Fut2,
    P: ProgressRendererProxyInterface,
    T: FnOnce(fio::DirectoryProxy, Arc<Simple>) -> Fut,
    Fut: Future<Output = Result<(), Error>> + 'static,
    Fut2: Future<Output = Result<fio::DirectoryProxy, Error>> + 'static,
{
    let outgoing_dir_vfs = vfs::mut_pseudo_directory! {};

    let blobfs_proxy = wipe_storage_fn().await.context("failed to wipe storage")?;

    let scope = vfs::execution_scope::ExecutionScope::new();
    outgoing_dir_vfs.clone().open(
        scope.clone(),
        fio::OpenFlags::RIGHT_READABLE
            | fio::OpenFlags::RIGHT_WRITABLE
            | fio::OpenFlags::RIGHT_EXECUTABLE,
        0,
        vfs::path::Path::dot(),
        out_dir,
    );
    fasync::Task::local(async move { scope.wait().await }).detach();

    ota_progress_proxy
        .render2(ProgressRendererRender2Request {
            status: Some(Status::Active),
            percent_complete: Some(0.0),
            ..ProgressRendererRender2Request::EMPTY
        })
        .await
        .map_err(to_render2_error)?;

    match do_ota_fn(blobfs_proxy, outgoing_dir_vfs).await {
        Ok(_) => {
            println!("OTA Success!");
            ota_progress_proxy
                .render2(ProgressRendererRender2Request {
                    status: Some(Status::Complete),
                    percent_complete: Some(100.0),
                    ..ProgressRendererRender2Request::EMPTY
                })
                .await
                .map_err(to_render2_error)
        }
        Err(e) => {
            println!("OTA Error..... {:?}", e);
            ota_progress_proxy
                .render2(ProgressRendererRender2Request {
                    status: Some(Status::Error),
                    ..ProgressRendererRender2Request::EMPTY
                })
                .await
                .map_err(to_render2_error)?;
            Err(e)
        }
    }
}

#[fuchsia::main(logging = true)]
async fn main() -> Result<(), Error> {
    stdout_to_debuglog::init().await.unwrap_or_else(|error| {
        eprintln!("Failed to initialize debuglog: {:?}", error);
    });

    println!("recovery-ota: started");
    let ota_progress_proxy = client::connect_to_protocol::<ProgressRendererMarker>()?;
    let directory_handle = take_startup_handle(HandleType::DirectoryRequest.into())
        .expect("cannot take startup handle");

    main_internal(
        wipe_storage,
        &ota_progress_proxy,
        fuchsia_zircon::Channel::from(directory_handle).into(),
        move |blobfs_proxy, outgoing_dir| run_wellknown_ota(blobfs_proxy, outgoing_dir),
    )
    .await
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::format_err;
    use assert_matches::assert_matches;
    use fidl::endpoints::{create_endpoints, create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_recovery_ui::ProgressRendererRequest;
    use futures::stream::StreamExt;
    use vfs::{directory::entry::DirectoryEntry, file::vmo::read_only_static};

    async fn fake_wipe_storage() -> Result<fio::DirectoryProxy, Error> {
        let (client, server) = create_endpoints::<fio::DirectoryMarker>().unwrap();

        let scope = vfs::execution_scope::ExecutionScope::new();

        let dir = vfs::pseudo_directory! {
            "testfile" => read_only_static("test1")
        };

        dir.open(
            scope.clone(),
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::RIGHT_EXECUTABLE,
            0,
            vfs::path::Path::dot(),
            server.into_channel().into(),
        );
        fasync::Task::local(async move { scope.wait().await }).detach();

        Ok(client.into_proxy()?)
    }

    #[fuchsia::test]
    async fn test_main_internal_reports_ota_success() {
        let (progress_proxy, mut progress_stream) =
            create_proxy_and_stream::<ProgressRendererMarker>().unwrap();
        let (_dir_proxy, dir_server) = create_proxy::<fio::DirectoryMarker>().unwrap();

        fasync::Task::local(async move {
            assert_matches!(progress_stream.next().await.unwrap().unwrap(), ProgressRendererRequest::Render2 { payload, responder } => {
                assert_eq!(payload.status.unwrap(), Status::Active);
                assert_eq!(payload.percent_complete.unwrap(), 0.0);
                responder.send().unwrap();
            });

            assert_matches!(progress_stream.next().await.unwrap().unwrap(), ProgressRendererRequest::Render2 { payload, responder } => {
                assert_eq!(payload.status.unwrap(), Status::Complete);
                assert_eq!(payload.percent_complete.unwrap(), 100.0);
                responder.send().unwrap();
            });

            // Expect nothing more.
            assert!(progress_stream.next().await.is_none());
        })
        .detach();

        main_internal(
            fake_wipe_storage,
            &progress_proxy,
            dir_server.into_channel().into(),
            |_storage, _outgoing_dir| futures::future::ready(Ok(())),
        )
        .await
        .unwrap();
    }

    #[fuchsia::test]
    async fn test_main_internal_sends_error_when_ota_fails() {
        let (progress_proxy, mut progress_stream) =
            create_proxy_and_stream::<ProgressRendererMarker>().unwrap();
        let (_dir_proxy, dir_server) = create_proxy::<fio::DirectoryMarker>().unwrap();

        fasync::Task::local(async move {
            assert_matches!(progress_stream.next().await.unwrap().unwrap(), ProgressRendererRequest::Render2 { payload, responder } => {
                assert_eq!(payload.status.unwrap(), Status::Active);
                assert_eq!(payload.percent_complete.unwrap(), 0.0);
                responder.send().unwrap();
            });

            assert_matches!(progress_stream.next().await.unwrap().unwrap(), ProgressRendererRequest::Render2 { payload, responder } => {
                assert_eq!(payload.status.unwrap(), Status::Error);
                responder.send().unwrap();
            });

            // Expect nothing more.
            assert!(progress_stream.next().await.is_none());
        })
        .detach();

        main_internal(
            fake_wipe_storage,
            &progress_proxy,
            dir_server.into_channel().into(),
            |_storage, _outgoing_dir| futures::future::ready(Err(format_err!("ota failed"))),
        )
        .await
        .unwrap_err();
    }
}
