// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    ext4_parser::{construct_fs, ConstructFsError},
    ext4_read_only::structs::{InvalidAddressErrorType, ParsingError},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_storage_ext4::{
        BadDirectory, BadEntryType, BadFile, BannedFeatureIncompat, BlockNumberOutOfBounds,
        BlockSizeInvalid, DirEntry2NonUtf8, ExtentUnexpectedLength, Incompatible, InvalidAddress,
        InvalidBlockGroupDesc, InvalidDirEntry2, InvalidExtent, InvalidExtentHeader,
        InvalidExtentHeaderMagic, InvalidINode, InvalidInputPath, InvalidSuperBlock,
        InvalidSuperBlockMagic, MountVmoResult, OutOfBoundsDirection, ParseError, PathNotFound,
        ReaderReadError, RequiredFeatureIncompat, Server_Request, Server_RequestStream,
        ServiceRequest, Success,
    },
    fuchsia_component::server::ServiceFs,
    futures::{
        future::TryFutureExt,
        stream::{StreamExt, TryStreamExt},
    },
    vfs::{execution_scope::ExecutionScope, path::Path},
};

async fn run_ext4_server(mut stream: Server_RequestStream) -> Result<(), Error> {
    while let Some(req) = stream.try_next().await.context("Error while reading request")? {
        match req {
            Server_Request::MountVmo { source, flags, root, responder } => {
                // Each mount get's its own scope.  We may provide additional control over this
                // scope in the future.  For example, one thing we may want to do is also return an
                // "administrative chanel" that would allow calling "shutdown" on a mount.
                let scope = ExecutionScope::new();

                let mut res = serve_vmo(scope, source, flags, root);

                // If the connection was already closed when we tried to send the result, there is
                // nothing we can do.
                let _ = responder.send(&mut res);
            }
        }
    }
    Ok(())
}

fn construct_fs_error_to_mount_vmo_result(source: ConstructFsError) -> MountVmoResult {
    match source {
        ConstructFsError::VmoReadError(status) => MountVmoResult::VmoReadFailure(status.into_raw()),
        ConstructFsError::ParsingError(error) => {
            let result = match error {
                ParsingError::InvalidSuperBlock(pos) => {
                    ParseError::InvalidSuperBlock(InvalidSuperBlock { position: pos as u64 })
                }
                ParsingError::InvalidSuperBlockMagic(val) => {
                    ParseError::InvalidSuperBlockMagic(InvalidSuperBlockMagic { value: val })
                }
                ParsingError::BlockNumberOutOfBounds(num) => {
                    ParseError::BlockNumberOutOfBounds(BlockNumberOutOfBounds { block_number: num })
                }
                ParsingError::BlockSizeInvalid(bs) => {
                    ParseError::BlockSizeInvalid(BlockSizeInvalid { block_size: bs })
                }
                ParsingError::InvalidBlockGroupDesc(pos) => {
                    ParseError::InvalidBlockGroupDesc(InvalidBlockGroupDesc {
                        position: pos as u64,
                    })
                }
                ParsingError::InvalidInode(num) => {
                    ParseError::InvalidInode(InvalidINode { inode_number: num })
                }
                ParsingError::InvalidExtentHeader => {
                    ParseError::InvalidExtentHeader(InvalidExtentHeader {})
                }
                ParsingError::InvalidExtentHeaderMagic(val) => {
                    ParseError::InvalidExtentHeaderMagic(InvalidExtentHeaderMagic { value: val })
                }
                ParsingError::InvalidExtent(pos) => {
                    ParseError::InvalidExtent(InvalidExtent { position: pos as u64 })
                }
                ParsingError::ExtentUnexpectedLength(size, exp) => {
                    ParseError::ExtentUnexpectedLength(ExtentUnexpectedLength {
                        size: size as u64,
                        expected: exp as u64,
                    })
                }
                ParsingError::InvalidDirEntry2(pos) => {
                    ParseError::InvalidDirEntry2(InvalidDirEntry2 { position: pos as u64 })
                }
                ParsingError::DirEntry2NonUtf8(val) => {
                    ParseError::DirEntry2NonUtf8(DirEntry2NonUtf8 { data: val })
                }
                ParsingError::InvalidInputPath => {
                    // TODO(vfcc): Get the actual path.
                    ParseError::InvalidInputPath(InvalidInputPath { path: "".to_string() })
                }
                ParsingError::PathNotFound(path) => {
                    ParseError::PathNotFound(PathNotFound { path: path })
                }
                ParsingError::BadEntryType(val) => {
                    ParseError::BadEntryType(BadEntryType { value: val })
                }
                ParsingError::BannedFeatureIncompat(val) => {
                    ParseError::BannedFeatureIncompat(BannedFeatureIncompat { value: val })
                }
                ParsingError::RequiredFeatureIncompat(val) => {
                    ParseError::RequiredFeatureIncompat(RequiredFeatureIncompat { value: val })
                }
                ParsingError::Incompatible(msg) => {
                    ParseError::Incompatible(Incompatible { msg: msg })
                }
                ParsingError::BadFile(path) => ParseError::BadFile(BadFile { path: path }),
                ParsingError::BadDirectory(path) => {
                    ParseError::BadDirectory(BadDirectory { path: path })
                }
                ParsingError::SourceReadError(pos) => {
                    ParseError::ReaderReadError(ReaderReadError { position: pos as u64 })
                }
                ParsingError::InvalidAddress(direction, pos, bound) => {
                    let mut dir = OutOfBoundsDirection::Below;
                    if direction == InvalidAddressErrorType::Upper {
                        dir = OutOfBoundsDirection::Above
                    }
                    ParseError::InvalidAddress(InvalidAddress {
                        position: pos as u64,
                        direction: dir,
                        bound: bound as u64,
                    })
                }
            };
            MountVmoResult::ParseError(result)
        }
    }
}

fn serve_vmo(
    scope: ExecutionScope,
    source: Buffer,
    flags: u32,
    root: ServerEnd<DirectoryMarker>,
) -> MountVmoResult {
    let tree = match construct_fs(source) {
        Ok(tree) => tree,
        Err(err) => return construct_fs_error_to_mount_vmo_result(err),
    };

    tree.open(scope, flags, 0, Path::empty(), root.into_channel().into());

    MountVmoResult::Success(Success {})
}

enum IncomingService {
    Server(Server_RequestStream),
    Svc(ServiceRequest),
}

// `run` argument is the number of thread to use for the server.
#[fuchsia_async::run(10)]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(IncomingService::Server)
        .add_unified_service(IncomingService::Svc);

    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10_000;
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, move |request| {
        match request {
            IncomingService::Server(stream) => run_ext4_server(stream),
            IncomingService::Svc(ServiceRequest::Server(stream)) => run_ext4_server(stream),
        }
        .unwrap_or_else(|e| println!("{:?}", e))
    });

    fut.await;
    Ok(())
}

#[cfg(test)]
mod tests {}
