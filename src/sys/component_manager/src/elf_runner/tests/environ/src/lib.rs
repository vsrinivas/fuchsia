// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_elf_test as fet, fuchsia_component::client};

#[fuchsia::test]
async fn test_puppet_has_environ_set() {
    let proxy = client::connect_to_protocol::<fet::ContextMarker>()
        .expect("couldn't connect to context service");
    let environ = proxy.get_environ().await.expect("failed to make fidl call");
    assert_eq!(environ, vec!["ENVIRONMENT=testing", "threadcount=8"]);
}
