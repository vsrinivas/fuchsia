// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_manager_lib::startup,
    failure::{Error, ResultExt},
    fdio,
    fidl::{self, endpoints::create_proxy},
    fidl_fidl_examples_echo as fidl_echo, fidl_fuchsia_io as fio,
    fidl_fuchsia_sys::LoaderMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::{self as zx, HandleBased},
    io_util,
    std::{
        convert::{Into, TryInto},
        ffi::CString,
        ptr,
    },
};

// Installs a handle to /pkg in our namespace as /boot, for component manager to see
fn test_setup() -> Result<(), Error> {
    let pkg_proxy = io_util::open_directory_in_namespace("/pkg", io_util::OPEN_RIGHT_READABLE)?;

    let mut ns_ptr: *mut fdio::fdio_sys::fdio_ns_t = ptr::null_mut();
    let status = unsafe { fdio::fdio_sys::fdio_ns_get_installed(&mut ns_ptr) };
    if status != zx::sys::ZX_OK {
        panic!("bad status returned for fdio_ns_get_installed: {}", zx::Status::from_raw(status));
    }
    let cstr = CString::new("/boot").unwrap();
    let status = unsafe {
        fdio::fdio_sys::fdio_ns_bind(
            ns_ptr,
            cstr.as_ptr(),
            pkg_proxy.into_channel().unwrap().into_zx_channel().into_raw(),
        )
    };
    if status != zx::sys::ZX_OK {
        panic!("bad status returned for fdio_ns_bind: {}", zx::Status::from_raw(status));
    }

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn base_resolver_test() -> Result<(), Error> {
    // This test aims to exercise the fuchsia_base_pkg_resolver, which is automatically enabled if
    // the fuchsia.sys.Loader service is not available. To ensure that we're testing the right
    // resolver, attempt to connect to the loader service and check that we observe PEER_CLOSED
    // when we try to use it.
    let loader_proxy = connect_to_service::<LoaderMarker>()?;
    match loader_proxy.load_url("fuchsia-pkg://fuchsia.com/base_resolver_test").await {
        Err(fidl::Error::ClientWrite(zx::Status::PEER_CLOSED))
        | Err(fidl::Error::ClientChannelClosed(zx::Status::PEER_CLOSED)) => (),
        Err(e) => panic!("unexpected error when checking for fuchsia.sys.Loader: {:?}", e),
        Ok(_) => panic!("the fuchsia.sys.Loader service cannot be present for this test to work"),
    }

    test_setup()?;

    let args = startup::Arguments {
        use_builtin_process_launcher: false,
        use_builtin_vmex: false,
        root_component_url: "fuchsia-boot:///#meta/root.cm".to_string(),
    };
    let model = startup::model_setup(&args).await.context("failed to set up model")?;

    let echo_server_moniker = vec!["echo_server:0"].into();
    let echo_server_realm = model.look_up_realm(&echo_server_moniker).await?;
    let (echo_proxy, echo_server_end) = create_proxy::<fidl_echo::EchoMarker>()?;
    let svc_path = "/svc/fidl.examples.echo.Echo".try_into().unwrap();
    model
        .bind_open_outgoing(
            echo_server_realm,
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            fio::MODE_TYPE_SERVICE,
            &svc_path,
            echo_server_end.into_channel(),
        )
        .await?;

    assert_eq!(Some("hippos!".to_string()), echo_proxy.echo_string(Some("hippos!")).await?);

    Ok(())
}
