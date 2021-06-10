// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{TempDirPkgFs, TestEnv},
    fuchsia_async as fasync,
    fuchsia_zircon::Status,
    futures::TryFutureExt,
};

#[fasync::run_singlethreaded(test)]
async fn sync_success() {
    let env = TestEnv::builder().build().await;

    let res = env.proxies.package_cache.sync().await;

    assert_eq!(res.unwrap(), Ok(()));
}

#[fasync::run_singlethreaded(test)]
async fn sync_returns_errs() {
    let test_fs = TempDirPkgFs::new();
    test_fs.emulate_ctl_error();

    let env = TestEnv::builder().pkgfs(test_fs).build().await;

    assert_eq!(
        env.proxies.package_cache.sync().map_ok(|res| res.map_err(Status::from_raw)).await.unwrap(),
        Err(Status::INTERNAL)
    );
}
