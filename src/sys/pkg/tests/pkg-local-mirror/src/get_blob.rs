// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests calls to the get_blob API.
use {
    super::*,
    fidl::endpoints::{create_proxy, create_request_stream},
    fidl_fuchsia_io::FileEvent::OnOpen_,
    fidl_fuchsia_pkg::GetBlobError,
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_zircon::Status,
    futures::channel::oneshot,
    matches::assert_matches,
    vfs::file::pcb::read_only_static,
};

async fn verify_get_blob_with_read_success(env: &TestEnv, blob: &str, file_contents: &str) {
    let (file_proxy, server_end) = create_proxy().unwrap();

    let res = env
        .local_mirror_proxy()
        .get_blob(&mut BlobId::parse(blob).unwrap().into(), server_end)
        .await;

    assert_eq!(res.unwrap(), Ok(()));
    assert_matches!(
        file_proxy.take_event_stream().next().await,
        Some(Ok(OnOpen_{s, info: Some(_)})) if Status::ok(s) == Ok(())
    );
    assert_eq!(io_util::read_file(&file_proxy).await.unwrap(), file_contents.to_owned());
}

#[fasync::run_singlethreaded(test)]
async fn success() {
    let env = TestEnv::builder()
        .usb_dir(spawn_vfs(pseudo_directory! {
            "0" => pseudo_directory! {
                "fuchsia_pkg" => pseudo_directory! {
                    "blobs" => pseudo_directory! {
                        "00" => pseudo_directory! {
                            "00000000000000000000000000000000000000000000000000000000000000" =>
                                read_only_static("ben"),
                            "11111111111111111111111111111111111111111111111111111111111111" =>
                                read_only_static("dan"),
                        },
                        "aa" => pseudo_directory! {
                            "bbccddeeff00112233445566778899aabbccddeeff00112233445566778899" =>
                                read_only_static("kevin"),
                        },
                    },
                    "repository_metadata" => pseudo_directory! {},
                },
            },
        }))
        .build();

    verify_get_blob_with_read_success(
        &env,
        "0000000000000000000000000000000000000000000000000000000000000000",
        "ben",
    )
    .await;
    verify_get_blob_with_read_success(
        &env,
        "0011111111111111111111111111111111111111111111111111111111111111",
        "dan",
    )
    .await;
    verify_get_blob_with_read_success(
        &env,
        "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899",
        "kevin",
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn missing_blob() {
    let env = TestEnv::builder().build();
    let (file_proxy, server_end) = create_proxy().unwrap();

    let res = env
        .local_mirror_proxy()
        .get_blob(
            &mut BlobId::parse("0000000000000000000000000000000000000000000000000000000000000000")
                .unwrap()
                .into(),
            server_end,
        )
        .await;

    assert_eq!(res.unwrap(), Ok(()));
    assert_matches!(
        file_proxy.take_event_stream().next().await,
        Some(Ok(OnOpen_{s, info: None})) if  Status::from_raw(s) == Status::NOT_FOUND
    );
    assert_matches!(io_util::read_file(&file_proxy).await, Err(_));
}

#[fasync::run_singlethreaded(test)]
async fn error_opening_blob() {
    let (client_end, stream) = create_request_stream().unwrap();
    let env = TestEnv::builder().usb_dir(client_end).build();

    let (channels_closed_sender, channels_closed_recv) = oneshot::channel();
    fasync::Task::spawn(close_channels_then_notify(stream, channels_closed_sender)).detach();

    // Wait for the channel connecting the pkg-local-mirror to the blobs dir to close.
    // This ensures that GetBlob calls will fail with the expected fidl error.
    let () = channels_closed_recv.await.unwrap();

    let (file_proxy, server_end) = create_proxy().unwrap();
    let res = env
        .local_mirror_proxy()
        .get_blob(
            &mut BlobId::parse("0000000000000000000000000000000000000000000000000000000000000000")
                .unwrap()
                .into(),
            server_end,
        )
        .await;

    assert_eq!(res.unwrap(), Err(GetBlobError::ErrorOpeningBlob));
    assert_matches!(file_proxy.take_event_stream().next().await, None);
    assert_matches!(io_util::read_file(&file_proxy).await, Err(_));
}
