// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! io.fidl and io2.fidl conformance testing harness for the rust psuedo-fs-mt library

use {
    anyhow::{anyhow, Context as _, Error},
    fidl_fuchsia_io_test::{Io1Config, Io1HarnessRequest, Io1HarnessRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog as syslog, fuchsia_zircon as zx,
    futures::prelude::*,
    log::error,
    vfs::{
        directory::{entry::DirectoryEntry, helper::DirectlyMutable, mutable::simple},
        execution_scope::ExecutionScope,
        file::pcb,
        file::vmo::asynchronous as vmo,
        path::Path,
    },
};

struct Harness(Io1HarnessRequestStream);

async fn run(mut stream: Io1HarnessRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("error running harness server")? {
        let (dir, flags, directory_request) = match request {
            Io1HarnessRequest::GetConfig { responder } => {
                let config = Io1Config {
                    immutable_file: Some(false),
                    immutable_dir: Some(false),
                    no_exec: Some(false),
                    no_vmofile: Some(false),
                    // TODO(fxbug.dev/33880): Remote directories are supported by the vfs, just
                    // haven't been implemented in this harness yet.
                    no_remote_dir: Some(true),
                };
                responder.send(config)?;
                continue;
            }
            Io1HarnessRequest::GetEmptyDirectory {
                flags,
                directory_request,
                control_handle: _,
            } => {
                let dir = simple();
                (dir, flags, directory_request)
            }
            Io1HarnessRequest::GetDirectoryWithEmptyFile {
                name,
                flags,
                directory_request,
                control_handle: _,
            } => {
                let dir = simple();
                let file = pcb::read_write(
                    || future::ready(Ok(Vec::new())),
                    100,
                    |_content| async move { Ok(()) },
                );
                dir.clone().add_entry(name, file)?;
                (dir, flags, directory_request)
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
                let file = vmo::read_only(move || {
                    let data_clone = data.clone();
                    async move {
                        let vmo = zx::Vmo::create(size)?;
                        vmo.write(&data_clone, 0)?;
                        Ok(vmo::NewVmo { vmo, size, capacity: size })
                    }
                });
                dir.clone().add_entry(name, file)?;
                (dir, flags, directory_request)
            }
            // TODO(fxbug.dev/33880): Implement GetDirectoryWithRemoteDirectory.
            _ => {
                return Err(anyhow!("Unsupported request type: {:?}.", request));
            }
        };
        let scope = ExecutionScope::new();
        dir.open(scope, flags, 0, Path::empty(), directory_request.into_channel().into());
    }

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init().unwrap();

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(Harness);
    fs.take_and_serve_directory_handle()?;

    let fut = fs.for_each_concurrent(10_000, |Harness(stream)| {
        run(stream).unwrap_or_else(|e| error!("Error processing request: {:?}", anyhow!(e)))
    });

    fut.await;
    Ok(())
}
