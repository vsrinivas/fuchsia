// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Unit tests for fuchsia-component that exercise only the public API.

#![cfg(test)]

use {
    anyhow::{Context as _, Error},
    fidl::endpoints::{create_proxy, ServerEnd, UnifiedServiceMarker},
    fidl_fuchsia_component_test::{
        CounterRequest, CounterRequestStream, CounterServiceMarker, CounterServiceRequest,
    },
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, FileMarker, FileProxy, NodeEvent, NodeInfo, NodeMarker,
        NodeProxy, SeekOrigin, Service, OPEN_RIGHT_READABLE,
    },
    files_async::readdir,
    fuchsia_async::{self as fasync, run_until_stalled},
    fuchsia_component::server::{ServiceFs, ServiceFsDir, ServiceObj},
    fuchsia_vfs_pseudo_fs::{
        directory::entry::DirectoryEntry, file::simple::read_only_static, pseudo_directory,
    },
    fuchsia_zircon::{self as zx, HandleBased as _},
    futures::{future::try_join, stream::TryStreamExt, FutureExt, StreamExt},
    matches::assert_matches,
    std::{future::Future, path::Path, sync::atomic},
};

#[run_until_stalled(test)]
async fn complete_with_no_clients() {
    ServiceFs::new().collect().await
}

#[run_until_stalled(test)]
async fn complete_with_no_remaining_clients() {
    let (fs, _) = fs_with_connection();
    fs.collect().await
}

fn fs_with_connection<'a, T: 'a>() -> (ServiceFs<ServiceObj<'a, T>>, DirectoryProxy) {
    let mut fs = ServiceFs::new();
    let (dir_proxy, dir_server_end) =
        create_proxy::<DirectoryMarker>().expect("Unable to create directory proxy");
    fs.serve_connection(dir_server_end.into_channel()).expect("unable to serve main dir proxy");
    (fs, dir_proxy)
}

#[run_until_stalled(test)]
async fn check_bad_flags_root() -> Result<(), Error> {
    let (fs, dir_proxy) = fs_with_connection::<'_, ()>();
    let serve_fut = fs.collect().map(Ok);

    let test_fut = async move {
        // attempt to open . with CREATE flags
        let flags = fidl_fuchsia_io::OPEN_FLAG_DESCRIBE
            | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY
            | fidl_fuchsia_io::OPEN_FLAG_CREATE;
        let mode = fidl_fuchsia_io::MODE_TYPE_DIRECTORY;
        let (node_proxy, server_end) = create_proxy::<NodeMarker>()?;
        dir_proxy.open(flags, mode, ".", server_end.into()).unwrap();
        assert_open_status(&node_proxy, zx::Status::NOT_SUPPORTED).await;
        Ok::<(), Error>(())
    };

    let ((), ()) = try_join(serve_fut, test_fut).await?;
    Ok(())
}

#[run_until_stalled(test)]
async fn check_bad_flags_folder() -> Result<(), Error> {
    let (mut fs, dir_proxy) = fs_with_connection::<'_, ()>();
    fs.dir("foo").add_service_at("bar", |_chan| Some(()));
    let serve_fut = fs.collect().map(Ok);

    let test_fut = async move {
        // attempt to create a folder that already exists in ServiceFS
        let flags = fidl_fuchsia_io::OPEN_FLAG_DESCRIBE
            | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY
            | fidl_fuchsia_io::OPEN_FLAG_CREATE;
        let mode = fidl_fuchsia_io::MODE_TYPE_DIRECTORY;
        let (node_proxy, server_end) = create_proxy::<NodeMarker>()?;
        dir_proxy.open(flags, mode, "foo", server_end.into()).unwrap();
        assert_open_status(&node_proxy, zx::Status::NOT_SUPPORTED).await;
        Ok::<(), Error>(())
    };

    let ((), ()) = try_join(serve_fut, test_fut).await?;
    Ok(())
}

#[run_until_stalled(test)]
async fn check_bad_flags_file() -> Result<(), Error> {
    let (mut fs, dir_proxy) = fs_with_connection::<'_, ()>();
    fs.dir("foo").add_service_at("bar", |_chan| Some(()));
    let serve_fut = fs.collect().map(Ok);

    let test_fut = async move {
        // attempt to create a file that already exists in ServiceFS
        let flags = fidl_fuchsia_io::OPEN_FLAG_DESCRIBE
            | fidl_fuchsia_io::OPEN_FLAG_NOT_DIRECTORY
            | fidl_fuchsia_io::OPEN_FLAG_CREATE
            | fidl_fuchsia_io::OPEN_FLAG_TRUNCATE;
        let mode = fidl_fuchsia_io::MODE_TYPE_FILE;
        let (node_proxy, server_end) = create_proxy::<NodeMarker>()?;
        dir_proxy.open(flags, mode, "foo/bar", server_end.into()).unwrap();
        assert_open_status(&node_proxy, zx::Status::NOT_SUPPORTED).await;
        Ok::<(), Error>(())
    };

    let ((), ()) = try_join(serve_fut, test_fut).await?;
    Ok(())
}

#[run_until_stalled(test)]
async fn check_bad_flags_new_file() -> Result<(), Error> {
    let (fs, dir_proxy) = fs_with_connection::<'_, ()>();
    let serve_fut = fs.collect().map(Ok);

    let test_fut = async move {
        // attempt to create a new file in ServiceFS
        let flags = fidl_fuchsia_io::OPEN_FLAG_DESCRIBE
            | fidl_fuchsia_io::OPEN_FLAG_NOT_DIRECTORY
            | fidl_fuchsia_io::OPEN_FLAG_CREATE
            | fidl_fuchsia_io::OPEN_FLAG_TRUNCATE;
        let mode = fidl_fuchsia_io::MODE_TYPE_FILE;
        let (node_proxy, server_end) = create_proxy::<NodeMarker>()?;
        dir_proxy.open(flags, mode, "qaz", server_end.into()).unwrap();
        assert_open_status(&node_proxy, zx::Status::NOT_SUPPORTED).await;
        Ok::<(), Error>(())
    };

    let ((), ()) = try_join(serve_fut, test_fut).await?;
    Ok(())
}

#[run_until_stalled(test)]
async fn serve_on_root_and_subdir() -> Result<(), Error> {
    const SERVICE_NAME: &str = "foo";

    // Example of serving a dummy service that just throws away
    // the channel on an arbitrary directory.
    fn serve_on_dir(dir: &mut ServiceFsDir<'_, ServiceObj<'_, ()>>) {
        dir.add_service_at(SERVICE_NAME, |_chan| Some(()));
    }

    fn assert_peer_closed(chan: zx::Channel) {
        let err = chan
            .read_raw(&mut vec![], &mut vec![])
            .expect("unexpected too small buffer")
            .0
            .expect_err("should've been a PEER_CLOSED error");
        assert_eq!(err, zx::Status::PEER_CLOSED);
    }

    async fn assert_has_service_child(
        fs: &mut ServiceFs<ServiceObj<'_, ()>>,
        dir_proxy: &DirectoryProxy,
    ) {
        let flags = 0;
        let mode = fidl_fuchsia_io::MODE_TYPE_SERVICE;
        let (server_end, client_end) = zx::Channel::create().expect("create channel");
        dir_proxy.open(flags, mode, SERVICE_NAME, server_end.into()).expect("open");
        fs.next().await.expect("expected one service to have been started");
        assert_peer_closed(client_end);
    }

    let (mut fs, dir_proxy) = fs_with_connection();

    // serve at both the root directory and a child dir /fooey
    serve_on_dir(&mut fs.root_dir());
    serve_on_dir(&mut fs.dir("fooey"));

    // attempt to connect to the root service
    assert_has_service_child(&mut fs, &dir_proxy).await;

    // attempt to connect to the /fooey dir
    let flags = fidl_fuchsia_io::OPEN_FLAG_DIRECTORY;
    let mode = fidl_fuchsia_io::MODE_TYPE_DIRECTORY;
    let (subdir_proxy, server_end) = create_proxy::<DirectoryMarker>()?;
    dir_proxy.open(flags, mode, "fooey", server_end.into_channel().into())?;

    // attempt to connect ot the service under /fooey
    assert_has_service_child(&mut fs, &subdir_proxy).await;

    // drop the connections and ensure the fs has no outstanding
    // clients or service connections
    drop(dir_proxy);
    drop(subdir_proxy);
    assert!(fs.next().await.is_none());

    Ok(())
}

#[run_until_stalled(test)]
async fn open_service_node_reference() -> Result<(), Error> {
    const PATH: &str = "service_name";

    let (mut fs, dir_proxy) = fs_with_connection();
    fs.add_service_at(PATH, |_chan| Some(()));
    let serve_fut = fs.collect().map(Ok);

    let open_reference_fut = async {
        let flags = fidl_fuchsia_io::OPEN_FLAG_NODE_REFERENCE;
        let mode = fidl_fuchsia_io::MODE_TYPE_SERVICE;
        let (node_proxy, node_server_end) = create_proxy::<NodeMarker>()?;
        dir_proxy.open(flags, mode, PATH, node_server_end)?;
        drop(dir_proxy);

        let info = node_proxy.describe().await?;
        if let NodeInfo::Service(Service {}) = info {
            // ok
        } else {
            panic!("expected service node, found {:?}", info);
        }
        drop(node_proxy);
        Ok::<(), Error>(())
    };

    let ((), ()) = try_join(serve_fut, open_reference_fut).await?;
    Ok(())
}

// Identical to `open_service_node_reference`, but clones the opened directory before attempting to
// open the service node.
#[run_until_stalled(test)]
async fn clone_service_dir() -> Result<(), Error> {
    const PATH: &str = "service_name";

    let (mut fs, dir_proxy) = fs_with_connection();
    fs.add_service_at(PATH, |_chan| Some(()));
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

        let info = node_proxy.describe().await?;
        if let NodeInfo::Service(Service {}) = info {
            // ok
        } else {
            panic!("expected service node, found {:?}", info);
        }
        drop(node_proxy);
        Ok::<(), Error>(())
    };

    let ((), ()) = try_join(serve_fut, open_reference_fut).await?;
    Ok(())
}

#[run_until_stalled(test)]
async fn handles_dir_not_dir_flags() -> Result<(), Error> {
    let service_open_count = atomic::AtomicU32::new(0);

    let (mut fs, root_proxy) = fs_with_connection();
    fs.dir("dir").add_service_at("notdir", |chan| {
        service_open_count.fetch_add(1, atomic::Ordering::SeqCst);
        let (_request_stream, control_handle) =
            ServerEnd::<NodeMarker>::new(chan).into_stream_and_control_handle().unwrap();
        control_handle.send_on_open_(zx::Status::OK.into_raw(), None).unwrap();
        None
    });
    let serve_fut = fs.collect().map(Ok);

    let service_open_count = &service_open_count;
    let test_fut = async move {
        let dir_flags = fidl_fuchsia_io::OPEN_FLAG_DESCRIBE | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY;
        let not_dir_flags =
            fidl_fuchsia_io::OPEN_FLAG_DESCRIBE | fidl_fuchsia_io::OPEN_FLAG_NOT_DIRECTORY;

        // Verify flags when opening a directory.
        let (node_proxy, node_end) = create_proxy::<NodeMarker>()?;
        root_proxy.open(dir_flags, 0, "dir", node_end)?;
        assert_open_status(&node_proxy, zx::Status::OK).await;

        let (node_proxy, node_end) = create_proxy::<NodeMarker>()?;
        root_proxy.open(not_dir_flags, 0, "dir", node_end)?;
        assert_open_status(&node_proxy, zx::Status::NOT_FILE).await;

        // Verify flags when opening a file.
        assert_eq!(service_open_count.load(atomic::Ordering::SeqCst), 0);
        let (node_proxy, node_end) = create_proxy::<NodeMarker>()?;
        root_proxy.open(dir_flags, 0, "dir/notdir", node_end)?;
        assert_open_status(&node_proxy, zx::Status::NOT_DIR).await;
        assert_eq!(service_open_count.load(atomic::Ordering::SeqCst), 0);

        let (node_proxy, node_end) = create_proxy::<NodeMarker>()?;
        root_proxy.open(not_dir_flags, 0, "dir/notdir", node_end)?;
        assert_open_status(&node_proxy, zx::Status::OK).await;
        assert_eq!(service_open_count.load(atomic::Ordering::SeqCst), 1);

        Ok::<(), Error>(())
    };

    let ((), ()) = try_join(serve_fut, test_fut).await?;
    Ok(())
}

/// Serves a dummy unified service `US` that immediately drops the request channel.
fn serve_dummy_unified_service<US>() -> (impl Future<Output = Result<(), Error>>, DirectoryProxy)
where
    US: UnifiedServiceMarker,
{
    let (mut fs, dir_proxy) = fs_with_connection();
    fs.add_unified_service(|_: US::Request| ());
    (fs.collect().map(Ok), dir_proxy)
}

/// Serves an instance of a dummy unified service `US` named `instance` that immediately drops the request channel.
fn serve_dummy_unified_service_instance<US>(
    instance: &str,
) -> (impl Future<Output = Result<(), Error>>, DirectoryProxy)
where
    US: UnifiedServiceMarker,
{
    let (mut fs, dir_proxy) = fs_with_connection();
    fs.add_unified_service_instance(instance, |_: US::Request| ());
    (fs.collect().map(Ok), dir_proxy)
}

/// Returns the NodeInfo of a node reference located at `path` under the directory `dir_proxy`.
async fn node_reference_type_at_path(
    dir_proxy: &DirectoryProxy,
    path: &str,
) -> Result<NodeInfo, Error> {
    let flags = fidl_fuchsia_io::OPEN_FLAG_NODE_REFERENCE;
    let mode = fidl_fuchsia_io::MODE_TYPE_DIRECTORY;
    let (node_proxy, node_server_end) = create_proxy::<NodeMarker>()?;
    dir_proxy.open(flags, mode, path, node_server_end)?;
    Ok(node_proxy.describe().await?)
}

#[run_until_stalled(test)]
async fn open_unified_service_node_reference() -> Result<(), Error> {
    let (serve_fut, dir_proxy) = serve_dummy_unified_service::<CounterServiceMarker>();

    let open_reference_fut = async {
        let info = node_reference_type_at_path(&dir_proxy, CounterServiceMarker::SERVICE_NAME)
            .await
            .expect("failed to get NodeInfo");
        assert_matches!(info, NodeInfo::Directory(_));
        drop(dir_proxy);
        Ok::<(), Error>(())
    };

    let ((), ()) = try_join(serve_fut, open_reference_fut).await?;
    Ok(())
}

#[run_until_stalled(test)]
async fn open_unified_service_instance_node_reference() -> Result<(), Error> {
    let (serve_fut, dir_proxy) = serve_dummy_unified_service::<CounterServiceMarker>();

    let open_reference_fut = async {
        let path = format!("{}/default", CounterServiceMarker::SERVICE_NAME);
        let info =
            node_reference_type_at_path(&dir_proxy, &path).await.expect("failed to get NodeInfo");
        assert_matches!(info, NodeInfo::Directory(_));
        drop(dir_proxy);
        Ok::<(), Error>(())
    };

    let ((), ()) = try_join(serve_fut, open_reference_fut).await?;
    Ok(())
}

async fn list_service_entries(
    dir_proxy: &DirectoryProxy,
    path: &str,
) -> Result<Vec<String>, Error> {
    let flags = fidl_fuchsia_io::OPEN_FLAG_DIRECTORY
        | fidl_fuchsia_io::OPEN_RIGHT_READABLE
        | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE;
    let instance_proxy = io_util::open_directory(dir_proxy, Path::new(&path), flags)?;
    let mut entries =
        files_async::readdir(&instance_proxy)
            .await?
            .into_iter()
            .filter_map(|e| {
                if let files_async::DirentKind::Service = e.kind {
                    Some(e.name)
                } else {
                    None
                }
            })
            .collect::<Vec<_>>();
    entries.sort();
    Ok(entries)
}

#[run_until_stalled(test)]
async fn list_unified_service_members_of_default_instance() -> Result<(), Error> {
    let (serve_fut, dir_proxy) = serve_dummy_unified_service::<CounterServiceMarker>();

    let open_reference_fut = async {
        let path = format!("{}/default", CounterServiceMarker::SERVICE_NAME);
        let entries =
            list_service_entries(&dir_proxy, &path).await.expect("failed to list service entries");
        assert_eq!(vec![String::from("counter"), String::from("counter_v2")], entries);
        drop(dir_proxy);
        Ok::<(), Error>(())
    };

    let ((), ()) = try_join(serve_fut, open_reference_fut).await?;
    Ok(())
}

#[run_until_stalled(test)]
async fn list_unified_service_members_of_other_instance() -> Result<(), Error> {
    let (serve_fut, dir_proxy) =
        serve_dummy_unified_service_instance::<CounterServiceMarker>("other");

    let open_reference_fut = async {
        let path = format!("{}/other", CounterServiceMarker::SERVICE_NAME);
        let entries =
            list_service_entries(&dir_proxy, &path).await.expect("failed to list service entries");
        assert_eq!(vec![String::from("counter"), String::from("counter_v2")], entries);
        drop(dir_proxy);
        Ok::<(), Error>(())
    };

    let ((), ()) = try_join(serve_fut, open_reference_fut).await?;
    Ok(())
}

async fn serve_get_and_increment(mut stream: CounterRequestStream) -> Result<(), Error> {
    let mut value: u32 = 0;
    while let Some(CounterRequest::GetAndIncrement { responder }) =
        stream.try_next().await.context("error running fetching next request")?
    {
        responder.send(value).context("error sending response")?;
        value += 1;
    }
    Ok(())
}

#[run_until_stalled(test)]
async fn connect_to_unified_service_member_of_default_instance() -> Result<(), Error> {
    enum IncomingService {
        Counter(CounterServiceRequest),
    }

    let (mut fs, dir_proxy) = fs_with_connection();

    // Serve the default instance of CounterService.
    fs.add_unified_service(IncomingService::Counter);
    let serve_fut = fs.for_each_concurrent(1, |IncomingService::Counter(req)| {
        async {
            match req {
                CounterServiceRequest::Counter(stream) => {
                    serve_get_and_increment(stream).await.expect("serving Counter failed")
                }
                CounterServiceRequest::CounterV2(_) => (), // Not under test
            }
        }
    });
    fasync::Task::spawn(serve_fut).detach();

    let dir_request =
        dir_proxy.into_channel().expect("failed to extract channel from proxy").into_zx_channel();

    // Connect to the default instance of CounterService and make calls to the "counter" member.
    let service_proxy = fuchsia_component::client::connect_to_unified_service_at_dir::<
        CounterServiceMarker,
    >(&dir_request)?;
    let counter_proxy = service_proxy.counter().expect("failed conencting to counter member");
    let value: u32 =
        counter_proxy.get_and_increment().await.expect("first call to get_and_increment failed");
    assert_eq!(value, 0);
    let value =
        counter_proxy.get_and_increment().await.expect("second call to get_and_increment failed");
    assert_eq!(value, 1);
    drop(dir_request);
    Ok(())
}

async fn assert_open_status(proxy: &NodeProxy, expected: zx::Status) {
    let mut events = proxy.take_event_stream();
    let NodeEvent::OnOpen_ { s: actual, info: _ } =
        events.next().await.expect("event").expect("no error");

    assert_eq!(zx::Status::from_raw(actual), expected);
}

async fn assert_read<'a>(
    file_proxy: &'a FileProxy,
    length: u64,
    expected: &'a [u8],
) -> Result<(), Error> {
    let (status, read_data) = file_proxy.read(length).await?;
    zx::Status::ok(status)?;
    assert_eq!(&*read_data, expected);
    Ok(())
}

// close the file and check that further reads fail.
async fn assert_close(file_proxy: &FileProxy) -> Result<(), Error> {
    let status = file_proxy.close().await?;
    zx::Status::ok(status)?;
    assert!(file_proxy.read(0).await.is_err());
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

    fuchsia_async::Task::spawn(root.collect::<()>()).detach();
    fuchsia_async::Task::spawn(fs.collect::<()>()).detach();

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
    assert_read(&file_proxy, data.len() as u64, data).await.expect("read data did not match");
    top_proxy.read_dirents(128).await.expect("failed to read top directory entries");

    Ok(())
}

#[run_until_stalled(test)]
async fn open_remote_pseudo_directory_files() -> Result<(), Error> {
    let data = "test";
    let mut root = pseudo_directory! {
        "test.txt" => read_only_static(data)
    };
    let mut fs = ServiceFs::new();
    let (remote_proxy, remote_server_end) = create_proxy::<DirectoryMarker>()?;

    root.open(
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        0,
        &mut vec![].into_iter(),
        fidl::endpoints::ServerEnd::<NodeMarker>::from(remote_server_end.into_channel()),
    );

    // Add the remote as "test"
    fs.add_remote("test", remote_proxy);
    let (dir_proxy, dir_server_end) = create_proxy::<DirectoryMarker>()?;
    fs.serve_connection(dir_server_end.into_channel())?;

    fuchsia_async::Task::spawn(async move {
        root.await;
    })
    .detach();
    fuchsia_async::Task::spawn(fs.collect::<()>()).detach();

    // Open the test file
    let (file_proxy, file_server_end) = create_proxy::<FileMarker>()?;
    let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE;
    let mode = fidl_fuchsia_io::MODE_TYPE_FILE;
    dir_proxy.open(flags, mode, "test/test.txt", file_server_end.into_channel().into())?;

    // Check that we can read the contents of the file.
    assert_read(&file_proxy, data.len() as u64, data.as_bytes())
        .await
        .expect("could not read expected data");

    Ok(())
}

#[run_until_stalled(test)]
async fn open_remote_nested_servicefs_files() -> Result<(), Error> {
    let mut fs = ServiceFs::new();

    // Create a nested ServiceFs contains directories "temp/folder".
    let mut nested_fs = ServiceFs::new();
    let (nested_proxy, nested_server_end) = create_proxy::<DirectoryMarker>()?;
    nested_fs.dir("temp").dir("folder");
    nested_fs.serve_connection(nested_server_end.into_channel())?;
    fuchsia_async::Task::spawn(nested_fs.collect::<()>()).detach();

    // Add the remote as "test"
    // "temp/folder" should appear in this directory as "test/folder".
    fs.add_remote(
        "test",
        io_util::open_directory(&nested_proxy, Path::new("temp"), OPEN_RIGHT_READABLE)?,
    );
    let (dir_proxy, dir_server_end) = create_proxy::<DirectoryMarker>()?;
    fs.serve_connection(dir_server_end.into_channel())?;

    fuchsia_async::Task::spawn(fs.collect::<()>()).detach();

    // Open and read "test"
    let temp_proxy = io_util::open_directory(&dir_proxy, Path::new("test"), OPEN_RIGHT_READABLE)?;
    let result = readdir(&temp_proxy).await;
    assert!(!result.is_err(), "got Err instead of Ok: {:?}", result.unwrap_err());
    let files = result.unwrap();
    assert_eq!(1, files.len());
    assert_eq!("folder", files[0].name);

    Ok(())
}

#[run_until_stalled(test)]
async fn create_nested_env_with_sub_dir() {
    for dir_name in &["svc", "bin", "foobar"] {
        let mut fs: ServiceFs<ServiceObj<'_, ()>> = ServiceFs::new();

        fs.dir(*dir_name).add_service_at("test", |_chan: zx::Channel| None);
        if fs.create_nested_environment("should-not-be-created").is_ok() {
            panic!("create_nested_environment should fail when a svc dir exists");
        }
    }
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

    let (mut fs, dir_proxy) = fs_with_connection();

    let vmo = zx::Vmo::create(VMO_SIZE)?;
    vmo.write(&*data, 0)?;

    fs.add_vmo_file_at(
        PATH,
        vmo.duplicate_handle(zx::Rights::READ)?,
        VMO_FILE_OFFSET as u64,
        VMO_FILE_LENGTH as u64,
    );

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
            let ((), ()) = try_join(serve_fut, test_future).await?;
            Ok(())
        }
    )* }
}

async_test_with_vmo_file![
    |file_proxy, file_data|
    describe_vmo_file => async {
        // Describe the file
        let (status, attrs) = file_proxy.get_attr().await?;
        zx::Status::ok(status)?;
        assert!(attrs.mode & fidl_fuchsia_io::MODE_TYPE_FILE != 0);
        assert_eq!(attrs.content_size, file_data.len() as u64);
        assert_eq!(attrs.storage_size, file_data.len() as u64);
        drop(file_proxy);
        Ok(())
    },
    read_from_vmo_file => async {
        // Read the whole file
        assert_read(&file_proxy, file_data.len() as u64, file_data).await?;
        drop(file_proxy);
        Ok(())
    },
    seek_around_vmo_file => async {
        // Read the whole file
        assert_read(&file_proxy, file_data.len() as u64, file_data).await?;

        // Try and read the whole file again, while our cursor is at the end.
        // This should return no more data.
        assert_read(&file_proxy, file_data.len() as u64, &[]).await?;

        // Seek back to 5 bytes from the end and read again.
        let (status, position) = file_proxy.seek(-5, SeekOrigin::End).await?;
        zx::Status::ok(status)?;
        assert_eq!(position, file_data.len() as u64 - 5);

        let read_at_count = 10usize;
        let read_at_offset = 4usize;
        let (status, read_at_data) =
            file_proxy.read_at(read_at_count as u64, read_at_offset as u64).await?;
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
        assert_read(&file_proxy, file_data.len() as u64, file_data).await?;
        assert_close(&file_proxy).await?;
        // Check that our original clone was never moved from position zero nor closed.
        assert_read(&file_proxy_clone, file_data.len() as u64, file_data).await?;
        drop(file_proxy);
        drop(file_proxy_clone);
        Ok(())
    },
];
