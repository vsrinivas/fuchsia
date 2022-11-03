// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_component_test::ScopedInstance;

#[fuchsia::main]
async fn main() {
    let config_lib::Config { .. } = config_lib::Config::take_from_startup_handle();

    // launch echo_client_sc
    let echo_client =
        ScopedInstance::new("test-collection".into(), "#meta/echo_client_sc.cm".into())
            .await
            .unwrap();
    let _binder = echo_client.connect_to_binder().unwrap();

    // sleep until the test completes
    loop {}
}
