// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync, fuchsia_component::server::ServiceFs, futures::StreamExt,
    test_utils_lib::echo_factory_capability::EchoFactoryCapability,
};

fn main() {
    let mut executor = fasync::Executor::new().expect("error creating executor");
    let mut fs = ServiceFs::new_local();
    let capability = EchoFactoryCapability::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        capability.clone().serve_async(stream);
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");
    executor.run_singlethreaded(fs.collect::<()>());
}
