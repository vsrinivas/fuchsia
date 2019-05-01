// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    failure::{Error, ResultExt},
    fdio,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{DirectoryMarker, OPEN_RIGHT_READABLE},
    fidl_fuchsia_pkg as fpkg, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self, macros::*},
    fuchsia_uri::pkg_uri::PkgUri,
    fuchsia_zircon::{HandleBased, Status},
    futures::{StreamExt, TryStreamExt},
    std::ffi::CString,
    std::ptr,
};

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["mock_pkg_resolver"]).expect("can't init logger");
    fx_log_info!("starting mock resolver");
    let mut executor = fasync::Executor::new().context("error creating executor")?;
    let mut fs = ServiceFs::new_local();
    fs.dir("public").add_fidl_service(move |stream| {
        fasync::spawn_local(
            async move {
                await!(run_resolver_service(stream)).expect("failed to run echo service")
            },
        );
    });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}

async fn run_resolver_service(mut stream: fpkg::PackageResolverRequestStream) -> Result<(), Error> {
    fx_log_info!("running mock resolver service");
    while let Some(event) = await!(stream.try_next())? {
        let fpkg::PackageResolverRequest::Resolve { package_uri, dir, responder, .. } = event;
        let status = await!(resolve(package_uri, dir));
        responder.send(Status::from(status).into_raw())?;
        if let Err(s) = status {
            fx_log_err!("request failed: {}", s);
        }
    }
    Ok(())
}

async fn resolve(package_uri: String, dir: ServerEnd<DirectoryMarker>) -> Result<(), Status> {
    let uri = PkgUri::parse(&package_uri).map_err(|_| Err(Status::INVALID_ARGS))?;
    let name = uri.name().ok_or_else(|| Err(Status::INVALID_ARGS))?;
    if name != "routing_integration_test" {
        return Err(Status::NOT_FOUND);
    }
    open_in_namespace("/pkg", dir)
}

fn open_in_namespace(path: &str, dir: ServerEnd<DirectoryMarker>) -> Result<(), Status> {
    let mut ns_ptr: *mut fdio::fdio_sys::fdio_ns_t = ptr::null_mut();
    Status::ok(unsafe { fdio::fdio_sys::fdio_ns_get_installed(&mut ns_ptr) })?;
    let cstr = CString::new(path)?;
    Status::ok(unsafe {
        fdio::fdio_sys::fdio_ns_connect(
            ns_ptr,
            cstr.as_ptr(),
            OPEN_RIGHT_READABLE,
            dir.into_channel().into_raw(),
        )
    })?;
    Ok(())
}
