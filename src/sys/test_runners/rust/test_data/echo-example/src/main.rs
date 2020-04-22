// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fidl_examples_routing_echo as fecho, fuchsia_component::client::connect_to_service};

#[fuchsia_async::run_singlethreaded(test)]
async fn test_echo() {
    let echo = connect_to_service::<fecho::EchoMarker>().expect("error connecting to echo");
    assert_eq!(
        echo.echo_string(Some("test string")).await.unwrap(),
        Some("test string".to_owned())
    );
}
