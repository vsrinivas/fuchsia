// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::TestEnv, fuchsia_async as fasync};

#[fasync::run_singlethreaded(test)]
async fn sync_success() {
    let env = TestEnv::builder().build();

    let res = env.proxies.package_cache.sync().await;

    assert_eq!(res.unwrap(), Ok(()));
}
