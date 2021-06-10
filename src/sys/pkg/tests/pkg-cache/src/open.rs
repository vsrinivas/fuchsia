// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::TestEnv, fuchsia_zircon::Status};

#[fuchsia_async::run_singlethreaded(test)]
async fn not_found_when_package_doesnt_exist() {
    let env = TestEnv::builder().build().await;
    assert_eq!(
        env.open_package("0000000000000000000000000000000000000000000000000000000000000000")
            .await
            .map(|_| ()),
        Err(Status::NOT_FOUND)
    );
}
