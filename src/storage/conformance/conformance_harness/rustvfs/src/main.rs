// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! io.fidl and io2.fidl conformance testing harness for the rust psuedo-fs-mt library

use {
    anyhow::{anyhow, Context as _, Error},
    fidl_fuchsia_io_test::{Io1HarnessRequest, Io1HarnessRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::prelude::*,
    vfs::{
        directory::{entry::DirectoryEntry, helper::DirectlyMutable, mutable::simple},
        execution_scope::ExecutionScope,
        file::vmo::asynchronous::{read_only, NewVmo},
        path::Path,
    },
};

struct Harness(Io1HarnessRequestStream);

async fn run(mut stream: Io1HarnessRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("error running harness server")? {
        match request {
            Io1HarnessRequest::GetEmptyDirectory {
                flags,
                directory_request,
                control_handle: _,
            } => {
                let dir = simple();
                let scope = ExecutionScope::from_executor(Box::new(fasync::EHandle::local()));
                dir.open(scope, flags, 0, Path::empty(), directory_request.into_channel().into());
            }
            Io1HarnessRequest::GetDirectoryWithVmoFile {
                file,
                name,
                flags,
                directory_request,
                control_handle: _,
            } => {
                let dir = simple();
                let size = file.size;
                let mut data = vec![0; size as usize];
                file.vmo.read(&mut data, file.offset)?;
                let data = std::sync::Arc::new(data);
                let file = read_only(move || {
                    let data_clone = data.clone();
                    async move {
                        let vmo = zx::Vmo::create(size)?;
                        vmo.write(&data_clone, 0)?;
                        Ok(NewVmo { vmo, size, capacity: size })
                    }
                });
                dir.clone().add_entry(name, file)?;
                let scope = ExecutionScope::from_executor(Box::new(fasync::EHandle::local()));
                dir.open(scope, flags, 0, Path::empty(), directory_request.into_channel().into());
            }
            _ => return Err(anyhow!("Unsupported request type.")),
        }
    }

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(Harness);
    fs.take_and_serve_directory_handle()?;

    let fut = fs.for_each_concurrent(10_000, |Harness(stream)| {
        run(stream).unwrap_or_else(|e| println!("{:?}", e))
    });

    fut.await;
    Ok(())
}
