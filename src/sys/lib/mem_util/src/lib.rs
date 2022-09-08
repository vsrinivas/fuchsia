// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for working with the `fuchsia.mem` FIDL library.

use fidl::endpoints::ServerEnd;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_mem as fmem;
use fuchsia_zircon_status as zxs;
use std::borrow::Cow;
use thiserror::Error;

/// Open `path` from given `parent` directory, returning an [`fmem::Data`] of the contents.
///
/// Prioritizes returning an [`fmem::Data::Buffer`] if it can be done by reusing a VMO handle
/// from the directory's server.
pub async fn open_file_data(
    parent: &fio::DirectoryProxy,
    path: &str,
) -> Result<fmem::Data, FileError> {
    // open the file READ/DESCRIBE, expecting the server to return OnOpen
    let (file, server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().map_err(FileError::CreateProxy)?;
    parent
        .open(
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_FILE,
            path,
            ServerEnd::new(server_end.into_channel()),
        )
        .map_err(FileError::SendOpenRequest)?;

    match file
        .get_backing_memory(fio::VmoFlags::READ)
        .await
        .map_err(FileError::GetBufferError)?
        .map_err(zxs::Status::from_raw)
    {
        Ok(vmo) => {
            let size = vmo.get_content_size().expect("failed to get VMO size");

            Ok(fmem::Data::Buffer(fmem::Buffer { vmo, size }))
        }
        Err(e) => {
            let _: zxs::Status = e;
            // we still didn't get a VMO handle, fallback to reads over the channel
            let bytes = fuchsia_fs::file::read(&file).await?;
            Ok(fmem::Data::Bytes(bytes))
        }
    }
}

/// Errors that can occur when operating on `DirectoryProxy`s and `FileProxy`s.
#[derive(Debug, Error)]
pub enum FileError {
    #[error("Failed to create a FIDL proxy.")]
    CreateProxy(#[source] fidl::Error),

    #[error("Failed to send an open request.")]
    SendOpenRequest(#[source] fidl::Error),

    #[error("Event stream closed before we received the reply to the DESCRIBE flag.")]
    FileEventStreamClosed,

    #[error("Couldn't read a File event from the channel.")]
    FileEventDecode(#[source] fidl::Error),

    #[error("Open() call failed, as reported in the OnOpen event.")]
    OnOpenError(#[source] zxs::Status),

    #[error("We got an OK from OnOpen but didn't receive NodeInfoDeprecated.")]
    MissingNodeInfo,

    #[error("Couldn't read a file")]
    ReadError(
        #[source]
        #[from]
        fuchsia_fs::file::ReadError,
    ),

    #[error("FIDL call to retrieve a file's buffer failed")]
    GetBufferError(#[source] fidl::Error),
}

/// Retrieve the bytes in `data`, returning a reference if it's a `Data::Bytes` and a copy of
/// the bytes read from the VMO if it's a `Data::Buffer`.
pub fn bytes_from_data<'d>(data: &'d fmem::Data) -> Result<Cow<'d, [u8]>, DataError> {
    Ok(match data {
        fmem::Data::Buffer(buf) => {
            let size = buf.size as usize;
            let mut raw_bytes = Vec::with_capacity(size);
            raw_bytes.resize(size, 0);
            buf.vmo.read(&mut raw_bytes, 0).map_err(DataError::VmoReadError)?;
            Cow::Owned(raw_bytes)
        }
        fmem::Data::Bytes(b) => Cow::Borrowed(b),
        fmem::DataUnknown!() => return Err(DataError::UnrecognizedDataVariant),
    })
}

/// Errors that can occur when operating on `fuchsia.mem.Data` values.
#[derive(Debug, Error, PartialEq)]
pub enum DataError {
    #[error("Couldn't read from VMO")]
    VmoReadError(#[source] zxs::Status),

    #[error("Encountered an unrecognized variant of fuchsia.mem.Data")]
    UnrecognizedDataVariant,
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::{create_proxy, ServerEnd};
    use futures::StreamExt;
    use std::sync::Arc;
    use vfs::{
        directory::entry::DirectoryEntry,
        execution_scope::ExecutionScope,
        file::vmo::asynchronous::read_only_static,
        pseudo_directory,
        remote::{remote_boxed_with_type, RoutingFn},
    };

    #[fuchsia::test]
    async fn bytes_from_read_only_static() {
        let fs = pseudo_directory! {
            // `read_only_static` is a vmo file, returns the buffer in OnOpen
            "foo" => read_only_static("hello, world!"),
        };
        let directory = serve_vfs_dir(fs);

        let data = open_file_data(&directory, "foo").await.unwrap();
        match bytes_from_data(&data).unwrap() {
            Cow::Owned(b) => assert_eq!(b, b"hello, world!"),
            _ => panic!("must produce an owned value from reading contents of fmem::Data::Buffer"),
        }
    }

    /// Test that we get a VMO when the server supports `File/GetBackingMemory`.
    #[fuchsia::test]
    async fn bytes_from_vmo_from_get_buffer() {
        let channel_only_foo: RoutingFn = Box::new(|scope, _flags, _mode, _path, server_end| {
            let server_end: ServerEnd<fio::FileMarker> = ServerEnd::new(server_end.into_channel());
            let (mut file_requests, control) = server_end.into_stream_and_control_handle().unwrap();

            // ack the open request
            control
                .send_on_open_(
                    zxs::Status::OK.into_raw(),
                    Some(&mut fio::NodeInfoDeprecated::File(fio::FileObject {
                        event: None,
                        stream: None,
                    })),
                )
                .unwrap();

            scope.spawn(async move {
                while let Some(Ok(request)) = file_requests.next().await {
                    let vmo = fidl::Vmo::create(13).unwrap();
                    vmo.write(b"hello, world!", 0).unwrap();
                    match request {
                        fio::FileRequest::GetBackingMemory { flags: _, responder } => {
                            responder.send(&mut Ok(vmo)).unwrap()
                        }
                        unexpected => unimplemented!("{:#?}", unexpected),
                    }
                }
            });
        });
        let fs = pseudo_directory! {
            "foo" => remote_boxed_with_type(channel_only_foo, fio::DirentType::File),
        };
        let directory = serve_vfs_dir(fs);

        let data = open_file_data(&directory, "foo").await.unwrap();
        match bytes_from_data(&data).unwrap() {
            Cow::Owned(b) => assert_eq!(b, b"hello, world!"),
            _ => panic!("must produce an owned value from reading contents of fmem::Data::Buffer"),
        }
    }

    /// Test that we correctly fall back to reading through FIDL calls in a channel if the server
    /// doesn't support returning a VMO.
    #[fuchsia::test]
    async fn bytes_from_channel_fallback() {
        // create a fuchsia.io.Node which returns NOT_SUPPORTED on `File/GetBackingMemory`.
        let channel_only_foo: RoutingFn = Box::new(|scope, _flags, _mode, _path, server_end| {
            let server_end: ServerEnd<fio::FileMarker> = ServerEnd::new(server_end.into_channel());
            let (mut file_requests, control) = server_end.into_stream_and_control_handle().unwrap();

            // ack the open request
            control
                .send_on_open_(
                    zxs::Status::OK.into_raw(),
                    Some(&mut fio::NodeInfoDeprecated::File(fio::FileObject {
                        event: None,
                        stream: None,
                    })),
                )
                .unwrap();

            scope.spawn(async move {
                let mut have_sent_bytes = false;
                while let Some(Ok(request)) = file_requests.next().await {
                    match request {
                        fio::FileRequest::GetBackingMemory { flags: _, responder } => {
                            responder.send(&mut Err(zxs::Status::NOT_SUPPORTED.into_raw())).unwrap()
                        }
                        fio::FileRequest::Read { count: _, responder } => {
                            let to_send = if !have_sent_bytes {
                                have_sent_bytes = true;
                                b"hello, world!".to_vec()
                            } else {
                                vec![]
                            };
                            responder.send(&mut Ok(to_send)).unwrap();
                        }
                        unexpected => unimplemented!("{:#?}", unexpected),
                    }
                }
            });
        });
        let fs = pseudo_directory! {
            "foo" => remote_boxed_with_type(channel_only_foo, fio::DirentType::File),
        };
        let directory = serve_vfs_dir(fs);

        let data = open_file_data(&directory, "foo").await.unwrap();
        let data = bytes_from_data(&data).unwrap();
        assert_eq!(
            data,
            Cow::Borrowed(b"hello, world!"),
            "must produce a borrowed value from fmem::Data::Bytes"
        );
    }

    fn serve_vfs_dir(root: Arc<impl DirectoryEntry>) -> fio::DirectoryProxy {
        let fs_scope = ExecutionScope::new();
        let (client, server) = create_proxy::<fio::DirectoryMarker>().unwrap();
        root.open(
            fs_scope.clone(),
            fio::OpenFlags::RIGHT_READABLE,
            0,
            vfs::path::Path::dot(),
            ServerEnd::new(server.into_channel()),
        );
        client
    }
}
