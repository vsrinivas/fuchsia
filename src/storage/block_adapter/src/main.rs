// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This launches the binary specified in the arguments and takes the block device that is passed via
// the usual startup handle and makes it appear to the child process as an object that will work
// with POSIX I/O.  The object will exist in the child's namespace under /device/block.  At the time
// of writing, this is used to run the fsck-msdosfs and mkfs-msdosfs tools which use POSIX I/O to
// interact with block devices.

use {
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    fidl::endpoints::{create_endpoints, ClientEnd, ServerEnd},
    fidl_fuchsia_hardware_block as fblock, fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_runtime::HandleType,
    fuchsia_zircon::{self as zx, AsHandleRef, HandleBased},
    remote_block_device::{Cache, RemoteBlockClientSync},
    std::{
        io::{Seek, SeekFrom},
        sync::{Arc, Mutex},
    },
    vfs::{
        common::{rights_to_posix_mode_bits, send_on_open_with_error},
        directory::entry::{DirectoryEntry, EntryInfo},
        execution_scope::ExecutionScope,
        file::{connection::io1::create_connection, File, FileIo},
        path::Path,
        pseudo_directory,
    },
};

fn map_to_status(err: Error) -> zx::Status {
    if let Some(status) = err.root_cause().downcast_ref::<zx::Status>() {
        status.clone()
    } else {
        // Print the internal error if we re-map it because we will lose any context after this.
        println!("Internal error: {:?}", err);
        zx::Status::INTERNAL
    }
}

struct BlockFile {
    cache: Mutex<Cache>,
}

impl DirectoryEntry for BlockFile {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: fio::OpenFlags,
        _mode: u32,
        path: Path,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        if !path.is_empty() {
            send_on_open_with_error(flags, server_end, zx::Status::NOT_FILE);
            return;
        }
        create_connection(scope, self, flags, server_end, true, true, false);
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(0, fio::DirentType::File)
    }
}

#[async_trait]
impl File for BlockFile {
    async fn open(&self, _flags: fio::OpenFlags) -> Result<(), zx::Status> {
        Ok(())
    }

    async fn truncate(&self, _length: u64) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    async fn get_backing_memory(&self, _flags: fio::VmoFlags) -> Result<zx::Vmo, zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    async fn get_size(&self) -> Result<u64, zx::Status> {
        Ok(self.cache.lock().unwrap().seek(SeekFrom::End(0)).unwrap())
    }

    async fn get_attrs(&self) -> Result<fio::NodeAttributes, zx::Status> {
        let device_size = self.cache.lock().unwrap().seek(SeekFrom::End(0)).unwrap();
        Ok(fio::NodeAttributes {
            mode: fio::MODE_TYPE_FILE
                | rights_to_posix_mode_bits(/*r*/ true, /*w*/ true, /*x*/ false),
            id: 0,
            content_size: device_size,
            storage_size: device_size,
            link_count: 1,
            creation_time: 0,
            modification_time: 0,
        })
    }

    async fn set_attrs(
        &self,
        _flags: fio::NodeAttributeFlags,
        _attrs: fio::NodeAttributes,
    ) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    async fn close(&self) -> Result<(), zx::Status> {
        Ok(())
    }

    async fn sync(&self) -> Result<(), zx::Status> {
        self.cache.lock().unwrap().flush_device().map_err(map_to_status)
    }

    fn query_filesystem(&self) -> Result<fio::FilesystemInfo, zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }
}

#[async_trait]
impl FileIo for BlockFile {
    async fn read_at(&self, offset: u64, buffer: &mut [u8]) -> Result<u64, zx::Status> {
        self.cache.lock().unwrap().read_at(buffer, offset).map_err(map_to_status)?;
        Ok(buffer.len() as u64)
    }

    async fn write_at(&self, offset: u64, content: &[u8]) -> Result<u64, zx::Status> {
        self.cache.lock().unwrap().write_at(content, offset).map_err(map_to_status)?;
        Ok(content.len() as u64)
    }

    async fn append(&self, _content: &[u8]) -> Result<(u64, u64), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }
}

async fn run(
    client: ClientEnd<fblock::BlockMarker>,
    server: ServerEnd<fio::DirectoryMarker>,
) -> Result<(), Error> {
    let scope = ExecutionScope::new();

    let dir = pseudo_directory! {
        "block" => Arc::new(BlockFile {
            cache: Mutex::new(Cache::new(RemoteBlockClientSync::new(client)?)?),
        }),
    };

    dir.open(
        scope.clone(),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        0,
        Path::dot(),
        ServerEnd::new(server.into_channel()),
    );

    scope.wait().await;

    Ok(())
}

fn main() -> Result<(), Error> {
    let device = ClientEnd::new(zx::Channel::from(
        fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleInfo::new(
            HandleType::User0,
            1,
        ))
        .ok_or(anyhow!("Missing device handle"))?,
    ));

    let (client, server) = create_endpoints::<fio::DirectoryMarker>()?;

    let adapter_thread = std::thread::spawn(move || {
        let mut executor = fasync::LocalExecutor::new().expect("Failed to create executor");
        executor.run_singlethreaded(run(device, server))
    });

    let dir = fdio::create_fd(client.into_handle())?;

    let mut args = std::env::args();
    args.next().ok_or(anyhow!("Expected path of executable"))?;
    let binary = args.next().ok_or(anyhow!("Missing binary argument"))?;

    let mut builder = fdio::SpawnBuilder::new()
        .options(fdio::SpawnOptions::CLONE_ALL)
        .add_dir_to_namespace("/device", dir)?
        .arg(&binary)?;

    for arg in args {
        builder = builder.arg(arg)?;
    }
    builder = builder.arg("/device/block")?;

    let process = builder.spawn_from_path(binary, &fuchsia_runtime::job_default())?;

    process.wait_handle(zx::Signals::PROCESS_TERMINATED, zx::Time::INFINITE)?;
    adapter_thread.join().unwrap()?;

    let info = process.info()?;
    if info.flags & zx::ProcessInfoFlags::EXITED.bits() != 0 {
        if let Ok(return_code) = info.return_code.try_into() {
            std::process::exit(return_code);
        }
    }
    Err(anyhow!("Child failed"))
}
