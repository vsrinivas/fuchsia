// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

//! Defines a C API to spawn a cloud provider factory on a testloop,
//! so that it can be exposed to Ledger integration tests.

use cloud_provider_memory_diff_lib::CloudControllerFactory;
use fidl::endpoints::RequestStream;
use fidl_fuchsia_ledger_cloud_test::CloudControllerFactoryRequestStream;
use fuchsia_async as fasync;
use fuchsia_async_testloop::async_test_subloop_t;
use fuchsia_zircon::{self as zx, HandleBased};
use fuchsia_zircon_sys::zx_handle_t;

#[no_mangle]
/// Creates a subloop running a `CloudControllerFactory` answering on the given channel.
extern "C" fn cloud_provider_memory_diff_new_cloud_controller_factory(
    channel: zx_handle_t,
) -> *mut async_test_subloop_t {
    let fut = async move {
        let handle = unsafe { zx::Handle::from_raw(channel) };
        let channel = fasync::Channel::from_channel(zx::Channel::from_handle(handle)).unwrap();
        let stream = CloudControllerFactoryRequestStream::from_channel(channel);
        CloudControllerFactory::new(stream).run().await
    };

    fuchsia_async_testloop::new_subloop(fut)
}
