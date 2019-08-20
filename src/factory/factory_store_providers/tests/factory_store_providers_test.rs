// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
#![feature(async_await)]

use {
    failure::Error,
    fidl_fuchsia_factory::{
        CastCredentialsFactoryStoreProviderMarker, MiscFactoryStoreProviderMarker,
        PlayReadyFactoryStoreProviderMarker, WidevineFactoryStoreProviderMarker,
    },
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fuchsia_async as fasync,
    std::fs,
    std::path::PathBuf,
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
