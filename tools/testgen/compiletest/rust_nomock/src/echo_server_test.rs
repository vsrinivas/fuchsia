// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::lib::EchoServerTest;
mod lib;

#[fuchsia::test]
async fn test_echomarker() {
    let instance = EchoServerTest::create_realm().await.expect("setting up test realm");
    let proxy = EchoServerTest::connect_to_echomarker(&instance);
    assert_eq!(
        Some("Hello Fuchsia!".to_string()),
        proxy.echo_string(Some("Hello Fuchsia!")).await.unwrap(),
    );
}

#[fuchsia::test]
async fn test_echomarker2() {
    let instance = EchoServerTest::create_realm().await.expect("setting up test realm");
    let proxy = EchoServerTest::connect_to_echomarker(&instance);
    assert_eq!(
        Some("Hello Fuchsia 2!".to_string()),
        proxy.echo_string(Some("Hello Fuchsia 2!")).await.unwrap(),
    );
}
