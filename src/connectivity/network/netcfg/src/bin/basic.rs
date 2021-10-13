// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use netcfg::Never;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let _never: Never = netcfg::run::<netcfg::BasicMode>().await.expect("netcfg exited");
    // TODO(https://github.com/rust-lang/rust/issues/89779): enforce uninhabited on compile
    unreachable!("{} should be uninhabited", _never);
}
