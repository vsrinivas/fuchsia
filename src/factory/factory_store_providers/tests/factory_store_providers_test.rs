// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    failure::Error,
    fidl_fuchsia_factory::{
        CastCredentialsFactoryStoreProviderMarker, MiscFactoryStoreProviderMarker,
        PlayReadyFactoryStoreProviderMarker, WeaveFactoryStoreProviderMarker,
        WidevineFactoryStoreProviderMarker,
    },
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fuchsia_async as fasync,
    std::fs,
    std::path::PathBuf,
    std::vec::Vec,
};

static DATA_FILE_PATH: &'static str = "/pkg/data";

macro_rules! connect_to_factory_store_provider {
    ($t:ty) => {{
        let provider = fuchsia_component::client::connect_to_service::<$t>()
            .expect("Failed to connect to service");

        let (dir_proxy, dir_server) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;
        provider.get_factory_store(dir_server).expect("Failed to get factory store");
        dir_proxy
    }};
}

async fn read_file_from_proxy<'a>(
    dir_proxy: &'a DirectoryProxy,
    file_path: &'a str,
) -> Result<Vec<u8>, Error> {
    let file =
        io_util::open_file(&dir_proxy, &PathBuf::from(file_path), io_util::OPEN_RIGHT_READABLE)?;
    io_util::read_file_bytes(&file).await
}

#[fasync::run_singlethreaded(test)]
async fn read_factory_files_from_cast_credentials_store() -> Result<(), Error> {
    let dir_proxy = connect_to_factory_store_provider!(CastCredentialsFactoryStoreProviderMarker);

    {
        let path = format!("{}/{}", DATA_FILE_PATH, "another_cast_file");
        let expected_contents =
            fs::read(&path).expect(&format!("Unable to read expected file: {}", &path));

        let contents = read_file_from_proxy(&dir_proxy, "cast.blk").await?;
        assert_eq!(expected_contents, contents);
    }
    {
        let path = format!("{}/{}", DATA_FILE_PATH, "some_cast_file");
        let expected_contents =
            fs::read(&path).expect(&format!("Unable to read expected file: {}", &path));

        let contents = read_file_from_proxy(&dir_proxy, "cast.dat").await?;
        assert_eq!(expected_contents, contents);
    }

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn read_factory_files_from_cast_credentials_store_missing_fails() -> Result<(), Error> {
    let dir_proxy = connect_to_factory_store_provider!(CastCredentialsFactoryStoreProviderMarker);
    read_file_from_proxy(&dir_proxy, "misc.bin").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "pr3.dat").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "wv.blk").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "some_path").await.unwrap_err();
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn read_factory_files_from_misc_store() -> Result<(), Error> {
    let dir_proxy = connect_to_factory_store_provider!(MiscFactoryStoreProviderMarker);

    let path = format!("{}/{}", DATA_FILE_PATH, "misc");
    let expected_contents =
        fs::read(&path).expect(&format!("Unable to read expected file: {}", &path));

    let contents = read_file_from_proxy(&dir_proxy, "misc.bin").await?;
    assert_eq!(expected_contents, contents);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn read_factory_files_from_misc_store_passed_file_appears() -> Result<(), Error> {
    let dir_proxy = connect_to_factory_store_provider!(MiscFactoryStoreProviderMarker);

    let path = format!("{}/{}", DATA_FILE_PATH, "passed_misc_file");
    let expected_contents =
        fs::read(&path).expect(&format!("Unable to read expected file: {}", &path));

    let contents = read_file_from_proxy(&dir_proxy, "passed/file").await?;
    assert_eq!(expected_contents, contents);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn read_factory_files_from_misc_store_ignored_file_missing() -> Result<(), Error> {
    let dir_proxy = connect_to_factory_store_provider!(MiscFactoryStoreProviderMarker);
    read_file_from_proxy(&dir_proxy, "ignored").await.unwrap_err();
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn read_factory_files_from_misc_store_missing_fails() -> Result<(), Error> {
    let dir_proxy = connect_to_factory_store_provider!(MiscFactoryStoreProviderMarker);
    read_file_from_proxy(&dir_proxy, "cast.blk").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "cast.dat").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "pr3.dat").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "wv.blk").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "/nofile/path").await.unwrap_err();
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn read_factory_files_from_playready_store() -> Result<(), Error> {
    let dir_proxy = connect_to_factory_store_provider!(PlayReadyFactoryStoreProviderMarker);

    let path = format!("{}/{}", DATA_FILE_PATH, "file1");
    let expected_contents =
        fs::read(&path).expect(&format!("Unable to read expected file: {}", &path));

    let contents = read_file_from_proxy(&dir_proxy, "pr3.dat").await?;
    assert_eq!(expected_contents, contents);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn read_factory_files_from_playready_store_missing_fails() -> Result<(), Error> {
    let dir_proxy = connect_to_factory_store_provider!(PlayReadyFactoryStoreProviderMarker);
    read_file_from_proxy(&dir_proxy, "cast.blk").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "cast.dat").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "misc.bin").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "wv.blk").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "nothing").await.unwrap_err();
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn read_factory_files_from_widevine_store() -> Result<(), Error> {
    let dir_proxy = connect_to_factory_store_provider!(WidevineFactoryStoreProviderMarker);

    let path = format!("{}/{}", DATA_FILE_PATH, "widevine_file");
    let expected_contents =
        fs::read(&path).expect(&format!("Unable to read expected file: {}", &path));

    let contents = read_file_from_proxy(&dir_proxy, "wv.blk").await?;
    assert_eq!(expected_contents, contents);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn read_factory_files_from_widevine_store_missing_files_fail() -> Result<(), Error> {
    let dir_proxy = connect_to_factory_store_provider!(WidevineFactoryStoreProviderMarker);
    read_file_from_proxy(&dir_proxy, "cast.blk").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "cast.dat").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "misc.bin").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "pr3.dat").await.unwrap_err();
    read_file_from_proxy(&dir_proxy, "nonexistant").await.unwrap_err();
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn read_factory_files_from_weave_store() -> Result<(), Error> {
    let dir_proxy = connect_to_factory_store_provider!(WeaveFactoryStoreProviderMarker);
    let path = format!("{}/{}", DATA_FILE_PATH, "weave_file");
    let expected_contents =
        fs::read(&path).expect(&format!("Unable to read expected file: {}", &path));
    let contents = read_file_from_proxy(&dir_proxy, "weave").await?;
    assert_eq!(expected_contents, contents);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn read_factory_files_from_weave_store_missing_files_fail() -> Result<(), Error> {
    let dir_proxy = connect_to_factory_store_provider!(WeaveFactoryStoreProviderMarker);
    read_file_from_proxy(&dir_proxy, "weave_bad").await.unwrap_err();
    Ok(())
}

/// The "multi_validated_file" file is validated in 2 places:
/// - In fuchsia.factory.PlayReadyFactoryStoreProvider where it must be UTF-8 formatted.
/// - In fuchsia.factory.WidevineFactoryStoreProvider where it must meet a size requirement.
///   The required size for multi_validated_file is set such that it will fail validation for that
///   protocol (file must be at least 1TB in size which is quite large especially for this test
///   suite); however this file should still appear in PlayReadyFactoryStoreProvider.
///
/// This test ensures 2 things:
/// 1. That a file goes through both a positive and negative validation case. This is to ensure that
///    the file has actually been processed and not that the file is simply missing from the test
///    environment.
/// 2. Configurations between the protocols are separate and distinct. In practice, it's unlikely
///    the same file will appear in multiple protocols.
#[fasync::run_singlethreaded(test)]
async fn multi_validated_file_is_processed_properly() -> Result<(), Error> {
    let widevine_dir_proxy = connect_to_factory_store_provider!(WidevineFactoryStoreProviderMarker);
    read_file_from_proxy(&widevine_dir_proxy, "multi_validated_file").await.unwrap_err();

    let path = format!("{}/{}", DATA_FILE_PATH, "multi_validated_file");
    let expected_contents =
        fs::read(&path).expect(&format!("Unable to read expected file: {}", &path));

    let playready_dir_proxy =
        connect_to_factory_store_provider!(PlayReadyFactoryStoreProviderMarker);
    let contents = read_file_from_proxy(&playready_dir_proxy, "multi_validated_file").await?;
    assert_eq!(expected_contents, contents);
    Ok(())
}
