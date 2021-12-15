// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fidl_examples_routing_echo as fecho, fuchsia_component_test::ScopedInstance, log::*};

#[fuchsia::test]
async fn core_proxy() {
    let core =
        ScopedInstance::new("realm_builder".into(), "#meta/fake_core.cm".into()).await.unwrap();
    info!("binding to Echo");
    let echo = core.connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>().unwrap();
    let out = echo.echo_string(Some("world")).await.unwrap();
    info!("received echo response");
    assert_eq!(out.unwrap(), "world");
}
