// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Unit tests for fuchsia-component that exercise only the public API.

#![cfg(test)]
#![feature(async_await, await_macro)]

use {
    failure::Error,
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, FileMarker, FileProxy, NodeInfo, NodeMarker, SeekOrigin, Service,
    },
    fuchsia_async::run_until_stalled,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{self as zx, HandleBased as _},
    futures::{future::try_join, FutureExt, StreamExt},
    std::future::Future,
};

#[run_until_stalled(test)]
async fn complete_with_no_clients() {
    await!(ServiceFs::new().collect())
}

#[run_until_stalled(test)]
async fn open_service_node_reference() -> Result<(), Error> {
    const PATH: &str = "service_name";

    let mut fs = ServiceFs::new();
    fs.add_service_at(PATH, |_chan| Some(()));
    let (dir_proxy, dir_server_end) = create_proxy::<DirectoryMarker>()?;
    fs.serve_connection(dir_server_end.into_channel())?;
    let serve_fut = fs.collect().map(Ok);

    let open_reference_fut = async {
        let flags = fidl_fuchsia_io::OPEN_FLAG_NODE_REFERENCE;
        let mode = fidl_fuchsia_io::MODE_TYPE_SERVICE;
        let (node_proxy, node_server_end) = create_proxy::<NodeMarker>()?;
        dir_proxy.open(flags, mode, PATH, node_server_end)?;
        drop(dir_proxy);

        let info = await!(node_proxy.describe())?;
        if let NodeInfo::Service(Service {}) = info {
            // ok
        } else {
            panic!("expected service node, found {:?}", info);
        }
        drop(node_proxy);
        Ok::<(), Error>(())
    };

    let ((), ()) = await!(try_join(serve_fut, open_reference_fut))?;
    Ok(())
}

// Identical to `open_service_node_reference`, but clones the opened directory before attempting to
// open the service node.
#[run_until_stalled(test)]
async fn clone_service_dir() -> Result<(), Error> {
    const PATH: &str = "service_name";

    let mut fs = ServiceFs::new();
    fs.add_service_at(PATH, |_chan| Some(()));
    let (dir_proxy, dir_server_end) = create_proxy::<DirectoryMarker>()?;
    fs.serve_connection(dir_server_end.into_channel())?;
    let serve_fut = fs.collect().map(Ok);

    let open_reference_fut = async {
        let (dir_proxy_clone, dir_server_end_clone) = create_proxy::<DirectoryMarker>()?;
        dir_proxy.clone(
            fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS,
            ServerEnd::new(dir_server_end_clone.into_channel()),
        )?;
        drop(dir_proxy);

        let flags = fidl_fuchsia_io::OPEN_FLAG_NODE_REFERENCE;
        let mode = fidl_fuchsia_io::MODE_TYPE_SERVICE;
        let (node_proxy, node_server_end) = create_proxy::<NodeMarker>()?;
        dir_proxy_clone.open(flags, mode, PATH, node_server_end)?;
        drop(dir_proxy_clone);

        let info = await!(node_proxy.describe())?;
        if let NodeInfo::Service(Service {}) = info {
            // ok
        } else {
            panic!("expected service node, found {:?}", info);
        }
        drop(node_proxy);
        Ok::<(), Error>(())
    };

    let ((), ()) = await!(try_join(serve_fut, open_reference_fut))?;
    Ok(())
}

async fn assert_read<'a>(
    file_proxy: &'a FileProxy,
    length: u64,
    expected: &'a [u8],
) -> Result<(), Error> {
    let (status, read_data) = await!(file_proxy.read(length))?;
    zx::Status::ok(status)?;
    assert_eq!(&*read_data, expected);
    Ok(())
}

// close the file and check that further reads fail.
async fn assert_close(file_proxy: &FileProxy) -> Result<(), Error> {
    let status = await!(file_proxy.close())?;
    zx::Status::ok(status)?;
    assert!(await!(file_proxy.read(0)).is_err());
    Ok(())
}

#[run_until_stalled(test)]
async fn open_remote_directory_files() -> Result<(), Error> {
    let mut root = ServiceFs::new();
    let mut fs = ServiceFs::new();
    let (remote_proxy, remote_server_end) = create_proxy::<DirectoryMarker>()?;

    let data = b"test";

    let vmo = zx::Vmo::create(4096)?;
    vmo.write(data, 0)?;

    root.dir("files").add_vmo_file_at(
        "test.txt",
        vmo.duplicate_handle(zx::Rights::READ)?,
        0,
        data.len() as u64,
    );
    root.serve_connection(remote_server_end.into_channel())?;

    // Add the remote as "test"
    fs.add_remote("test", remote_proxy);
    let (dir_proxy, dir_server_end) = create_proxy::<DirectoryMarker>()?;
    fs.serve_connection(dir_server_end.into_channel())?;

    fuchsia_async::spawn(root.collect::<()>());
    fuchsia_async::spawn(fs.collect::<()>());

    // Open the test file
    let (file_proxy, file_server_end) = create_proxy::<FileMarker>()?;
    let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE;
    let mode = fidl_fuchsia_io::MODE_TYPE_FILE;
    dir_proxy.open(flags, mode, "test/files/test.txt", file_server_end.into_channel().into())?;

    // Open the top of the remote hierarchy.
    let (top_proxy, top_end) = create_proxy::<DirectoryMarker>()?;
    dir_proxy.open(
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        fidl_fuchsia_io::MODE_TYPE_DIRECTORY,
        "test",
        top_end.into_channel().into(),
    )?;
    drop(dir_proxy);

    // Check that we can read the contents of the file.
    await!(assert_read(&file_proxy, data.len() as u64, data)).expect("read data did not match");
    await!(top_proxy.read_dirents(128)).expect("failed to read top directory entries");

    Ok(())
}

/// Sets up a new filesystem containing a vmofile.
///
/// Returns a future which runs the filesystem, a proxy connected to the vmofile, and the data
/// expected to be in the file.
fn set_up_and_connect_to_vmo_file(
) -> Result<(impl Future<Output = Result<(), Error>>, FileProxy, Vec<u8>), Error> {
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

    // Open a connection to the file within the directory
    let (file_proxy, file_server_end) = create_proxy::<FileMarker>()?;
    let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE;
    let mode = fidl_fuchsia_io::MODE_TYPE_FILE;
    dir_proxy.open(flags, mode, PATH, file_server_end.into_channel().into())?;

    // truncate `data` to just the data that appears in the file
    data.drain(..VMO_FILE_OFFSET);
    data.truncate(VMO_FILE_LENGTH);
    Ok((fs.collect().map(Ok), file_proxy, data))
}

macro_rules! async_test_with_vmo_file {
    (
        |$file_proxy:ident, $file_data:ident|
        $( $test_name:ident => $test_future:expr ,)*
    ) => { $(
        #[run_until_stalled(test)]
        async fn $test_name() -> Result<(), Error> {
            let (serve_fut, $file_proxy, $file_data) = set_up_and_connect_to_vmo_file()?;
            let $file_data = &*$file_data;
            let test_future = $test_future;
            let ((), ()) = await!(try_join(serve_fut, test_future))?;
            Ok(())
        }
    )* }
}

async_test_with_vmo_file![
    |file_proxy, file_data|
    describe_vmo_file => async {
        // Describe the file
        let (status, attrs) = await!(file_proxy.get_attr())?;
        zx::Status::ok(status)?;
        assert!(attrs.mode & fidl_fuchsia_io::MODE_TYPE_FILE != 0);
        assert_eq!(attrs.content_size, file_data.len() as u64);
        assert_eq!(attrs.storage_size, file_data.len() as u64);
        drop(file_proxy);
        Ok(())
    },
    read_from_vmo_file => async {
        // Read the whole file
        await!(assert_read(&file_proxy, file_data.len() as u64, file_data))?;
        drop(file_proxy);
        Ok(())
    },
    seek_around_vmo_file => async {
        // Read the whole file
        await!(assert_read(&file_proxy, file_data.len() as u64, file_data))?;

        // Try and read the whole file again, while our cursor is at the end.
        // This should return no more data.
        await!(assert_read(&file_proxy, file_data.len() as u64, &[]))?;

        // Seek back to 5 bytes from the end and read again.
        let (status, position) = await!(file_proxy.seek(-5, SeekOrigin::End))?;
        zx::Status::ok(status)?;
        assert_eq!(position, file_data.len() as u64 - 5);

        let read_at_count = 10usize;
        let read_at_offset = 4usize;
        let (status, read_at_data) =
            await!(file_proxy.read_at(read_at_count as u64, read_at_offset as u64))?;
        zx::Status::ok(status)?;
        assert_eq!(&*read_at_data, &file_data[read_at_offset..(read_at_offset + read_at_count)]);

        drop(file_proxy);
        Ok(())
    },
    read_from_clone => async {
        // Create a clone of the file
        let (file_proxy_clone, file_clone_server_end) = create_proxy::<FileMarker>()?;
        let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE;
        file_proxy.clone(flags, file_clone_server_end.into_channel().into())?;
        // Read the whole file
        await!(assert_read(&file_proxy, file_data.len() as u64, file_data))?;
        await!(assert_close(&file_proxy))?;
        // Check that our original clone was never moved from position zero nor closed.
        await!(assert_read(&file_proxy_clone, file_data.len() as u64, file_data))?;
        drop(file_proxy);
        drop(file_proxy_clone);
        Ok(())
    },
];
