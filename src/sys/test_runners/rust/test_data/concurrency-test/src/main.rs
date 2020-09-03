// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fidl_examples_routing_echo as fecho, fuchsia_component::client::connect_to_service};

async fn test_helper(count: i8) {
    let echo = connect_to_service::<fecho::EchoMarker>().expect("error connecting to echo");
    let msg = format!("test str {}", count);
    assert_eq!(echo.echo_string(Some(&msg)).await.unwrap(), Some(msg));
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_echo1() {
    test_helper(1).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_echo2() {
    test_helper(2).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_echo3() {
    test_helper(3).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_echo4() {
    test_helper(4).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_echo5() {
    test_helper(5).await;
}
