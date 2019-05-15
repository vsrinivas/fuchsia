// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Unit tests for fuchsia-component that exercise only the public API.

#![cfg(test)]
#![feature(async_await, await_macro)]

use {
    failure::Error,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io::{DirectoryMarker, FileMarker, SeekOrigin},
    fuchsia_async::run_until_stalled,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{self as zx, HandleBased as _},
    futures::{future::try_join, FutureExt, StreamExt},
};

#[run_until_stalled(test)]
async fn complete_with_no_clients() {
    await!(ServiceFs::new().collect())
}

#[run_until_stalled(test)]
async fn read_from_vmo_file() -> Result<(), Error> {
    const PATH: &str = "foo";
    const VMO_SIZE: u64 = 256;
    const VMO_FILE_OFFSET: usize = 5;
    const VMO_FILE_LENGTH: usize = 22;

    // 0, 1, 2, 3, 4, 5...
    let mut data = vec![];
    let mut data_i = 0u8;
    data.resize_with(VMO_SIZE as usize, || {
        data_i = data_i.wrapping_add(1);
        data_i
    });
    let file_data = &data[VMO_FILE_OFFSET..(VMO_FILE_OFFSET + VMO_FILE_LENGTH)];

    let mut fs = ServiceFs::new();

    let vmo = zx::Vmo::create(VMO_SIZE)?;
    vmo.write(&*data, 0)?;

    fs.add_vmo_file_at(
        PATH,
        vmo.duplicate_handle(zx::Rights::READ)?,
        VMO_FILE_OFFSET as u64,
        VMO_FILE_LENGTH as u64,
    );

    let (dir_proxy, dir_server_end) = create_proxy::<DirectoryMarker>()?;
    fs.serve_connection(dir_server_end.into_channel())?;

    let (file_proxy, file_server_end) = create_proxy::<FileMarker>()?;
    let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE;
    let mode = fidl_fuchsia_io::MODE_TYPE_FILE;
    dir_proxy.open(flags, mode, PATH, file_server_end.into_channel().into())?;

    let serve_fut = fs.collect().map(Ok);

    let read_from_chunks = async {
        let (status, read_data) = await!(file_proxy.read(VMO_FILE_LENGTH as u64))?;
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        assert_eq!(&*read_data, file_data);

        let (status, empty_read) = await!(file_proxy.read(VMO_FILE_LENGTH as u64))?;
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        assert_eq!(&*empty_read, &[]);

        let (status, position) = await!(file_proxy.seek(-5, SeekOrigin::End))?;
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        assert_eq!(position, VMO_FILE_LENGTH as u64 - 5);

        let read_at_count = 10usize;
        let read_at_offset = 4usize;
        let (status, read_at_data) =
            await!(file_proxy.read_at(read_at_count as u64, read_at_offset as u64))?;
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        assert_eq!(&*read_at_data, &file_data[read_at_offset..(read_at_offset + read_at_count)]);

        // Drop connections to allow server to close.
        drop(file_proxy);
        drop(dir_proxy);

        Ok::<_, Error>(())
    };

    let ((), ()) = await!(try_join(serve_fut, read_from_chunks))?;

    Ok(())
}
