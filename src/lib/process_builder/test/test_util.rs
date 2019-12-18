// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_test_processbuilder::{EnvVar, UtilRequest, UtilRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    std::env,
    std::fs,
};

async fn run_util_server(mut stream: UtilRequestStream) -> Result<(), Error> {
    while let Some(req) = stream.try_next().await.context("error running echo server")? {
        match req {
            UtilRequest::GetArguments { responder } => {
                let args: Vec<String> = env::args().collect();
                responder
                    .send(&mut args.iter().map(String::as_ref))
                    .context("error sending response")?;
            }
            UtilRequest::GetEnvironment { responder } => {
                let mut vars: Vec<EnvVar> =
                    env::vars().map(|v| EnvVar { key: v.0, value: v.1 }).collect();
                responder.send(&mut vars.iter_mut()).context("error sending response")?;
            }
            UtilRequest::DumpNamespace { responder } => {
                let mut contents = Vec::new();
                let mut a =
                    |entry: &fs::DirEntry| contents.push(format!("{}", entry.path().display()));
                visit(std::path::Path::new("/"), &mut a)?;
                responder.send(&contents.join(", ")).context("error sending response")?;
            }
            UtilRequest::ReadFile { path, responder } => {
                let contents = fs::read_to_string(path)
                    .unwrap_or_else(|e| format!("read_to_string failed: {}", e));
                responder.send(&contents).context("error sending response")?;
            }
        }
    }
    Ok(())
}

fn visit(dir: &std::path::Path, cb: &mut dyn FnMut(&fs::DirEntry)) -> Result<(), Error> {
    if dir.is_dir() {
        for entry in fs::read_dir(dir)? {
            let entry = entry?;
            let path = entry.path();
            cb(&entry);
            if path.is_dir() {
                visit(&path, cb)?;
            }
        }
    }
    Ok(())
}

enum IncomingServices {
    Util(UtilRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Note that the util doesn't add services under a subdirectory as would be typical for a
    // component, as that isn't necessary for these tests.
    let mut fs = ServiceFs::new_local();
    fs.add_fidl_service(IncomingServices::Util);

    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10;
    fs.for_each_concurrent(MAX_CONCURRENT, |IncomingServices::Util(stream)| {
        run_util_server(stream).unwrap_or_else(|e| println!("{:?}", e))
    })
    .await;
    Ok(())
}
