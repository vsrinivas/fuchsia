// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines a C API to spawn a cloud provider factory on a testloop,
//! so that it can be exposed to Ledger integration tests.

use cloud_provider_memory_diff_lib::CloudControllerFactory;
use fidl::endpoints::RequestStream;
use fidl_fuchsia_ledger_cloud_test::CloudControllerFactoryRequestStream;
use fuchsia_async as fasync;
use fuchsia_async_testloop::async_test_subloop_t;
use fuchsia_zircon::{self as zx, HandleBased};
use fuchsia_zircon_sys::zx_handle_t;
use rand::SeedableRng;
use std::cell::RefCell;
use std::rc::Rc;

#[no_mangle]
/// Creates a subloop running a `CloudControllerFactory` answering on the given channel. The random
/// number generator for the factory is deterministically seeded from |seed|.
extern "C" fn cloud_provider_memory_diff_new_cloud_controller_factory(
    channel: zx_handle_t,
    seed: u64,
) -> *mut async_test_subloop_t {
    let fut = async move {
        let rng = Rc::new(RefCell::new(rand_xorshift::XorShiftRng::seed_from_u64(seed)));
        let handle = unsafe { zx::Handle::from_raw(channel) };
        let channel = fasync::Channel::from_channel(zx::Channel::from_handle(handle)).unwrap();
        let stream = CloudControllerFactoryRequestStream::from_channel(channel);
        CloudControllerFactory::new(stream, rng).run().await
    };

    fuchsia_async_testloop::new_subloop(fut)
}
