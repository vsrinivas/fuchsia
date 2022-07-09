// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_component::client::connect_to_protocol;

// This test is expected to fail. A hermetic test should not be able to use the
// the fuchsia.hwinfo.Product capability.
#[fuchsia::test]
async fn hermetic_test_rejects_capabilities() {
    let product = connect_to_protocol::<fidl_fuchsia_hwinfo::ProductMarker>().unwrap();
    let _result = product.get_info().await.unwrap();
}
