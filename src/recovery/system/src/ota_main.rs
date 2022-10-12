// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context, Error};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_recovery_ui::{
    ProgressRendererMarker, ProgressRendererProxyInterface, ProgressRendererRender2Request, Status,
};
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_runtime::{take_startup_handle, HandleType};
use futures::future::Future;
use ota_lib::{
    ota::{run_wellknown_ota, StorageType},
    storage::{Storage, StorageFactory, TopoPathInitializer},
};
use vfs::directory::entry::DirectoryEntry;

fn to_render2_error(err: fidl::Error) -> Error {
    anyhow::format_err!("Error encountered while calling render2: {:?}", err)
}

async fn main_internal<B, S, P, T, Fut>(
    storage_factory: S,
    ota_progress_proxy: &P,
    outgoing_dir: ServerEnd<fio::NodeMarker>,
    do_ota: T,
) -> Result<(), Error>
where
    B: Storage + 'static,
    S: StorageFactory<B>,
    P: ProgressRendererProxyInterface,
    T: FnOnce(Box<dyn Storage>) -> Fut,
    Fut: Future<Output = Result<(), Error>> + 'static,
{
    let storage = storage_factory.create().await.context("initialising storage")?;

    let blobfs_proxy = storage
        .wipe_or_get_storage()
        .await?
        .into_proxy()
        .map_err(|e| format_err!("could not convert blobfs to proxy: {:?}", e))?;

    let svc_dir = vfs::pseudo_directory! {
        "blob" => vfs::remote::remote_dir(blobfs_proxy)
    };

    let scope = vfs::execution_scope::ExecutionScope::new();
    svc_dir.open(
        scope.clone(),
        fio::OpenFlags::RIGHT_READABLE
            | fio::OpenFlags::RIGHT_WRITABLE
            | fio::OpenFlags::RIGHT_EXECUTABLE,
        0,
        vfs::path::Path::dot(),
        outgoing_dir,
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

    match do_ota(Box::new(storage)).await {
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
    let topo_path_initializer = TopoPathInitializer {};
    let ota_progress_proxy = client::connect_to_protocol::<ProgressRendererMarker>()?;
    let directory_handle = take_startup_handle(HandleType::DirectoryRequest.into())
        .expect("cannot take startup handle");

    main_internal(
        topo_path_initializer,
        &ota_progress_proxy,
        fuchsia_zircon::Channel::from(directory_handle).into(),
        move |storage| run_wellknown_ota(StorageType::Ready(storage)),
    )
    .await
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use async_trait::async_trait;
    use fidl::endpoints::{create_endpoints, create_proxy, create_proxy_and_stream, ClientEnd};
    use fidl_fuchsia_recovery_ui::ProgressRendererRequest;
    use futures::stream::StreamExt;
    use std::sync::Arc;
    use vfs::{directory::entry::DirectoryEntry, file::vmo::read_only_static};

    struct FakeStorage {
        dir: Arc<dyn DirectoryEntry>,
    }
    impl FakeStorage {
        fn new() -> Self {
            let dir = vfs::pseudo_directory! {
                "testfile" => read_only_static("test1")
            };

            Self { dir }
        }
    }

    #[async_trait]
    impl Storage for FakeStorage {
        async fn wipe_or_get_storage(&self) -> Result<ClientEnd<fio::DirectoryMarker>, Error> {
            let (client, server) = create_endpoints::<fio::DirectoryMarker>().unwrap();

            let scope = vfs::execution_scope::ExecutionScope::new();

            self.dir.clone().open(
                scope.clone(),
                fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::RIGHT_EXECUTABLE,
                0,
                vfs::path::Path::dot(),
                server.into_channel().into(),
            );
            fasync::Task::local(async move { scope.wait().await }).detach();

            Ok(client)
        }

        async fn wipe_data(&self) -> Result<(), Error> {
            Ok(())
        }
    }

    struct FakeStorageFactory {
        storage: FakeStorage,
    }

    #[async_trait]
    impl StorageFactory<FakeStorage> for FakeStorageFactory {
        async fn create(self) -> Result<FakeStorage, Error> {
            Ok(self.storage)
        }
    }

    #[fuchsia::test]
    async fn test_main_internal_reports_ota_success() {
        let fake_storage_factory = FakeStorageFactory { storage: FakeStorage::new() };
        let (progress_proxy, mut progress_stream) =
            create_proxy_and_stream::<ProgressRendererMarker>().unwrap();
        let (dir_proxy, dir_server) = create_proxy::<fio::DirectoryMarker>().unwrap();

        fasync::Task::local(async move {
            let blob_test_file = fuchsia_fs::directory::open_file(&dir_proxy, "blob/testfile", fio::OpenFlags::RIGHT_READABLE).await.unwrap();
            assert_eq!("test1", fuchsia_fs::file::read_to_string(&blob_test_file).await.unwrap());

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
            fake_storage_factory,
            &progress_proxy,
            dir_server.into_channel().into(),
            |_storage| futures::future::ready(Ok(())),
        )
        .await
        .unwrap();
    }

    #[fuchsia::test]
    async fn test_main_internal_sends_error_when_ota_fails() {
        let fake_storage_factory = FakeStorageFactory { storage: FakeStorage::new() };
        let (progress_proxy, mut progress_stream) =
            create_proxy_and_stream::<ProgressRendererMarker>().unwrap();
        let (dir_proxy, dir_server) = create_proxy::<fio::DirectoryMarker>().unwrap();

        fasync::Task::local(async move {
            assert!(fuchsia_fs::directory::dir_contains(&dir_proxy, "blob").await.unwrap());

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
            fake_storage_factory,
            &progress_proxy,
            dir_server.into_channel().into(),
            |_storage| futures::future::ready(Err(format_err!("ota failed"))),
        )
        .await
        .unwrap_err();
    }
}
