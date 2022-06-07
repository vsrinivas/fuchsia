// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utility functions for fuchsia.io directories.

use {
    crate::node::{self, CloneError, CloseError, OpenError, RenameError},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio, fuchsia_zircon_status as zx_status,
};

/// Opens the given `path` from the current namespace as a [`DirectoryProxy`]. The target is not
/// verified to be any particular type and may not implement the fuchsia.io.Directory protocol.
#[cfg(target_os = "fuchsia")]
pub fn open_in_namespace(
    path: &str,
    flags: fio::OpenFlags,
) -> Result<fio::DirectoryProxy, OpenError> {
    let (dir, server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().map_err(OpenError::CreateProxy)?;

    let flags = flags | fio::OpenFlags::DIRECTORY;
    node::connect_in_namespace(path, flags, server_end.into_channel())
        .map_err(OpenError::Namespace)?;

    Ok(dir)
}

/// Opens the given `path` from the given `parent` directory as a [`DirectoryProxy`]. The target is
/// not verified to be any particular type and may not implement the fuchsia.io.Directory protocol.
pub fn open_directory_no_describe(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
) -> Result<fio::DirectoryProxy, OpenError> {
    let (dir, server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().map_err(OpenError::CreateProxy)?;

    let flags = flags | fio::OpenFlags::DIRECTORY;
    let mode = fio::MODE_TYPE_DIRECTORY;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    Ok(dir)
}

/// Opens the given `path` from given `parent` directory as a [`DirectoryProxy`], verifying that
/// the target implements the fuchsia.io.Directory protocol.
pub async fn open_directory(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
) -> Result<fio::DirectoryProxy, OpenError> {
    let (dir, server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().map_err(OpenError::CreateProxy)?;

    let flags = flags | fio::OpenFlags::DIRECTORY | fio::OpenFlags::DESCRIBE;
    let mode = fio::MODE_TYPE_DIRECTORY;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    // wait for the directory to open and report success.
    node::verify_directory_describe_event(dir).await
}

/// Creates a directory named `path` within the `parent` directory.
pub async fn create_directory(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
) -> Result<fio::DirectoryProxy, OpenError> {
    let (dir, server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().map_err(OpenError::CreateProxy)?;

    // NB: POSIX does not allow open(2) to create dirs, but fuchsia.io does not have an equivalent
    // of mkdir(2), so on Fuchsia we're expected to call open on a DirectoryMarker with (flags &
    // OPEN_FLAG_CREATE) set.
    // (mode & MODE_TYPE_DIRECTORY) is also required, although it is redundant (the fact that we
    // opened a DirectoryMarker is the main way that the underlying filesystem understands our
    // intention.)
    let flags =
        flags | fio::OpenFlags::CREATE | fio::OpenFlags::DIRECTORY | fio::OpenFlags::DESCRIBE;
    let mode = fio::MODE_TYPE_DIRECTORY;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    // wait for the directory to open and report success.
    node::verify_directory_describe_event(dir).await
}

/// Opens the given `path` from the given `parent` directory as a [`FileProxy`]. The target is not
/// verified to be any particular type and may not implement the fuchsia.io.File protocol.
pub fn open_file_no_describe(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
) -> Result<fio::FileProxy, OpenError> {
    let (file, server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().map_err(OpenError::CreateProxy)?;

    let mode = fio::MODE_TYPE_FILE;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    Ok(file)
}

/// Opens the given `path` from given `parent` directory as a [`FileProxy`], verifying that the
/// target implements the fuchsia.io.File protocol.
pub async fn open_file(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
) -> Result<fio::FileProxy, OpenError> {
    let (file, server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().map_err(OpenError::CreateProxy)?;

    let flags = flags | fio::OpenFlags::DESCRIBE;
    let mode = fio::MODE_TYPE_FILE;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    // wait for the file to open and report success.
    node::verify_file_describe_event(file).await
}

/// Opens the given `path` from the given `parent` directory as a [`NodeProxy`], verifying that the
/// target implements the fuchsia.io.Node protocol.
pub async fn open_node(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
    mode: u32,
) -> Result<fio::NodeProxy, OpenError> {
    let (file, server_end) =
        fidl::endpoints::create_proxy::<fio::NodeMarker>().map_err(OpenError::CreateProxy)?;

    let flags = flags | fio::OpenFlags::DESCRIBE;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    // wait for the file to open and report success.
    node::verify_node_describe_event(file).await
}

/// Opens the given `path` from the given `parent` directory as a [`NodeProxy`]. The target is not
/// verified to be any particular type and may not implement the fuchsia.io.Node protocol.
pub fn open_node_no_describe(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
    mode: u32,
) -> Result<fio::NodeProxy, OpenError> {
    let (file, server_end) =
        fidl::endpoints::create_proxy::<fio::NodeMarker>().map_err(OpenError::CreateProxy)?;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    Ok(file)
}

/// Opens a new connection to the given directory using `flags` if provided, or
/// `fidl_fuchsia_io::OpenFlags::SAME_RIGHTS` otherwise.
pub fn clone_no_describe(
    dir: &fio::DirectoryProxy,
    flags: Option<fio::OpenFlags>,
) -> Result<fio::DirectoryProxy, CloneError> {
    let (clone, server_end) = fidl::endpoints::create_proxy().map_err(CloneError::CreateProxy)?;
    clone_onto_no_describe(dir, flags, server_end)?;
    Ok(clone)
}

/// Opens a new connection to the given directory onto the given server end using `flags` if
/// provided, or `fidl_fuchsia_io::OpenFlags::SAME_RIGHTS` otherwise.
pub fn clone_onto_no_describe(
    dir: &fio::DirectoryProxy,
    flags: Option<fio::OpenFlags>,
    request: ServerEnd<fio::DirectoryMarker>,
) -> Result<(), CloneError> {
    let node_request = ServerEnd::new(request.into_channel());
    let flags = flags.unwrap_or(fio::OpenFlags::CLONE_SAME_RIGHTS);

    dir.clone(flags, node_request).map_err(CloneError::SendCloneRequest)?;
    Ok(())
}

/// Gracefully closes the directory proxy from the remote end.
pub async fn close(dir: fio::DirectoryProxy) -> Result<(), CloseError> {
    let result = dir.close().await.map_err(CloseError::SendCloseRequest)?;
    result.map_err(|s| CloseError::CloseError(zx_status::Status::from_raw(s)))
}

/// Create a randomly named file in the given directory with the given prefix, and return its path
/// and `FileProxy`. `prefix` may contain "/".
pub async fn create_randomly_named_file(
    dir: &fio::DirectoryProxy,
    prefix: &str,
    flags: fio::OpenFlags,
) -> Result<(String, fio::FileProxy), OpenError> {
    use rand::{
        distributions::{Alphanumeric, DistString as _},
        SeedableRng as _,
    };
    let mut rng = rand::rngs::SmallRng::from_entropy();

    let flags = flags | fio::OpenFlags::CREATE | fio::OpenFlags::CREATE_IF_ABSENT;

    loop {
        let random_string = Alphanumeric.sample_string(&mut rng, 6);
        let path = prefix.to_string() + &random_string;

        match open_file(dir, &path, flags).await {
            Ok(file) => return Ok((path, file)),
            Err(OpenError::OpenError(zx_status::Status::ALREADY_EXISTS)) => {}
            Err(err) => return Err(err),
        }
    }
}

// Split the given path under the directory into parent and file name, and open the parent directory
// if the path contains "/".
async fn split_path<'a>(
    dir: &fio::DirectoryProxy,
    path: &'a str,
) -> Result<(Option<fio::DirectoryProxy>, &'a str), OpenError> {
    match path.rsplit_once('/') {
        Some((parent, name)) => {
            let proxy = open_directory(dir, parent, fio::OpenFlags::RIGHT_WRITABLE).await?;
            Ok((Some(proxy), name))
        }
        None => Ok((None, path)),
    }
}

/// Rename `src` to `dst` under the given directory, `src` and `dst` may contain "/".
pub async fn rename(dir: &fio::DirectoryProxy, src: &str, dst: &str) -> Result<(), RenameError> {
    let (src_parent, src_filename) = split_path(dir, src).await?;
    let src_parent = src_parent.as_ref().unwrap_or(dir);
    let (dst_parent, dst_filename) = split_path(dir, dst).await?;
    let dst_parent = dst_parent.as_ref().unwrap_or(dir);
    let (status, dst_parent_dir_token) =
        dst_parent.get_token().await.map_err(RenameError::SendGetTokenRequest)?;
    zx_status::Status::ok(status).map_err(RenameError::GetTokenError)?;
    let event = fidl::Event::from(dst_parent_dir_token.ok_or(RenameError::NoHandleError)?);
    src_parent
        .rename(src_filename, event, dst_filename)
        .await
        .map_err(RenameError::SendRenameRequest)?
        .map_err(|s| RenameError::RenameError(zx_status::Status::from_raw(s)))
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        futures::prelude::*,
        tempfile::TempDir,
        vfs::{
            directory::entry::DirectoryEntry,
            execution_scope::ExecutionScope,
            file::vmo::{read_only_static, read_write, simple_init_vmo_with_capacity},
            pseudo_directory,
        },
    };

    const DATA_FILE_CONTENTS: &str = "Hello World!\n";

    fn open_pkg() -> fio::DirectoryProxy {
        open_in_namespace("/pkg", fio::OpenFlags::RIGHT_READABLE).unwrap()
    }

    fn open_tmp() -> (TempDir, fio::DirectoryProxy) {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        let proxy = open_in_namespace(
            tempdir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        (tempdir, proxy)
    }

    // open_in_namespace

    #[fasync::run_singlethreaded(test)]
    async fn open_in_namespace_opens_real_dir() {
        let exists = open_in_namespace("/pkg", fio::OpenFlags::RIGHT_READABLE).unwrap();
        assert_matches!(close(exists).await, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_in_namespace_opens_fake_subdir_of_root_namespace_entry() {
        let notfound = open_in_namespace("/pkg/fake", fio::OpenFlags::RIGHT_READABLE).unwrap();
        // The open error is not detected until the proxy is interacted with.
        assert_matches!(close(notfound).await, Err(_));
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_in_namespace_rejects_fake_root_namespace_entry() {
        assert_matches!(
            open_in_namespace("/fake", fio::OpenFlags::RIGHT_READABLE),
            Err(OpenError::Namespace(zx_status::Status::NOT_FOUND))
        );
    }

    // open_directory_no_describe

    #[fasync::run_singlethreaded(test)]
    async fn open_directory_no_describe_opens_real_dir() {
        let pkg = open_pkg();
        let data =
            open_directory_no_describe(&pkg, "data", fio::OpenFlags::RIGHT_READABLE).unwrap();
        close(data).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_directory_no_describe_opens_fake_dir() {
        let pkg = open_pkg();
        let fake =
            open_directory_no_describe(&pkg, "fake", fio::OpenFlags::RIGHT_READABLE).unwrap();
        // The open error is not detected until the proxy is interacted with.
        assert_matches!(close(fake).await, Err(_));
    }

    // open_directory

    #[fasync::run_singlethreaded(test)]
    async fn open_directory_opens_real_dir() {
        let pkg = open_pkg();
        let data = open_directory(&pkg, "data", fio::OpenFlags::RIGHT_READABLE).await.unwrap();
        close(data).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_directory_rejects_fake_dir() {
        let pkg = open_pkg();

        assert_matches!(
            open_directory(&pkg, "fake", fio::OpenFlags::RIGHT_READABLE).await,
            Err(OpenError::OpenError(zx_status::Status::NOT_FOUND))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_directory_rejects_file() {
        let pkg = open_pkg();

        assert_matches!(
            open_directory(&pkg, "data/file", fio::OpenFlags::RIGHT_READABLE).await,
            Err(OpenError::OpenError(zx_status::Status::NOT_DIR))
        );
    }

    // create_directory

    #[fasync::run_singlethreaded(test)]
    async fn create_directory_simple() {
        let (_tmp, proxy) = open_tmp();
        let dir = create_directory(&proxy, "dir", fio::OpenFlags::RIGHT_READABLE).await.unwrap();
        crate::directory::close(dir).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_directory_add_file() {
        let (_tmp, proxy) = open_tmp();
        let dir = create_directory(
            &proxy,
            "dir",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .unwrap();
        let file = open_file(
            &dir,
            "data",
            fio::OpenFlags::CREATE
                | fio::OpenFlags::CREATE_IF_ABSENT
                | fio::OpenFlags::RIGHT_READABLE,
        )
        .await
        .unwrap();
        crate::file::close(file).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_directory_existing_dir_opens() {
        let (_tmp, proxy) = open_tmp();
        let dir = create_directory(&proxy, "dir", fio::OpenFlags::RIGHT_READABLE).await.unwrap();
        crate::directory::close(dir).await.unwrap();
        create_directory(&proxy, "dir", fio::OpenFlags::RIGHT_READABLE).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_directory_existing_dir_fails_if_flag_set() {
        let (_tmp, proxy) = open_tmp();
        let dir = create_directory(
            &proxy,
            "dir",
            fio::OpenFlags::CREATE_IF_ABSENT | fio::OpenFlags::RIGHT_READABLE,
        )
        .await
        .unwrap();
        crate::directory::close(dir).await.unwrap();
        assert_matches!(
            create_directory(
                &proxy,
                "dir",
                fio::OpenFlags::CREATE_IF_ABSENT | fio::OpenFlags::RIGHT_READABLE
            )
            .await,
            Err(_)
        );
    }

    // open_file_no_describe

    #[fasync::run_singlethreaded(test)]
    async fn open_file_no_describe_opens_real_file() {
        let pkg = open_pkg();
        let file =
            open_file_no_describe(&pkg, "data/file", fio::OpenFlags::RIGHT_READABLE).unwrap();
        crate::file::close(file).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_file_no_describe_opens_fake_file() {
        let pkg = open_pkg();
        let fake =
            open_file_no_describe(&pkg, "data/fake", fio::OpenFlags::RIGHT_READABLE).unwrap();
        // The open error is not detected until the proxy is interacted with.
        assert_matches!(crate::file::close(fake).await, Err(_));
    }

    // open_file

    #[fasync::run_singlethreaded(test)]
    async fn open_file_opens_real_file() {
        let pkg = open_pkg();
        let file = open_file(&pkg, "data/file", fio::OpenFlags::RIGHT_READABLE).await.unwrap();
        assert_eq!(
            file.seek(fio::SeekOrigin::End, 0).await.unwrap(),
            Ok(DATA_FILE_CONTENTS.len() as u64),
        );
        crate::file::close(file).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_file_rejects_fake_file() {
        let pkg = open_pkg();

        assert_matches!(
            open_file(&pkg, "data/fake", fio::OpenFlags::RIGHT_READABLE).await,
            Err(OpenError::OpenError(zx_status::Status::NOT_FOUND))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_file_rejects_dir() {
        let pkg = open_pkg();

        assert_matches!(
            open_file(&pkg, "data", fio::OpenFlags::RIGHT_READABLE).await,
            Err(OpenError::UnexpectedNodeKind {
                expected: node::Kind::File,
                actual: node::Kind::Directory,
            })
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn open_file_flags() {
        let example_dir = pseudo_directory! {
            "read_only" => read_only_static("read_only"),
            "read_write" => read_write(
                simple_init_vmo_with_capacity("read_write".as_bytes(), 100)
            ),
        };
        let (example_dir_proxy, example_dir_service) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        example_dir.open(
            scope,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            ServerEnd::new(example_dir_service.into_channel()),
        );

        for (file_name, flags, should_succeed) in vec![
            ("read_only", fio::OpenFlags::RIGHT_READABLE, true),
            ("read_only", fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE, false),
            ("read_only", fio::OpenFlags::RIGHT_WRITABLE, false),
            ("read_write", fio::OpenFlags::RIGHT_READABLE, true),
            ("read_write", fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE, true),
            ("read_write", fio::OpenFlags::RIGHT_WRITABLE, true),
        ] {
            // open_file_no_describe

            let file = open_file_no_describe(&example_dir_proxy, file_name, flags).unwrap();
            match (should_succeed, file.describe().await) {
                (true, Ok(_)) => (),
                (false, Err(_)) => continue,
                (true, Err(e)) => {
                    panic!("failed to open when expected success, couldn't describe: {:?}", e)
                }
                (false, Ok(d)) => {
                    panic!("successfully opened when expected failure, could describe: {:?}", d)
                }
            }
            if flags.intersects(fio::OpenFlags::RIGHT_READABLE) {
                assert_eq!(crate::file::read_to_string(&file).await.unwrap(), file_name);
            }
            if flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
                let _ = file.seek(fio::SeekOrigin::Start, 0).await.expect("Seek failed!");
                let _: u64 = file
                    .write(b"read_write")
                    .await
                    .unwrap()
                    .map_err(zx_status::Status::from_raw)
                    .unwrap();
            }
            crate::file::close(file).await.unwrap();

            // open_file

            match open_file(&example_dir_proxy, file_name, flags).await {
                Ok(file) if should_succeed => {
                    if flags.intersects(fio::OpenFlags::RIGHT_READABLE) {
                        assert_eq!(crate::file::read_to_string(&file).await.unwrap(), file_name);
                    }
                    if flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
                        let _ = file.seek(fio::SeekOrigin::Start, 0).await.expect("Seek failed!");
                        let _: u64 = file
                            .write(b"read_write")
                            .await
                            .unwrap()
                            .map_err(zx_status::Status::from_raw)
                            .unwrap();
                    }
                    crate::file::close(file).await.unwrap();
                }
                Ok(_) => {
                    panic!("successfully opened when expected failure: {:?}", (file_name, flags))
                }
                Err(e) if should_succeed => {
                    panic!("failed to open when expected success: {:?}", (e, file_name, flags))
                }
                Err(_) => {}
            }
        }
    }

    // open_node_no_describe

    #[fasync::run_singlethreaded(test)]
    async fn open_node_no_describe_opens_real_node() {
        let pkg = open_pkg();
        let node = open_node_no_describe(&pkg, "data", fio::OpenFlags::RIGHT_READABLE, 0).unwrap();
        crate::node::close(node).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_node_no_describe_opens_fake_node() {
        let pkg = open_pkg();
        let fake = open_node_no_describe(&pkg, "fake", fio::OpenFlags::RIGHT_READABLE, 0).unwrap();
        // The open error is not detected until the proxy is interacted with.
        assert_matches!(crate::node::close(fake).await, Err(_));
    }

    // open_node

    #[fasync::run_singlethreaded(test)]
    async fn open_node_opens_real_node() {
        let pkg = open_pkg();
        let node = open_node(&pkg, "data", fio::OpenFlags::RIGHT_READABLE, 0).await.unwrap();
        crate::node::close(node).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_node_opens_fake_node() {
        let pkg = open_pkg();
        // The open error should be detected immediately.
        assert_matches!(open_node(&pkg, "fake", fio::OpenFlags::RIGHT_READABLE, 0).await, Err(_));
    }

    // clone_no_describe

    #[fasync::run_singlethreaded(test)]
    async fn clone_no_describe_no_flags_same_rights() {
        let (dir, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fio::DirectoryMarker>().unwrap();

        clone_no_describe(&dir, None).unwrap();

        assert_matches!(
            stream.next().await,
            Some(Ok(fio::DirectoryRequest::Clone { flags: fio::OpenFlags::CLONE_SAME_RIGHTS, .. }))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn clone_no_describe_flags_passed_through() {
        let (dir, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fio::DirectoryMarker>().unwrap();

        const FLAGS: fio::OpenFlags = fio::OpenFlags::DIRECTORY;

        clone_no_describe(&dir, Some(FLAGS)).unwrap();

        assert_matches!(
            stream.next().await,
            Some(Ok(fio::DirectoryRequest::Clone { flags: FLAGS, .. }))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_randomly_named_file_simple() {
        let (_tmp, proxy) = open_tmp();
        let (path, file) =
            create_randomly_named_file(&proxy, "prefix", fio::OpenFlags::RIGHT_WRITABLE)
                .await
                .unwrap();
        assert!(path.starts_with("prefix"));
        crate::file::close(file).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_randomly_named_file_subdir() {
        let (_tmp, proxy) = open_tmp();
        let _subdir =
            create_directory(&proxy, "subdir", fio::OpenFlags::RIGHT_WRITABLE).await.unwrap();
        let (path, file) =
            create_randomly_named_file(&proxy, "subdir/file", fio::OpenFlags::RIGHT_WRITABLE)
                .await
                .unwrap();
        assert!(path.starts_with("subdir/file"));
        crate::file::close(file).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_randomly_named_file_no_prefix() {
        let (_tmp, proxy) = open_tmp();
        let (_path, file) = create_randomly_named_file(
            &proxy,
            "",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .unwrap();
        crate::file::close(file).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_randomly_named_file_error() {
        let pkg = open_pkg();
        assert_matches!(
            create_randomly_named_file(&pkg, "", fio::OpenFlags::empty()).await,
            Err(_)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_simple() {
        let (tmp, proxy) = open_tmp();
        let (path, file) =
            create_randomly_named_file(&proxy, "", fio::OpenFlags::RIGHT_WRITABLE).await.unwrap();
        crate::file::close(file).await.unwrap();
        rename(&proxy, &path, "new_path").await.unwrap();
        assert!(!tmp.path().join(path).exists());
        assert!(tmp.path().join("new_path").exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_with_subdir() {
        let (tmp, proxy) = open_tmp();
        let _subdir1 =
            create_directory(&proxy, "subdir1", fio::OpenFlags::RIGHT_WRITABLE).await.unwrap();
        let _subdir2 =
            create_directory(&proxy, "subdir2", fio::OpenFlags::RIGHT_WRITABLE).await.unwrap();
        let (path, file) =
            create_randomly_named_file(&proxy, "subdir1/file", fio::OpenFlags::RIGHT_WRITABLE)
                .await
                .unwrap();
        crate::file::close(file).await.unwrap();
        rename(&proxy, &path, "subdir2/file").await.unwrap();
        assert!(!tmp.path().join(path).exists());
        assert!(tmp.path().join("subdir2/file").exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_directory() {
        let (tmp, proxy) = open_tmp();
        let dir = create_directory(&proxy, "dir", fio::OpenFlags::RIGHT_WRITABLE).await.unwrap();
        close(dir).await.unwrap();
        rename(&proxy, "dir", "dir2").await.unwrap();
        assert!(!tmp.path().join("dir").exists());
        assert!(tmp.path().join("dir2").exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_overwrite_existing_file() {
        let (tmp, proxy) = open_tmp();
        std::fs::write(tmp.path().join("foo"), b"foo").unwrap();
        std::fs::write(tmp.path().join("bar"), b"bar").unwrap();
        rename(&proxy, "foo", "bar").await.unwrap();
        assert!(!tmp.path().join("foo").exists());
        assert_eq!(std::fs::read_to_string(tmp.path().join("bar")).unwrap(), "foo");
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_non_existing_src_fails() {
        let (tmp, proxy) = open_tmp();
        assert_matches!(
            rename(&proxy, "foo", "bar").await,
            Err(RenameError::RenameError(zx_status::Status::NOT_FOUND))
        );
        assert!(!tmp.path().join("foo").exists());
        assert!(!tmp.path().join("bar").exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_to_non_existing_subdir_fails() {
        let (tmp, proxy) = open_tmp();
        std::fs::write(tmp.path().join("foo"), b"foo").unwrap();
        assert_matches!(
            rename(&proxy, "foo", "bar/foo").await,
            Err(RenameError::OpenError(OpenError::OpenError(zx_status::Status::NOT_FOUND)))
        );
        assert!(tmp.path().join("foo").exists());
        assert!(!tmp.path().join("bar/foo").exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_root_path_fails() {
        let (tmp, proxy) = open_tmp();
        assert_matches!(
            rename(&proxy, "/foo", "bar").await,
            Err(RenameError::OpenError(OpenError::OpenError(zx_status::Status::INVALID_ARGS)))
        );
        assert!(!tmp.path().join("bar").exists());
    }
}
