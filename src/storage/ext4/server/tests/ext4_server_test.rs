// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::Error,
    fidl_fuchsia_io::{DirectoryMarker, OPEN_RIGHT_READABLE},
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_storage_ext4::{MountVmoResult, Server_Marker, ServiceMarker, Success},
    fuchsia_async as fasync,
    fuchsia_component::client::{launch, launcher},
    fuchsia_zircon as zx, io_util,
    std::fs,
    std::io::{self, Read, Seek},
    std::path::PathBuf,
    test_util::assert_matches,
};

#[fasync::run_singlethreaded(test)]
async fn ext4_server_mounts_vmo_one_file() -> Result<(), Error> {
    let ext4 = fuchsia_component::client::connect_to_service::<Server_Marker>()
        .expect("Failed to connect to service");

    let mut file_buf = io::BufReader::new(fs::File::open("/pkg/data/1file.img")?);
    let size = file_buf.seek(io::SeekFrom::End(0))?;
    file_buf.seek(io::SeekFrom::Start(0))?;

    let mut temp_buf = vec![0u8; size as usize];
    file_buf.read(&mut temp_buf)?;

    let vmo = zx::Vmo::create(size)?;
    vmo.write(&temp_buf, 0)?;
    let mut buf = Buffer { vmo, size };

    let (dir_proxy, dir_server) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;
    let result = ext4.mount_vmo(&mut buf, OPEN_RIGHT_READABLE, dir_server).await;
    assert_matches!(result, Ok(MountVmoResult::Success(Success {})));

    let file =
        io_util::open_file(&dir_proxy, &PathBuf::from("file1"), io_util::OPEN_RIGHT_READABLE)?;
    assert_eq!("file1 contents.\n".to_string(), io_util::read_file(&file).await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn ext4_server_mounts_vmo_nested_dirs() -> Result<(), Error> {
    let ext4 = fuchsia_component::client::connect_to_service::<Server_Marker>()
        .expect("Failed to connect to service");

    let mut file_buf = io::BufReader::new(fs::File::open("/pkg/data/nest.img")?);
    let size = file_buf.seek(io::SeekFrom::End(0))?;
    file_buf.seek(io::SeekFrom::Start(0))?;

    let mut temp_buf = vec![0u8; size as usize];
    file_buf.read(&mut temp_buf)?;

    let vmo = zx::Vmo::create(size)?;
    vmo.write(&temp_buf, 0)?;
    let mut buf = Buffer { vmo, size };

    let (dir_proxy, dir_server) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;
    let result = ext4.mount_vmo(&mut buf, OPEN_RIGHT_READABLE, dir_server).await;
    assert_matches!(result, Ok(MountVmoResult::Success(Success {})));

    let file1 =
        io_util::open_file(&dir_proxy, &PathBuf::from("file1"), io_util::OPEN_RIGHT_READABLE)?;
    assert_eq!("file1 contents.\n".to_string(), io_util::read_file(&file1).await?);

    let file2 = io_util::open_file(
        &dir_proxy,
        &PathBuf::from("inner/file2"),
        io_util::OPEN_RIGHT_READABLE,
    )?;
    assert_eq!("file2 contents.\n".to_string(), io_util::read_file(&file2).await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn ext4_unified_service_mounts_vmo() -> Result<(), Error> {
    // Unified services are still a work in progress. Ideally, the component framework would launch
    // this component, but this feature is not fully implemented yet.
    // TODO(mbrunson): Move this to the component manifest when there is direction on how to
    // implement this kind of test properly.
    let app = launch(
        &launcher()?,
        "fuchsia-pkg://fuchsia.com/ext4_server_integration_tests#meta/ext4_readonly.cmx"
            .to_string(),
        None,
    )?;

    let ext4_service =
        app.connect_to_unified_service::<ServiceMarker>().expect("Failed to connect to service");
    let ext4 = ext4_service.server()?;

    let mut file_buf = io::BufReader::new(fs::File::open("/pkg/data/nest.img")?);
    let size = file_buf.seek(io::SeekFrom::End(0))?;
    file_buf.seek(io::SeekFrom::Start(0))?;

    let mut temp_buf = vec![0u8; size as usize];
    file_buf.read(&mut temp_buf)?;

    let vmo = zx::Vmo::create(size)?;
    vmo.write(&temp_buf, 0)?;
    let mut buf = Buffer { vmo, size };

    let (dir_proxy, dir_server) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;
    let result = ext4.mount_vmo(&mut buf, OPEN_RIGHT_READABLE, dir_server).await;
    assert_matches!(result, Ok(MountVmoResult::Success(Success {})));

    let file1 =
        io_util::open_file(&dir_proxy, &PathBuf::from("file1"), io_util::OPEN_RIGHT_READABLE)?;
    assert_eq!("file1 contents.\n".to_string(), io_util::read_file(&file1).await?);

    let file2 = io_util::open_file(
        &dir_proxy,
        &PathBuf::from("inner/file2"),
        io_util::OPEN_RIGHT_READABLE,
    )?;
    assert_eq!("file2 contents.\n".to_string(), io_util::read_file(&file2).await?);
    Ok(())
}
