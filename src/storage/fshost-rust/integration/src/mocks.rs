// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::prelude::*,
    fidl_fuchsia_boot as fboot, fidl_fuchsia_io as fio,
    fuchsia_component_test::LocalComponentHandles,
    futures::{future::BoxFuture, FutureExt, StreamExt},
    vfs::{directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path, service},
};

/// Identifier for ramdisk storage. Defined in zircon/system/public/zircon/boot/image.h.
const ZBI_TYPE_STORAGE_RAMDISK: u32 = 0x4b534452;

pub async fn new_mocks(
    netboot: bool,
) -> impl Fn(LocalComponentHandles) -> BoxFuture<'static, Result<(), Error>> + Sync + Send + 'static
{
    let mock = move |handles: LocalComponentHandles| run_mocks(handles, netboot).boxed();

    mock
}

async fn run_mocks(handles: LocalComponentHandles, netboot: bool) -> Result<(), Error> {
    let export = vfs::pseudo_directory! {
        "svc" => vfs::pseudo_directory! {
            fboot::ArgumentsMarker::PROTOCOL_NAME => service::host(move |stream| {
                run_boot_args(stream, netboot)
            }),
            fboot::ItemsMarker::PROTOCOL_NAME => service::host(run_boot_items),
        },
        "dev" => vfs::pseudo_directory! {
            "class" => vfs::pseudo_directory! {
                "block" => vfs::pseudo_directory!{},
                "nand" => vfs::pseudo_directory!{},
            },
        },
    };

    let scope = ExecutionScope::new();
    export.open(
        scope.clone(),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        fio::MODE_TYPE_DIRECTORY,
        Path::dot(),
        fidl::endpoints::ServerEnd::from(handles.outgoing_dir.into_channel()),
    );
    scope.wait().await;

    Ok(())
}

/// fshost uses exactly one boot item - it checks to see if there is an item of type
/// ZBI_TYPE_STORAGE_RAMDISK. If it's there, it's a vmo that represents a ramdisk version of the
/// fvm, and fshost creates a ramdisk from the vmo so it can go through the normal device matching.
/// For now we don't test this, so we make sure that's the only use (if any) and return None.
async fn run_boot_items(mut stream: fboot::ItemsRequestStream) {
    while let Some(request) = stream.next().await {
        match request.unwrap() {
            fboot::ItemsRequest::Get { type_, extra, responder } => {
                assert_eq!(type_, ZBI_TYPE_STORAGE_RAMDISK);
                assert_eq!(extra, 0);
                responder.send(None, 0).unwrap();
            }
            fboot::ItemsRequest::Get2 { type_, extra, responder } => {
                assert_eq!(type_, ZBI_TYPE_STORAGE_RAMDISK);
                assert_eq!((*extra.unwrap()).n, 0);
                responder.send(&mut Ok(Vec::new())).unwrap();
            }
            fboot::ItemsRequest::GetBootloaderFile { .. } => {
                panic!(
                    "unexpectedly called GetBootloaderFile on {}",
                    fboot::ItemsMarker::PROTOCOL_NAME
                );
            }
        }
    }
}

/// fshost expects a set of string and bool arguments to be available. This is a list of all the
/// arguments it looks for. NOTE: For what we are currently testing for, none of these are required,
/// so for now we either return None or the provided default depending on the context.
///
/// String args -
///   blobfs.write-compression-algorithm (optional)
///   blobfs.cache-eviction-policy (optional)
///   zircon.system.pkgfs.file.<path> - used when loading pkgfs and deps from blobfs
///   zircon.system.pkgfs.cmd - used when launching pkgfs
///   factory_verity_seal - only used when writing to the factory partition
/// Bool args -
///   netsvc.netboot (optional; default false)
///   zircon.system.disable-automount (optional; default false)
///   zircon.system.filesystem-check (optional; default false)
async fn run_boot_args(mut stream: fboot::ArgumentsRequestStream, netboot: bool) {
    while let Some(request) = stream.next().await {
        match request.unwrap() {
            fboot::ArgumentsRequest::GetString { key: _, responder } => {
                responder.send(None).unwrap();
            }
            fboot::ArgumentsRequest::GetStrings { keys, responder } => {
                responder.send(&mut keys.into_iter().map(|_| None)).unwrap();
            }
            fboot::ArgumentsRequest::GetBool { key: _, defaultval, responder } => {
                responder.send(defaultval).unwrap();
            }
            fboot::ArgumentsRequest::GetBools { keys, responder } => {
                responder
                    .send(&mut keys.into_iter().map(|bool_pair| {
                        if bool_pair.key == "netsvc.netboot".to_string() && netboot {
                            true
                        } else {
                            bool_pair.defaultval
                        }
                    }))
                    .unwrap();
            }
            fboot::ArgumentsRequest::Collect { .. } => {
                // This seems to be deprecated. Either way, fshost doesn't use it.
                panic!("unexpectedly called Collect on {}", fboot::ArgumentsMarker::PROTOCOL_NAME);
            }
        }
    }
}
