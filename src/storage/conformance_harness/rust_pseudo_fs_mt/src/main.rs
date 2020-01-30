// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! io.fidl and io2.fidl conformance testing harness for the rust psuedo-fs-mt library

use {
    anyhow::{anyhow, Context as _, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fidl_fuchsia_io_test::{HarnessRequest, TestCasesRequest, TestCasesRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_vfs_pseudo_fs_mt::{
        directory::{entry::DirectoryEntry, entry_container::DirectlyMutable, mutable::simple},
        execution_scope::ExecutionScope,
        file::vmo::asynchronous::{read_only, NewVmo},
        path::Path,
    },
    fuchsia_zircon as zx,
    futures::prelude::*,
};

struct Harness(HarnessRequest);

#[derive(PartialEq, Eq, Clone, Copy, Debug)]
enum IOVersion {
    V1,
    V2,
}

async fn run(mut stream: TestCasesRequestStream, version: IOVersion) -> Result<(), Error> {
    if version == IOVersion::V2 {
        return Err(anyhow!("v2 not supported"));
    }
    while let Some(request) = stream.try_next().await.context("error running harness server")? {
        match request {
            TestCasesRequest::GetEmptyDirectory { directory_request, control_handle: _ } => {
                let dir = simple();
                let scope = ExecutionScope::from_executor(Box::new(fasync::EHandle::local()));
                let server_end = ServerEnd::new(directory_request);
                let rights = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;
                dir.open(scope, rights, 0, Path::empty(), server_end);
            }
            TestCasesRequest::GetDirectoryWithVmoFile {
                buffer,
                directory_request,
                control_handle: _,
            } => {
                let dir = simple();
                let size = buffer.size - buffer.offset;
                let mut data = vec![0; size as usize];
                buffer.vmo.read(&mut data, buffer.offset)?;
                let data = std::sync::Arc::new(data);
                let file = read_only(move || {
                    let data_clone = data.clone();
                    async move {
                        let vmo = zx::Vmo::create(size)?;
                        vmo.write(&data_clone, 0)?;
                        Ok(NewVmo { vmo, size, capacity: size })
                    }
                });
                dir.clone().add_entry("vmo_file", file)?;
                let scope = ExecutionScope::from_executor(Box::new(fasync::EHandle::local()));
                let server_end = ServerEnd::new(directory_request);
                let rights = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;
                dir.open(scope, rights, 0, Path::empty(), server_end);
            }
        }
    }

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_unified_service(Harness);
    fs.take_and_serve_directory_handle()?;

    let fut = fs.for_each_concurrent(10_000, |request| {
        match request {
            Harness(HarnessRequest::V1(stream)) => run(stream, IOVersion::V1),
            Harness(HarnessRequest::V2(stream)) => run(stream, IOVersion::V2),
        }
        .unwrap_or_else(|e| println!("{:?}", e))
    });

    fut.await;
    Ok(())
}
