// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia_async::run_singlethreaded]
async fn main() {
    match netcfg::run::<netcfg::VirtualizationEnabled>().await.expect("netcfg exited") {}
}
