// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! fuchsia.IO Utility library
//!
//! Also, IOU a more complete set of typesafe wrappers around fuchsia.io.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        self as fio, DirectoryEvent, DirectoryMarker, DirectoryProxy, FileEvent, FileMarker,
        FileProxy, NodeInfo,
    },
    fuchsia_zircon::Status,
    futures::prelude::*,
    thiserror::Error,
};

/// An error encountered while opening a node
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum OpenError {
    #[error("while making a fidl proxy: {}", _0)]
    CreateProxy(fidl::Error),

    #[error("while sending open request: {}", _0)]
    SendOpenRequest(fidl::Error),

    #[error("while sending clone request: {}", _0)]
    SendCloneRequest(fidl::Error),

    #[error("node event stream closed prematurely")]
    OnOpenEventStreamClosed,

    #[error("while reading OnOpen event: {}", _0)]
    OnOpenDecode(fidl::Error),

    #[error("open failed with status: {}", _0)]
    OpenError(Status),

    #[error("remote responded with success but provided no node info")]
    MissingOnOpenInfo,

    #[error("expected node to be a {:?}, but got a {:?}", expected, actual)]
    UnexpectedNodeKind { expected: NodeKind, actual: NodeKind },
}

/// The type of a filesystem node
#[derive(Debug, PartialEq, Eq)]
#[allow(missing_docs)]
pub enum NodeKind {
    Service,
    File,
    Directory,
    Pipe,
    Vmofile,
    Device,
    Tty,
    Socket,
}

impl NodeKind {
    fn kind_of(info: &NodeInfo) -> NodeKind {
        match info {
            NodeInfo::Service(_) => NodeKind::Service,
            NodeInfo::File(_) => NodeKind::File,
            NodeInfo::Directory(_) => NodeKind::Directory,
            NodeInfo::Pipe(_) => NodeKind::Pipe,
            NodeInfo::Vmofile(_) => NodeKind::Vmofile,
            NodeInfo::Device(_) => NodeKind::Device,
            NodeInfo::Tty(_) => NodeKind::Tty,
            NodeInfo::Socket(_) => NodeKind::Socket,
        }
    }

    fn expect_file(info: NodeInfo) -> Result<Option<fuchsia_zircon::Event>, NodeKind> {
        match info {
            NodeInfo::File(fio::FileObject { event }) => Ok(event),
            other => Err(NodeKind::kind_of(&other)),
        }
    }

    fn expect_directory(info: NodeInfo) -> Result<(), NodeKind> {
        match info {
            NodeInfo::Directory(fio::DirectoryObject) => Ok(()),
            other => Err(NodeKind::kind_of(&other)),
        }
    }
}

pub(crate) fn open_directory_from_namespace(path: &str) -> Result<DirectoryProxy, anyhow::Error> {
    io_util::open_directory_in_namespace(
        path,
        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
    )
}

pub(crate) fn open_directory_no_describe(
    parent: &DirectoryProxy,
    path: &str,
    flags: u32,
) -> Result<DirectoryProxy, anyhow::Error> {
    let (dir, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;

    let flags = flags | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY;
    let mode = fidl_fuchsia_io::MODE_TYPE_DIRECTORY;

    parent.open(flags, mode, path, ServerEnd::new(server_end.into_channel()))?;

    Ok(dir)
}

pub(crate) async fn clone_directory(
    dir: &DirectoryProxy,
    request: ServerEnd<DirectoryMarker>,
) -> Result<(), OpenError> {
    let node_request = ServerEnd::new(request.into_channel());
    dir.clone(fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS, node_request)
        .map_err(OpenError::SendCloneRequest)
}

pub(crate) async fn open_directory(
    parent: &DirectoryProxy,
    path: &str,
    flags: u32,
) -> Result<DirectoryProxy, OpenError> {
    let (dir, server_end) =
        fidl::endpoints::create_proxy::<DirectoryMarker>().map_err(OpenError::CreateProxy)?;

    let flags = flags | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY | fidl_fuchsia_io::OPEN_FLAG_DESCRIBE;
    let mode = fidl_fuchsia_io::MODE_TYPE_DIRECTORY;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    // wait for the directory to open and report success.
    let mut events = dir.take_event_stream();
    let DirectoryEvent::OnOpen_ { s: status, info } = events
        .next()
        .await
        .ok_or(OpenError::OnOpenEventStreamClosed)?
        .map_err(OpenError::OnOpenDecode)?;

    let () = Status::ok(status).map_err(OpenError::OpenError)?;

    let info = info.ok_or(OpenError::MissingOnOpenInfo)?;

    let () = NodeKind::expect_directory(*info).map_err(|actual| OpenError::UnexpectedNodeKind {
        expected: NodeKind::Directory,
        actual,
    })?;

    Ok(dir)
}

pub(crate) async fn open_file(
    parent: &DirectoryProxy,
    path: &str,
    flags: u32,
) -> Result<FileProxy, OpenError> {
    let (file, server_end) =
        fidl::endpoints::create_proxy::<FileMarker>().map_err(OpenError::CreateProxy)?;

    let flags =
        flags | fidl_fuchsia_io::OPEN_FLAG_NOT_DIRECTORY | fidl_fuchsia_io::OPEN_FLAG_DESCRIBE;
    let mode = fidl_fuchsia_io::MODE_TYPE_FILE;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    // wait for the directory to open and report success.
    let mut events = file.take_event_stream();
    let FileEvent::OnOpen_ { s: status, info } = events
        .next()
        .await
        .ok_or(OpenError::OnOpenEventStreamClosed)?
        .map_err(OpenError::OnOpenDecode)?;

    let () = Status::ok(status).map_err(OpenError::OpenError)?;

    let info = info.ok_or(OpenError::MissingOnOpenInfo)?;

    let _event = NodeKind::expect_file(*info)
        .map_err(|actual| OpenError::UnexpectedNodeKind { expected: NodeKind::File, actual })?;

    Ok(file)
}
